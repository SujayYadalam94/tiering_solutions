#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <linux/perf_event.h>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>

#include "arms_kernel_threads.h"
#include "logging.h"

void *pebs_scan_thread(void *arg)
{
    (void)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(SCANNING_THREAD_CPU, &cpuset);
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
            {
                continue;
            }
#elif GSL_OPTANE
            if (cpu >= 16 && cpu < 32)
            {
                continue;
            }
#endif

            for (int type = 0; type < NPBUFTYPES; type++)
            {
                struct perf_event_mmap_page *header = perf_page[cpu][type];
                if (header == nullptr)
                {
                    continue;
                }

                char *pbuf = (char *)header + header->data_offset;
                __sync_synchronize();

                uint64_t data_head = header->data_head;
                uint64_t data_tail = header->data_tail;

                while (data_tail != data_head)
                {
                    const uint64_t offset = data_tail % header->data_size;

                    if (header->data_size - offset < sizeof(struct perf_event_header))
                    {
                        data_tail += (header->data_size - offset);
                        continue;
                    }

                    struct perf_event_header *ph = (struct perf_event_header *)(pbuf + offset);
                    if (ph->size < sizeof(struct perf_event_header) || ph->size > header->data_size)
                    {
                        data_tail = data_head;
                        break;
                    }

                    if (header->data_size - offset < ph->size)
                    {
                        data_tail += ph->size;
                        continue;
                    }

                    switch (ph->type)
                    {
                    case PERF_RECORD_SAMPLE: {
                        if (ph->size < sizeof(struct perf_sample))
                        {
                            data_tail += ph->size;
                            continue;
                        }

                        struct perf_sample *ps = (struct perf_sample *)(ph);
                        assert(ps != nullptr);

                        const uint64_t page_va = ps->addr & HUGE_PFN_MASK;
                        if (page_va != 0 && !is_access_log_page(page_va) && !is_kernel_page(page_va))
                        {
                            bool added_new_page = false;
                            const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
                            page_ptr page = get_or_create_tracked_page(ps->addr, cur_generation, cur_generation, false,
                                                                       &added_new_page);
                            if (page != nullptr)
                            {
                                page->accesses[type][curr_access_version]++;
                                total_samples[type]++;
                            }
                        }
                        break;
                    }
                    case PERF_RECORD_THROTTLE:
                    case PERF_RECORD_UNTHROTTLE:
                        if (ARMS_VERBOSE)
                        {
                            std::cout << "[ARMS] Warning: "
                                      << (ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE")
                                      << " event received, which is unexpected on cpu " << cpu << "." << std::endl;
                        }
                        break;
                    default:
                        if (ARMS_VERBOSE)
                        {
                            std::cout << "[ARMS] ERROR: Unknown perf_event type " << ph->type << " on cpu " << cpu
                                      << "." << std::endl;
                        }
                        break;
                    }

                    data_tail += ph->size;
                }

                __sync_synchronize();
                header->data_tail = data_tail;
            }
        }
    }

    std::cout << "[ARMS] Scanning thread terminating..." << std::endl;
    return nullptr;
}
