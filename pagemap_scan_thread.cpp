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

#include "arms_kernel_threads.h"

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
            uint64_t pagemap_index = (va / BASE_PAGE) * sizeof(uint64_t);
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
            }

            std::shared_ptr<page_info> page;
            bool missing = false;
            bool added_new_page = false;
            {
                std::lock_guard<std::mutex> lock(pages_map_lock);
                auto it = pages_map.find(va);
                if (it == pages_map.end())
                {
                    missing = true;
                }
                else
                {
                    page = it->second;
                    page->last_seen_scan = cur_scan;
                }
            }

            if (missing)
            {
                auto new_page = std::make_shared<page_info>();
                new_page->va = va;

                new_page->pages_in_dram = 0;
                new_page->last_seen_scan = cur_scan;

                std::lock_guard<std::mutex> lock(pages_map_lock);
                auto [it, inserted] = pages_map.emplace(va, new_page);
                if (!inserted)
                {
                    page = it->second;
                }
                else
                {
                    page = new_page;
                    added_new_page = true;
                }
            }

            if (added_new_page)
            {
                populate_new_page(va);
            }

            if (page)
            {
                void *addr[BASE_PAGE_PER_HUGEPAGE];
                for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
                {
                    addr[i] = (void *)(va + i * BASE_PAGE);
                }
                numa_move_pages(0, BASE_PAGE_PER_HUGEPAGE, addr, nullptr, page->page_status, 0);

                // std::lock_guard<std::mutex> lock(page->page_lock);
                bool invalid_status = false;
                int seen_pages = 0;
                int pages_in_dram = 0;
                for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
                {
                    if (page->page_status[i] < 0 && page->page_status[i] != -EFAULT && page->page_status[i] != -ENOENT)
                    {
                        std::cout << "[ARMS] Warning: Invalid page status (" << page->page_status[i] << ") for VA 0x"
                                  << std::hex << va + i * BASE_PAGE << std::dec << std::endl;
                        invalid_status = true;
                        break;
                    }
                    if (page->page_status[i] >= 0)
                    {
                        seen_pages++;
                    }
                    if (page->page_status[i] == 0)
                    {
                        pages_in_dram++;
                    }
                }
                if (!invalid_status)
                {
                    page->pages_in_dram = pages_in_dram;
                    page->seen_pages = seen_pages;
                }
                page->last_seen_scan = cur_scan;
            }
        }
    }

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
    while (!terminated)
    {
        scan_process_pages();
        usleep(policy_thread_interval);
    }

    return nullptr;
}
