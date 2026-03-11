#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <regex>
#include <unistd.h>
#include <vector>

#include "arms_kernel_threads.h"
#include "timer.h"

namespace
{
constexpr size_t kMovePagesBatchSize = PAGEMAP_BATCH_HUGEPAGES;

inline bool shutdown_requested()
{
    return terminated.load(std::memory_order_relaxed);
}

struct stale_page_cleanup_result
{
    size_t removed_pages = 0;
    uint64_t total_hugepages_in_dram = 0;
    size_t tracked_pages = 0;
};

struct page_slice
{
    std::shared_ptr<page_info> page;
    size_t start_index;
};

static void refresh_page_residency(uint64_t cur_scan, const std::vector<std::shared_ptr<page_info>> &pages_to_refresh)
{
    if (pages_to_refresh.empty() || shutdown_requested())
    {
        return;
    }

    std::vector<void *> addr_batch;
    addr_batch.reserve(kMovePagesBatchSize);

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

        if (addr_batch.size() >= kMovePagesBatchSize)
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

static bool should_refresh_page_partial(const std::shared_ptr<page_info> &page, uint64_t cur_scan)
{
    const uint64_t generations_since_access =
        (cur_scan > page->last_access_generation) ? (cur_scan - page->last_access_generation) : 0;
    return page->last_access_generation != 0 && generations_since_access <= PAGEMAP_RECENT_ACCESS_WINDOW;
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
    std::vector<std::shared_ptr<page_info>> pages_to_refresh;
    std::vector<uint64_t> new_pages_to_populate;

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
        if (perms[1] != 'w')
            continue;

        // Skip kernel regions
        if (start_addr >= 0x7fffffffffff)
            continue;

        /*
        static const uint64_t sys_page_size = (uint64_t)sysconf(_SC_PAGESIZE);q
        auto align_down = [](uint64_t val, uint64_t align) { return val & ~(align - 1); };
        auto align_up = [](uint64_t val, uint64_t align) { return (val + align - 1) & ~(align - 1); };
        const uint64_t advise_start = align_down(start_addr, sys_page_size);
        const uint64_t advise_end = align_up(end_addr, sys_page_size);
        if (advise_end > advise_start)
        {
            const size_t advise_len = advise_end - advise_start;

            if (madvise((void *)advise_start, advise_len, MADV_WILLNEED))
            {
                perror("[ARMS] Warning: MADV_WILLNEED failed for VA");
                std::cerr << "[ARMS] VA Range: 0x" << std::hex << advise_start << " - 0x" << advise_end
                          << " perms: " << perms << std::dec << std::endl;
            }
            if (perms[1] == 'w' && perms[3] != 's' && madvise((void *)advise_start, advise_len, MADV_POPULATE_WRITE))
            {
                perror("[ARMS] Warning: MADV_POPULATE_WRITE failed for VA");
                std::cerr << "[ARMS] VA Range: 0x" << std::hex << advise_start << " - 0x" << advise_end
                          << " perms: " << perms << std::dec << std::endl;
            }*/

        // This is not working properlly and needs to maybe go per page. This is also very slow.
        /*if (madvise((void *)advise_start, advise_len, MADV_COLLAPSE))
        {
            perror("[ARMS] Warning: MADV_COLLAPSE  failed for VA");
            std::cerr << "[ARMS] VA Range: 0x" << std::hex << advise_start << " - 0x" << advise_end
                      << " perms: " << perms << std::dec << std::endl;
        }
    }*/

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

            bool added_new_page = false;
            std::shared_ptr<page_info> page = get_or_create_tracked_page(va, cur_scan, 0, false, &added_new_page);

            if (page)
            {
                std::lock_guard<std::mutex> page_lock(page->page_lock);
                page->last_seen_scan = cur_scan;
                pages_to_refresh.push_back(page);
            }
        }
    }

    refresh_page_residency(cur_scan, pages_to_refresh);

    stale_page_cleanup_result cleanup = cleanup_stale_pages(cur_scan);
    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Number of process pages tracked: " << cleanup.tracked_pages
                  << ", Refreshed pages: " << pages_to_refresh.size()
                  << ", Removed stale pages: " << cleanup.removed_pages << std::endl;
    }

    update_dram_residency_stats(cleanup.total_hugepages_in_dram);
}

static void scan_process_pages_partial()
{
    if (shutdown_requested())
    {
        return;
    }

    uint64_t cur_scan = scan_generation.fetch_add(1, std::memory_order_relaxed) + 1;
    std::vector<std::shared_ptr<page_info>> pages_to_refresh;

    {
        std::shared_lock<std::shared_mutex> lock(pages_map_lock);
        pages_to_refresh.reserve(pages_map.size());
        for (auto &kv : pages_map)
        {
            if (shutdown_requested())
            {
                break;
            }

            const auto &page = kv.second;
            if (should_refresh_page_partial(page, cur_scan))
            {
                pages_to_refresh.push_back(page);
            }
        }
    }

    refresh_page_residency(cur_scan, pages_to_refresh);

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Partial pagemap refresh selected " << pages_to_refresh.size() << " tracked pages."
                  << std::endl;
    }
}

static void scan_process_pages(uint64_t iteration)
{

    scan_process_pages_full();
    return;
    const uint64_t full_scan_period = std::max<uint64_t>(1, PAGEMAP_FULL_SCAN_INTERVALS);
    if (iteration % full_scan_period == 0)
    {
        scan_process_pages_full();
    }
    else
    {
        scan_process_pages_partial();
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
        scan_process_pages(iteration++);
        ptimer_stop(&loop_timer);
        if (ARMS_VERBOSE)
        {
            ptimer_print(&loop_timer);
        }

        double elapsed_us = loop_timer.elapsed_us;

        if (!shutdown_requested() && elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval * 10 - elapsed_us);
        }
    }

    return nullptr;
}
