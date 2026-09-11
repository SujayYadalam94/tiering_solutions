#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <regex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "arms_kernel_threads.h"
#include "timer.h"

namespace
{
inline bool shutdown_requested()
{
    return terminated.load(std::memory_order_relaxed);
}

inline uint64_t parse_node_pages(const std::string &line, int node)
{
    const std::string token = "N" + std::to_string(node) + "=";
    const size_t token_pos = line.find(token);
    if (token_pos == std::string::npos)
    {
        return 0;
    }

    const char *value_begin = line.c_str() + token_pos + token.size();
    char *value_end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(value_begin, &value_end, 10);
    if (value_end == value_begin || errno != 0)
    {
        return 0;
    }

    return static_cast<uint64_t>(value);
}

inline bool parse_start_va(const std::string &line, uint64_t *va_out)
{
    if (va_out == nullptr)
    {
        return false;
    }

    const size_t separator = line.find(' ');
    if (separator == std::string::npos)
    {
        return false;
    }

    const std::string va_str = line.substr(0, separator);
    char *parse_end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(va_str.c_str(), &parse_end, 16);
    if (parse_end == va_str.c_str() || *parse_end != '\0' || errno != 0)
    {
        return false;
    }

    *va_out = static_cast<uint64_t>(parsed);
    return true;
}

struct stale_page_cleanup_result
{
    size_t removed_pages = 0;
    uint64_t total_hugepages_in_dram = 0;
    size_t tracked_pages = 0;
};

struct page_slice
{
    page_ptr page;
    size_t start_index;
};

static void refresh_page_residency(uint64_t cur_scan, const std::vector<page_ptr> &pages_to_refresh)
{
    if (pages_to_refresh.empty() || shutdown_requested())
    {
        return;
    }

    std::vector<void *> addr_batch;
    addr_batch.reserve(PAGEMAP_BATCH_HUGEPAGES);

    std::vector<page_slice> page_slices;
    page_slices.reserve(std::min(pages_to_refresh.size(), static_cast<size_t>(PAGEMAP_BATCH_HUGEPAGES)));

    std::vector<int> status_batch;

    auto flush_move_pages_batch = [&]() {
        if (addr_batch.empty() || shutdown_requested())
        {
            return;
        }

        status_batch.resize(addr_batch.size());
        long ret;
        {
            ret = numa_move_pages(0, static_cast<unsigned long>(addr_batch.size()), addr_batch.data(), nullptr,
                                  status_batch.data(), 0);
        }
        if (ret != 0)
        {
            perror("[ARMS] Warning: numa_move_pages batch failed");
        }

        int count = 0;
        for (const auto &slice : page_slices)
        {
            if (shutdown_requested())
            {
                break;
            }

            auto &page = slice.page;
            const size_t idx = slice.start_index;
            const int status = status_batch[idx];
            if (status < 0)
            {
                count++;
                if (status != -EFAULT && status != -ENOENT)
                {
                    if (ARMS_VERBOSE)
                    {
                        std::cout << "[ARMS] Warning: Invalid hugepage status (" << status << ") for VA 0x" << std::hex
                                  << page->va << std::dec << std::endl;
                    }
                }
                else
                {
                    page->reset_page_access_fields();
                }
                page->in_dram = false;
            }
            else
            {
                page->in_dram = (status == FAST_TIER);
            }
            page->last_seen_scan = cur_scan;
        }
        std::cout << "[ARMS] error found for " << count << " hugepages in batch of " << addr_batch.size() << " pages."
                  << std::endl;

        addr_batch.clear();
        page_slices.clear();
    };

    for (const auto &page : pages_to_refresh)
    {
        if (shutdown_requested())
        {
            break;
        }

        if (!page)
        {
            continue;
        }

        size_t start_index = addr_batch.size();
        addr_batch.emplace_back(reinterpret_cast<void *>(page->va));
        page_slices.push_back({page, start_index});

        if (addr_batch.size() >= PAGEMAP_BATCH_HUGEPAGES)
        {
            flush_move_pages_batch();
        }
    }

    flush_move_pages_batch();
}

static void update_dram_residency_stats(uint64_t total_hugepages_in_dram)
{
    dram_samples.fetch_add(1, std::memory_order_relaxed);
    total_dram_hugepages_accum.fetch_add(total_hugepages_in_dram, std::memory_order_relaxed);

    uint64_t prev_max = max_dram_hugepages_seen.load(std::memory_order_relaxed);
    while (total_hugepages_in_dram > prev_max &&
           !max_dram_hugepages_seen.compare_exchange_weak(prev_max, total_hugepages_in_dram, std::memory_order_relaxed))
    {
    }
}

static stale_page_cleanup_result cleanup_stale_pages(uint64_t cur_scan)
{
    stale_page_cleanup_result result;

    std::unique_lock<std::shared_mutex> lock(pages_map_lock);
    for (auto it = pages_map.begin(); it != pages_map.end();)
    {
        if (shutdown_requested())
        {
            break;
        }

        if (it->second->last_seen_scan < cur_scan)
        {
            it->second->reset_page_access_fields();
            it = pages_map.erase(it);
            result.removed_pages++;
        }
        else
        {
            result.total_hugepages_in_dram += (it->second->in_dram ? 1 : 0);
            ++it;
        }
    }
    result.tracked_pages = pages_map.size();

    return result;
}

static void process_batch(std::vector<void *> &addr_batch, std::vector<int> &status_batch,
                          std::vector<page_ptr> &to_add_near, std::vector<page_ptr> &to_add_far,
                          std::vector<uint64_t> &to_remove, int &total_dram, int &total_cxl, uint64_t cur_scan)
{
    long ret = numa_move_pages(0, static_cast<unsigned long>(addr_batch.size()), addr_batch.data(), nullptr,
                               status_batch.data(), 0);
    if (ret != 0)
    {
        perror("[ARMS] Warning: numa_move_pages batch failed");
        return;
    }

    int fast = 0;
    int slow = 0;
    int error = 0;

    for (int i = 0; i < status_batch.size(); i++)
    {
        int status = status_batch[i];
        uint64_t va = reinterpret_cast<uint64_t>(addr_batch[i]);

        if (status == FAST_TIER)
        {
            fast++;
            total_dram++;
            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;
                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_near.push_back(page);
            }
            page->in_dram = true;
            page->fragmented = false;
        }
        else if (status == SLOW_TIER)
        {
            slow++;
            total_cxl++;

            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;

                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_far.push_back(page);
            }
            page->in_dram = false;
            page->fragmented = false;
        }
        else if (status == -EFAULT || status == -ENOENT)
        {
            // Page not present or inaccessible, treat as not in DRAM
            auto page = get_tracked_page(va);
            if (page != nullptr)
            {
                if (page->found_in_pebs)
                {
                    page->found_in_pebs = false;
                    page->fragmented = true;
                }
                else if (!page->fragmented)
                {
                    to_remove.push_back(va);
                }
            }
        }
        else if (status < 0)
        {
            error++;
        }
    }
    addr_batch.clear();
}

