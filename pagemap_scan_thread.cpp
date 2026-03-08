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

static void scan_process_pages()
{
    if (pagemap_fd < 0)
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

    constexpr size_t kMovePagesBatchSize =
        BASE_PAGE_PER_HUGEPAGE * 128; // Number of base pages per batched move_pages call
    std::vector<void *> addr_batch;
    addr_batch.reserve(kMovePagesBatchSize);

    struct page_slice
    {
        std::shared_ptr<page_info> page;
        size_t start_index;
    };
    std::vector<page_slice> page_slices;
    std::vector<int> status_batch;

    auto flush_move_pages_batch = [&]() {
        if (addr_batch.empty())
        {
            return;
        }

        status_batch.resize(addr_batch.size());
        long ret = numa_move_pages(0, static_cast<unsigned long>(addr_batch.size()), addr_batch.data(), nullptr,
                                   status_batch.data(), 0);
        if (ret != 0)
        {
            perror("[ARMS] Warning: numa_move_pages batch failed");
        }

        for (const auto &slice : page_slices)
        {
            auto &page = slice.page;
            bool invalid_status = false;
            int seen_pages = 0;
            int pages_in_dram = 0;

            for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
            {
                size_t idx = slice.start_index + i;
                int status = status_batch[idx];
                page->page_status[i] = status;

                if (status < 0 && status != -EFAULT && status != -ENOENT)
                {
                    if (ARMS_VERBOSE)
                    {
                        std::cout << "[ARMS] Warning: Invalid page status (" << status << ") for VA 0x" << std::hex
                                  << page->va + i * BASE_PAGE << std::dec << std::endl;
                    }
                    invalid_status = true;
                    break;
                }
                if (status >= 0)
                {
                    seen_pages++;
                }
                if (status == 0)
                {
                    pages_in_dram++;
                }
            }

            if (!invalid_status)
            {
                page->pages_in_dram = pages_in_dram;

                if (seen_pages == 0 && page->seen_pages != 0)
                {
                    page->reset_page_access_fields();
                }
                page->seen_pages = seen_pages;
            }
            page->last_seen_scan = cur_scan;
        }

        addr_batch.clear();
        page_slices.clear();
    };

    while (std::getline(maps_file, line))
    {
        uint64_t start_addr, end_addr;
        char perms[5];

        // Parse the maps line
        if (sscanf(line.c_str(), "%lx-%lx %4s", &start_addr, &end_addr, perms) != 3)
        {
            continue;
        }

        // Skip non-readable and non-writable regions
        if (perms[0] != 'r' && perms[1] != 'w')
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
        for (uint64_t va = start_addr; va < end_addr; va += PAGE_SIZE)
        {
            // Align to page size
            va = va & HUGE_PFN_MASK;
            if (is_access_log_page(va))
            {
                continue;
            }
            /*uint64_t pagemap_index = (va / BASE_PAGE) * sizeof(uint64_t);
            uint64_t pagemap_entry;

            ssize_t ret = pread(pagemap_fd, &pagemap_entry, sizeof(pagemap_entry), pagemap_index);
            if (ret != sizeof(pagemap_entry))
            {
                // std::cerr << "[ARMS] Warning: Failed to read pagemap entry for VA 0x" << std::hex << va << std::dec
                //           << std::endl;
                continue;
            }

            uint64_t pfn = pagemap_entry & 0x7fffffffffffff;
            bool present = (pagemap_entry >> 63) & 1;

            if (!present || pfn == 0)
            {
                // std::cerr << "[ARMS] Warning: Page not present for VA 0x" << std::hex << va << std::dec << std::endl;
            }*/

            std::shared_ptr<page_info> page;
            bool added_new_page = false;
            {
                std::lock_guard<std::mutex> lock(pages_map_lock);
                auto it = pages_map.find(va);
                if (it == pages_map.end())
                {
                    auto new_page = std::make_shared<page_info>();
                    new_page->va = va;
                    new_page->pages_in_dram = 0;
                    new_page->last_seen_scan = cur_scan;
                    pages_map.emplace(va, new_page);
                    page = std::move(new_page);
                    added_new_page = true;
                }
                else
                {
                    page = it->second;
                    page->last_seen_scan = cur_scan;
                }
            }

            if (added_new_page)
            {
                populate_new_page(va);
            }

            if (page)
            {
                size_t start_index = addr_batch.size();
                for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
                {
                    addr_batch.emplace_back((void *)(va + i * BASE_PAGE));
                }
                page_slices.push_back({page, start_index});

                if (addr_batch.size() >= kMovePagesBatchSize)
                {
                    flush_move_pages_batch();
                }
            }
        }
    }

    // Flush any remaining batched pages.
    flush_move_pages_batch();

    size_t removed_pages = 0;
    uint64_t total_base_pages_in_dram = 0;
    {
        std::lock_guard<std::mutex> lock(pages_map_lock);
        for (auto it = pages_map.begin(); it != pages_map.end();)
        {
            if (it->second->last_seen_scan < cur_scan)
            {
                it->second->reset_page_access_fields();
                it = pages_map.erase(it);
                removed_pages++;
            }
            else
            {
                total_base_pages_in_dram += it->second->pages_in_dram;
                ++it;
            }
        }
    }
    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Number of process pages tracked: " << pages_map.size()
                  << ", Removed stale pages: " << removed_pages << std::endl;
    }

    dram_samples.fetch_add(1, std::memory_order_relaxed);
    total_dram_base_pages_accum.fetch_add(total_base_pages_in_dram, std::memory_order_relaxed);

    // Track the peak DRAM residency observed during pagemap scans.
    uint64_t prev_max = max_dram_base_pages_seen.load(std::memory_order_relaxed);
    while (total_base_pages_in_dram > prev_max && !max_dram_base_pages_seen.compare_exchange_weak(
                                                      prev_max, total_base_pages_in_dram, std::memory_order_relaxed))
    {
    }
}

void *pagemap_scan_thread_fn(void *arg)
{
    (void)arg;

    struct ptimer loop_timer;
    ptimer_init(&loop_timer, "Page Map Scan Loop");
    while (!terminated)
    {
        ptimer_start(&loop_timer);
        scan_process_pages();
        ptimer_stop(&loop_timer);
        if (ARMS_VERBOSE)
        {
            ptimer_print(&loop_timer);
        }

        double elapsed_us = loop_timer.elapsed_us;

        if (elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval - elapsed_us);
        }
    }

    return nullptr;
}
