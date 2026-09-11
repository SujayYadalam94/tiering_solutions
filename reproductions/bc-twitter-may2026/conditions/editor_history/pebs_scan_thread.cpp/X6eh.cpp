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
            // Skip node 1 cores on C220G5 (cores 10-19 are on NUMA node 1)
            if (cpu >= 10 && cpu < 20)
                continue;
#elif GSL_OPTANE
            // Skip node 1 cores on GSL_OPTANE (cores 10-19 are on NUMA node 1)
            if (cpu >= 16 && cpu < 32)
                continue;
#endif
            for (int type = 0; type < NPBUFTYPES; type++)
            {
                struct perf_event_mmap_page *header = perf_page[cpu][type];

                char *pbuf = (char *)header + header->data_offset;
                __sync_synchronize();

                if (header->data_head == header->data_tail)
                    if (header == nullptr)
                    {
                        continue;
                    }
                    std::cout << "[ARMS] No new samples on cpu " << cpu << ", type " << type << "." << std::endl;
                    continue;
                }
                    if (data_head == data_tail)
                struct perf_event_header *ph =

                struct perf_sample *ps;

                    while (data_tail != data_head)
                    {
                        const uint64_t offset = data_tail % header->data_size;
                {
                        // Ensure at least an event header is contiguous; otherwise wrap to ring start.
                        if (header->data_size - offset < sizeof(struct perf_event_header))
                        {
                            data_tail += (header->data_size - offset);
                            continue;
                        }
                    ps = (struct perf_sample *)(ph);
                        struct perf_event_header *ph = (struct perf_event_header *)(pbuf + offset);
                        if (ph->size < sizeof(struct perf_event_header) || ph->size > header->data_size)
                        {
                            // Corrupted/invalid record size; drop unread buffer to recover.
                            data_tail = data_head;
                            break;
                        }

                        // If record wraps, skip it safely. This avoids reading a torn sample payload.
                        if (header->data_size - offset < ph->size)
                        page_ptr page = // get_tracked_page(ps->addr);
                            data_tail += ph->size;
                            continue;
                        }
                                      << " (page VA 0x" << std::hex << page_va << std::dec << "), PID " << ps->pid
                        struct perf_sample *ps;

                        uint64_t page_va;
                        switch (ph->type)
                        {
                        case PERF_RECORD_SAMPLE:
                            if (ph->size < sizeof(struct perf_sample))
                            {
                                data_tail += ph->size;
                                continue;
                            }

                            ps = (struct perf_sample *)(ph);
                            assert(ps != nullptr);
                            page_va = ps->addr & HUGE_PFN_MASK; // Align to page
                            if (page_va != 0 && !is_access_log_page(page_va) && !is_kernel_page(page_va))

                            {
                                bool added_new_page = false;
                                const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
                                page_ptr page =
                                    get_or_create_tracked_page(ps->addr, cur_generation, cur_generation, false,
                                                               &added_new_page);

                                if (page != nullptr)
                                {
                                    page->accesses[type][curr_access_version]++;
                                    total_samples[type]++;
                                }
                            }
                            break;

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
                case PERF_RECORD_UNTHROTTLE:
    }
                        data_tail += ph->size;
                    }

                    __sync_synchronize();
                    header->data_tail = data_tail;
    std::cout << "[ARMS] Scanning thread terminating..." << std::endl;
    return nullptr;
}