static void scan_process_pages_full()
{
    if (pagemap_fd < 0 || shutdown_requested())
        return;

    uint64_t cur_scan = scan_generation.fetch_add(1, std::memory_order_relaxed) + 1;

    // Read /proc/self/maps to get VMA ranges
    std::ifstream maps_file("/proc/self/maps");
    if (!maps_file.is_open())
    {
        perror("Failed to open /proc/self/maps");
        return;
    }

    std::string line;
    std::vector<page_ptr> pages_to_refresh;
    std::vector<uint64_t> new_pages_to_populate;
    std::vector<int> status_batch;
    status_batch.resize(PAGEMAP_BATCH_HUGEPAGES);

    int total_dram = 0;
    int total_cxl = 0;

    std::vector<page_ptr> to_add_near;
    std::vector<page_ptr> to_add_far;
    std::vector<uint64_t> to_remove;
    while (std::getline(maps_file, line))
    {
        if (shutdown_requested())
        {
            break;
        }

        uint64_t start_addr, end_addr;
        char perms[5];

        // Parse the maps line
        if (sscanf(line.c_str(), "%lx-%lx %4s", &start_addr, &end_addr, perms) != 3)
        {
            continue;
        }

        // Track only writable mappings. Python-heavy processes have many read-only
        // interpreter / shared-library VMAs that drastically inflate full-scan time.
        if (perms[0] != 'r')
            continue;

        // Skip kernel regions
        if (is_kernel_page(start_addr))
            continue;

        std::vector<void *> addr_batch;
        addr_batch.reserve(PAGEMAP_BATCH_HUGEPAGES);

        // Scan this VMA range
        uint64_t va = start_addr & HUGE_PFN_MASK;
        for (; va < end_addr; va += PAGE_SIZE)
        {
            if (shutdown_requested())
            {
                break;
            }

            if (is_access_log_page(va))
            {
                continue;
            }

            addr_batch.push_back(reinterpret_cast<void *>(va));
            if (addr_batch.size() >= PAGEMAP_BATCH_HUGEPAGES)
            {
                process_batch(addr_batch, status_batch, to_add_near, to_add_far, to_remove, total_dram, total_cxl,
                              cur_scan);
                addr_batch.clear();
            }
        }
        process_batch(addr_batch, status_batch, to_add_near, to_add_far, to_remove, total_dram, total_cxl, cur_scan);
    }

    for (const auto &page : to_add_near)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        pages_map.emplace(page->va, page);
    }
    for (const auto &page : to_add_far)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        pages_map.emplace(page->va, page);
    }
    for (const auto &va : to_remove)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        pages_map.erase(va);
    }

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Full scan added " << to_add_near.size() << " hugepages in DRAM and " << to_add_far.size()
                  << " hugepages in CXL, and removed " << to_remove.size()
                  << " pages from tracking. Total DRAM hugepages: " << total_dram
                  << ", Total CXL hugepages: " << total_cxl << std::endl;
    }
}

} // namespace

void *pagemap_scan_thread_fn(void *arg)
{
    (void)arg;

    struct ptimer loop_timer;
    ptimer_init(&loop_timer, "Page Map Scan Loop");
    uint64_t iteration = 0;
    while (!shutdown_requested())
    {
        ptimer_start(&loop_timer);
        scan_process_pages_full();
        ptimer_stop(&loop_timer);
        if (ARMS_VERBOSE)
        {
            ptimer_print(&loop_timer);
        }

        double elapsed_us = loop_timer.elapsed_us;

        if (!shutdown_requested() && elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval - elapsed_us);
        }

        iteration++;
    }

    return nullptr;
}
