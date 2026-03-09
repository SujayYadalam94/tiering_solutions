#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <linux/perf_event.h>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <sched.h>

#include "arms_kernel_threads.h"
#include "logging.h"

void *pebs_scan_thread(void *arg)
{
    (void)arg;
    // Set thread affinity if needed
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(SCANNING_THREAD_CPU, &cpuset); // Use a dedicated core
    int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    if (s != 0)
    {
        perror("pthread_setaffinity_np");
        assert(0);
    }

    while (!terminated.load(std::memory_order_relaxed))
    {
        for (int cpu = 0; cpu < PEBS_NPROCS; cpu++)
        {
#ifdef C220G5
            if (cpu >= 10 && cpu < 20)
                continue;
#endif
            for (int type = 0; type < NPBUFTYPES; type++)
            {
                struct perf_event_mmap_page *header = perf_page[cpu][type];
                char *pbuf = (char *)header + header->data_offset;
                __sync_synchronize();

                if (header->data_head == header->data_tail)
                {
                    continue;
                }

                struct perf_event_header *ph =
                    (struct perf_event_header *)(pbuf + (header->data_tail % header->data_size));
                struct perf_sample *ps;

                uint64_t page_va;
                switch (ph->type)
                {
                case PERF_RECORD_SAMPLE:
                    ps = (struct perf_sample *)(ph);
                    assert(ps != nullptr);
                    page_va = ps->addr & HUGE_PFN_MASK; // Align to page
                    if (page_va != 0 && !is_access_log_page(page_va))
                    {
                        bool added_new_page = false;
                        const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
                        std::shared_ptr<page_info> page =
                            get_or_create_tracked_page(page_va, cur_generation, cur_generation, 1, 0, &added_new_page);

                        assert(page != nullptr);
                        {
                            std::lock_guard<std::mutex> page_lock(page->page_lock);
                            page->last_seen_scan = cur_generation;
                            page->last_access_generation = cur_generation;
                            page->accesses[type][curr_access_version]++;
                        }
                        total_samples[type]++;

                        if (added_new_page)
                        {
                            populate_new_page(page_va);
                        }
                    }
                    break;

                case PERF_RECORD_THROTTLE:
                case PERF_RECORD_UNTHROTTLE:
                    // std::cerr << "[ARMS] Warning: " << (ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE")
                    //           << " event received, which is unexpected." << std::endl;
                    break;
                default:
                    std::cerr << "[ARMS] ERROR: Unknown perf_event type " << ph->type << std::endl;
                    break;
                }

                header->data_tail += ph->size;
            }
        }
    }

    std::cout << "[ARMS] Scanning thread terminating..." << std::endl;
    return nullptr;
}
