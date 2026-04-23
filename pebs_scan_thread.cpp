#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <linux/perf_event.h>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <shared_mutex>
#include <vector>

#include "arms_kernel_threads.h"
#include "logging.h"
#include "model.h"

namespace
{

static void log_virtual_step_rows(const std::vector<page_ptr> &pages)
{
#if PRINT_TRAINING_DATA == (false)
    (void)pages;
    return;
#else
    if (!VIRTUAL_FEATURES_ENABLED || access_log == nullptr || pages.empty())
    {
        return;
    }

    size_t total_virtual_accesses = 0;
    for (const auto &page : pages)
    {
        total_virtual_accesses += static_cast<size_t>(page->virtual_count);
    }

    const size_t virtual_timestep = static_cast<size_t>(virtual_step.load(std::memory_order_relaxed));

    std::vector<struct data_row> rows;
    std::vector<std::shared_ptr<struct page_info>> model_pages;
    rows.reserve(pages.size());
    model_pages.reserve(pages.size());

    for (const auto &page : pages)
    {
        struct data_row row =
            access_log->extract_row(virtual_timestep, page, virtual_grp_tracker, total_virtual_accesses);
        rows.push_back(row);
        model_pages.push_back(page);
    }

    model_predict_batch(rows, model_pages);

    for (size_t i = 0; i < pages.size(); ++i)
    {
        rows[i].score = pages[i]->score;
        access_log->log_row(pages[i], rows[i]);
    }
#endif
}

static void refresh_virtual_group_features(const std::vector<page_ptr> &pages)
{
    if (!VIRTUAL_FEATURES_ENABLED || virtual_grp_tracker == nullptr)
    {
        return;
    }

    reset_group_hash(virtual_grp_tracker);

    size_t total_virtual_accesses = 0;
    for (const auto &page : pages)
    {
        total_virtual_accesses +=
            static_cast<size_t>(page->virtual_accesses[READ]) + static_cast<size_t>(page->virtual_accesses[WRITE]);
    }

    const uint64_t current_virtual_step = virtual_step.load(std::memory_order_relaxed);

    for (const auto &page : pages)
    {
        page->update_virtual_window(total_virtual_accesses, current_virtual_step);
        update_group_entry_values(virtual_grp_tracker, page->va, page->virtual_count, page->virtual_w[1],
                                  static_cast<float>(total_virtual_accesses), page->virtual_age);
    }

    for (const auto &page : pages)
    {
        float group_avg[15] = {0.0f};
        float group_avg_ewma5[15] = {0.0f};
        float group_avg_perc[15] = {0.0f};
        float group_avg_perc_ewma5[15] = {0.0f};
        get_group_window_values(virtual_grp_tracker, page->va, group_avg, group_avg_ewma5, group_avg_perc,
                                group_avg_perc_ewma5);
        for (int8_t i = -7; i <= 7; ++i)
        {
            // Keep legacy *_perc fields as model carriers, but store absolute group stats.
            page->virtual_groups_perc[i + 7] = group_avg[i + 7];
            page->virtual_group_ewma5_perc[i + 7] = group_avg_ewma5[i + 7];
        }

        float sum = 0.0f;
        float sum_sq = 0.0f;
        for (int idx = 0; idx < 15; ++idx)
        {
            const float value = page->virtual_group_ewma5_perc[idx];
            sum += value;
            sum_sq += value * value;
        }
        const float mean = sum / 15.0f;
        const float var = (sum_sq / 15.0f) - (mean * mean);
        page->virtual_group_ewma5_var = var > 0.0f ? var : 0.0f;
    }
}

static void maybe_advance_virtual_step()
{
    if (!VIRTUAL_FEATURES_ENABLED)
    {
        return;
    }

    const uint64_t previous_samples = virtual_sample_total.fetch_add(1, std::memory_order_relaxed);
    const uint64_t current_samples = previous_samples + 1;
    if ((current_samples % VIRTUAL_STEP_SAMPLES) != 0)
    {
        return;
    }

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Advancing virtual step to " << (virtual_step.load(std::memory_order_relaxed) + 1)
                  << " after " << current_samples << " virtual samples." << std::endl;
    }

    virtual_step.fetch_add(1, std::memory_order_relaxed);

    std::vector<page_ptr> page_snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(pages_map_lock);
        page_snapshot.reserve(pages_map.size() + detached_logging_pages.size());
        for (const auto &entry : pages_map)
        {
            page_snapshot.push_back(entry.second);
        }
        for (const auto &entry : detached_logging_pages)
        {
            page_snapshot.push_back(entry.second);
        }
    }

    if (page_snapshot.empty())
    {
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(virtual_features_lock);
        refresh_virtual_group_features(page_snapshot);
    }

    {
        std::unique_lock<std::shared_mutex> pages_lock(pages_map_lock);
        std::shared_lock<std::shared_mutex> virtual_lock(virtual_features_lock);
        for (auto it = detached_logging_pages.begin(); it != detached_logging_pages.end();)
        {
            const auto &page = it->second;
            const bool has_pending = (page->virtual_accesses[READ] != 0) || (page->virtual_accesses[WRITE] != 0);
            if (has_pending)
            {
                ++it;
                continue;
            }
            it = detached_logging_pages.erase(it);
        }
    }

    log_virtual_step_rows(page_snapshot);
}

} // namespace

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
                continue;
#elif GSL_OPTANE
            if (cpu >= 16 && cpu < 32)
                continue;
#endif
            for (int type = 0; type < NPBUFTYPES; type++)
            {
                struct perf_event_mmap_page *header = perf_page[cpu][type];

                char *pbuf = (char *)header + header->data_offset;
                const size_t ring_size = header->data_size;
                __sync_synchronize();

                while (header->data_head != header->data_tail)
                {
                    const size_t off = static_cast<size_t>(header->data_tail % ring_size);

                    struct perf_event_header ph_local{};
                    if (off + sizeof(ph_local) <= ring_size)
                    {
                        memcpy(&ph_local, pbuf + off, sizeof(ph_local));
                    }
                    else
                    {
                        const size_t first = ring_size - off;
                        memcpy(&ph_local, pbuf + off, first);
                        memcpy(reinterpret_cast<char *>(&ph_local) + first, pbuf, sizeof(ph_local) - first);
                    }

                    if (ph_local.size < sizeof(struct perf_event_header) || ph_local.size > ring_size)
                    {
                        header->data_tail = header->data_head;
                        break;
                    }

                    std::vector<char> wrapped_record;
                    const char *record_ptr = nullptr;
                    if (off + ph_local.size <= ring_size)
                    {
                        record_ptr = pbuf + off;
                    }
                    else
                    {
                        wrapped_record.resize(ph_local.size);
                        const size_t first = ring_size - off;
                        memcpy(wrapped_record.data(), pbuf + off, first);
                        memcpy(wrapped_record.data() + first, pbuf, ph_local.size - first);
                        record_ptr = wrapped_record.data();
                    }

                    const struct perf_event_header *ph = reinterpret_cast<const struct perf_event_header *>(record_ptr);
                    const struct perf_sample *ps;

                    switch (ph->type)
                    {
                    case PERF_RECORD_SAMPLE: {
                        if (ph_local.size < sizeof(struct perf_sample))
                        {
                            break;
                        }

                        ps = reinterpret_cast<const struct perf_sample *>(record_ptr);
                        assert(ps != nullptr);

                        if (VIRTUAL_FEATURES_ENABLED && is_migration_worker_tid(static_cast<pid_t>(ps->tid)))
                        {
                            break;
                        }

                        if(VIRTUAL_FEATURES_ENABLED && is_preload_library_ip(ps->ip))
                        {
                            note_preload_library_sample_filtered();
                            break;
                        }

                        if (VIRTUAL_FEATURES_ENABLED && is_preload_library_ip(ps->ip))
                        {
                            note_preload_library_sample_filtered();

                            const uint64_t page_va = ps->addr & HUGE_PFN_MASK;
                            const bool eligible_for_page_accounting =
                                (page_va != 0) && !is_access_log_page(page_va) && !is_kernel_page(page_va);
                            if (eligible_for_page_accounting)
                            {
                                bool added_new_page = false;
                                const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
                                page_ptr page = get_or_create_tracked_page(ps->addr, cur_generation, cur_generation,
                                                                           false, &added_new_page);
                                if (page != nullptr)
                                {
                                    std::shared_lock<std::shared_mutex> lock(virtual_features_lock);
                                    page->virtual_missed_accesses++;
                                }
                            }
                            break;
                        }

                        if (type == WRITE && ps->addr == 0)
                        {
                            break;
                        }

                        const uint64_t page_va = ps->addr & HUGE_PFN_MASK;
                        const bool eligible_for_page_accounting =
                            (page_va != 0) && !is_access_log_page(page_va) && !is_kernel_page(page_va);
                        if (!eligible_for_page_accounting)
                        {
                            break;
                        }

                        bool added_new_page = false;
                        const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
                        page_ptr page = get_or_create_tracked_page(ps->addr, cur_generation, cur_generation, false,
                                                                   &added_new_page);

                        if (page != nullptr)
                        {
                            page->accesses[type][curr_access_version]++;
                            if (VIRTUAL_FEATURES_ENABLED)
                            {
                                {
                                    std::shared_lock<std::shared_mutex> lock(virtual_features_lock);
                                    page->virtual_accesses[type]++;
                                }
                                maybe_advance_virtual_step();
                            }
                            total_samples[type]++;
                        }
                        break;
                    }
                    case PERF_RECORD_THROTTLE:
                    case PERF_RECORD_UNTHROTTLE:
                        std::cout << "[ARMS] Warning: "
                                  << (ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE")
                                  << " event received, which is unexpected on cpu " << cpu << "." << std::endl;
                        break;
                    default:
                        std::cout << "[ARMS] ERROR: Unknown perf_event type " << ph->type << " on cpu " << cpu << "."
                                  << std::endl;
                        break;
                    }

                    header->data_tail += ph_local.size;
                }
            }
        }
    }

    std::cout << "[ARMS] Scanning thread terminating..." << std::endl;
    return nullptr;
}
