#include <cassert>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/ioctl.h>
#include <time.h>
#include "pebs_ordering.h"
#include <chrono>
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

#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
namespace {
bool model_pebs_flag(const char *name)
{
    const char *value = std::getenv(name);
    if (value == nullptr || std::strcmp(value, "0") == 0) return false;
    if (std::strcmp(value, "1") == 0) return true;
    throw std::runtime_error(std::string(name) + " must be 0 or 1");
}
}
bool ordered_model_pebs_enabled()
{
    static const bool value = VIRTUAL_FEATURES_ENABLED && model_pebs_flag("PEBS_MODEL_ORDERED");
    return value;
}
bool discard_filtered_model_pebs_enabled()
{
    static const bool value = VIRTUAL_FEATURES_ENABLED && model_pebs_flag("PEBS_MODEL_DISCARD_FILTERED");
    return value;
}
uint64_t model_pebs_reorder_ns()
{
    static const uint64_t value = [] {
        const char *raw = std::getenv("PEBS_MODEL_REORDER_MS");
        if (raw == nullptr) return uint64_t(100000000);
        char *end = nullptr;
        const unsigned long n = std::strtoul(raw, &end, 10);
        if (*raw == '\0' || *end != '\0' || n < 1 || n > 10000)
            throw std::runtime_error("PEBS_MODEL_REORDER_MS must be between 1 and 10000");
        return uint64_t(n) * 1000000;
    }();
    return value;
}
#endif

namespace
{

static void account_tiering_related_skipped_sample(const struct perf_sample *ps)
{
    if (!VIRTUAL_FEATURES_ENABLED || ps == nullptr || discard_filtered_model_pebs_enabled())
    {
        return;
    }

    const uint64_t page_va = ps->addr & HUGE_PFN_MASK;
    const bool eligible_for_page_accounting =
        (page_va != 0) && !is_access_log_page(page_va) && !is_kernel_page(page_va);
    if (!eligible_for_page_accounting)
    {
        return;
    }

    bool added_new_page = false;
    const uint64_t cur_generation = scan_generation.load(std::memory_order_relaxed);
    page_ptr page = get_or_create_tracked_page(ps->addr, cur_generation, cur_generation, false, &added_new_page);
    if (page != nullptr)
    {
        std::shared_lock<std::shared_mutex> lock(virtual_features_lock);
        page->virtual_missed_accesses++;
    }
}

static void log_new_pages_for_previous_virtual_step(const std::vector<page_ptr> &pages, uint64_t current_step)
{
#if PRINT_TRAINING_DATA == (false)
    (void)pages;
    (void)current_step;
    return;
#else
    if (!VIRTUAL_FEATURES_ENABLED || access_log == nullptr || pages.empty() || current_step == 0)
    {
        return;
    }

    const size_t previous_step = static_cast<size_t>(current_step - 1);

    size_t total_virtual_accesses = 0;
    for (const auto &page : pages)
    {
        total_virtual_accesses += static_cast<size_t>(page->virtual_count);
    }

    std::vector<struct data_row> rows;
    std::vector<std::shared_ptr<struct page_info>> new_pages;
    rows.reserve(pages.size());
    new_pages.reserve(pages.size());

    for (const auto &page : pages)
    {
        if (page == nullptr || page->last_logged_row != nullptr)
        {
            continue;
        }

        const int saved_demotions = page->num_demotions;
        const int saved_promotions = page->num_promotions;
        struct data_row row = access_log->extract_row(previous_step, page, virtual_grp_tracker, total_virtual_accesses);
        page->num_demotions = saved_demotions;
        page->num_promotions = saved_promotions;

        row.step = previous_step;
        zero_cold_start_virtual_row_fields(row);
        fill_virtual_neighbor_group_features(virtual_grp_tracker, page, row);
        row.num_demotions = 0;
        row.num_promotions = 0;

        rows.push_back(row);
        new_pages.push_back(page);
    }

    if (rows.empty())
    {
        return;
    }

    model_predict_batch_observe(rows, new_pages);

    for (size_t i = 0; i < new_pages.size(); ++i)
    {
        rows[i].score = new_pages[i]->score;
        access_log->log_row(new_pages[i], rows[i]);
    }
#endif
}

static void log_virtual_step_rows(const std::vector<page_ptr> &pages, model_score_timing &timing)
{
#if PRINT_TRAINING_DATA == (false)
    (void)pages;
    (void)timing;
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

    model_predict_batch_observe(rows, model_pages, &timing);

    for (size_t i = 0; i < pages.size(); ++i)
    {
        rows[i].score = pages[i]->score;
    }
    timing.finish(rows);

    for (size_t i = 0; i < pages.size(); ++i)
    {
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

    const uint64_t virtual_step_samples = get_virtual_step_samples();
    const uint64_t previous_samples = virtual_sample_total.fetch_add(1, std::memory_order_relaxed);
    const uint64_t current_samples = previous_samples + 1;
    if ((current_samples % virtual_step_samples) != 0)
    {
        return;
    }

    const uint64_t current_virtual_step = virtual_step.fetch_add(1, std::memory_order_relaxed) + 1;

    static std::chrono::steady_clock::time_point previous_virtual_step_time;
    static bool have_previous_virtual_step_time = false;
    const auto now = std::chrono::steady_clock::now();
    if (have_previous_virtual_step_time && current_virtual_step > 1)
    {
        const double step_duration_ms =
            std::chrono::duration<double, std::milli>(now - previous_virtual_step_time).count();
        observe_virtual_step_duration_ms(step_duration_ms);
    }
    previous_virtual_step_time = now;
    have_previous_virtual_step_time = true;

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Advancing virtual step to " << current_virtual_step << " after " << current_samples
                  << " virtual samples; avg step duration = " << get_virtual_step_duration_ms_average()
                  << " ms, time cost scale = " << get_virtual_step_time_cost_scale() << "." << std::endl;
    }

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

    // Backfill the previous virtual step for pages first seen after that step,
    // before virtual windows/ages are advanced for the current step.
    log_new_pages_for_previous_virtual_step(page_snapshot, current_virtual_step);

    model_score_timing timing;
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

    log_virtual_step_rows(page_snapshot, timing);
}

#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
enum sample_reason : uint8_t { accepted, other_pid, runtime_tid, preload_ip, helper_ip, invalid_address };

static sample_reason classify_sample(const perf_sample &ps)
{
    const bool filters = (!ENABLE_MIGRATION_WORKERS || VIRTUAL_FEATURES_ENABLED);
    if (filters && static_cast<pid_t>(ps.pid) != target_pid) {
        note_other_pid_sample_filtered(); return other_pid;
    }
    if (filters && is_tiering_runtime_tid(static_cast<pid_t>(ps.tid))) {
        note_tiering_runtime_tid_sample_filtered(); return runtime_tid;
    }
    if (filters && is_preload_library_ip(ps.ip)) {
        note_preload_library_sample_filtered(); return preload_ip;
    }
    if (filters && is_helper_library_ip(ps.ip)) {
        note_helper_library_sample_filtered(); return helper_ip;
    }
    const uint64_t page_va = ps.addr & HUGE_PFN_MASK;
    if (page_va == 0 || is_access_log_page(page_va) || is_kernel_page(page_va))
        return invalid_address;
    return accepted;
}

static void apply_sample(const perf_sample &ps, int type, sample_reason reason)
{
    if (reason == other_pid || reason == invalid_address) return;
    if (reason != accepted) {
        account_tiering_related_skipped_sample(&ps);
        return;
    }
    bool added_new_page = false;
    const uint64_t generation = scan_generation.load(std::memory_order_relaxed);
    page_ptr page = get_or_create_tracked_page(ps.addr, generation, generation, false, &added_new_page);
    if (page != nullptr) {
        page->accesses[type][curr_access_version]++;
        if (VIRTUAL_FEATURES_ENABLED) {
            {
                std::shared_lock<std::shared_mutex> lock(virtual_features_lock);
                page->virtual_accesses[type]++;
            }
            maybe_advance_virtual_step();
        }
        total_samples[type]++;
    }
}

struct collector_stats {
    std::array<uint64_t, 6> reasons{};
    uint64_t lost = 0, malformed = 0, throttles = 0, unknown = 0, overflow = 0, errors = 0;
    uint64_t late = 0, processed = 0, max_queue = 0, max_pending = 0, drain_cycles = 0;
};

static std::string collector_output_base()
{
    const char *raw = std::getenv("LOG_OUTPUT_PATH");
    std::string path = raw != nullptr ? raw : "pebs_collection";
    for (const std::string suffix : {".log", ".parquet"})
        if (path.size() >= suffix.size() && path.compare(path.size()-suffix.size(), suffix.size(), suffix) == 0) {
            path.resize(path.size()-suffix.size()); break;
        }
    return path;
}

static void write_collector_stats(const collector_stats &s)
{
    if (!VIRTUAL_FEATURES_ENABLED) return;
    const bool valid = s.reasons[accepted] > 0 && s.lost == 0 && s.malformed == 0 && s.throttles == 0 &&
                       s.unknown == 0 && s.overflow == 0 && s.errors == 0 && s.late == 0;
    std::ofstream out(collector_output_base() + ".pebs.json");
    out << "{\n  \"mode\": \"" << (ordered_model_pebs_enabled() ? "timestamp_ordered" : "legacy") << "\",\n"
        << "  \"discard_filtered\": " << (discard_filtered_model_pebs_enabled() ? "true" : "false") << ",\n"
        << "  \"reorder_window_ns\": " << (ordered_model_pebs_enabled() ? model_pebs_reorder_ns() : 0) << ",\n"
        << "  \"valid\": " << (valid ? "true" : "false") << ",\n"
        << "  \"accepted_samples\": " << s.reasons[accepted] << ",\n"
        << "  \"filtered_other_pid\": " << s.reasons[other_pid] << ",\n"
        << "  \"filtered_runtime_tid\": " << s.reasons[runtime_tid] << ",\n"
        << "  \"filtered_preload_ip\": " << s.reasons[preload_ip] << ",\n"
        << "  \"filtered_helper_ip\": " << s.reasons[helper_ip] << ",\n"
        << "  \"invalid_address\": " << s.reasons[invalid_address] << ",\n"
        << "  \"lost_samples\": " << s.lost << ",\n"
        << "  \"malformed_records\": " << s.malformed << ",\n"
        << "  \"throttle_records\": " << s.throttles << ",\n"
        << "  \"unknown_records\": " << s.unknown << ",\n"
        << "  \"queue_overflow\": " << s.overflow << ",\n"
        << "  \"reader_errors\": " << s.errors << ",\n"
        << "  \"late_samples\": " << s.late << ",\n"
        << "  \"processed_ordered_records\": " << s.processed << ",\n"
        << "  \"max_queue_records\": " << s.max_queue << ",\n"
        << "  \"max_pending_records\": " << s.max_pending << ",\n"
        << "  \"drain_cycles\": " << s.drain_cycles << ",\n"
        << "  \"completed_virtual_steps\": " << virtual_step.load() << ",\n"
        << "  \"partial_step_samples\": " << virtual_sample_total.load() % get_virtual_step_samples() << "\n}\n";
    if (!out) std::cerr << "[ARMS] Failed writing PEBS audit sidecar" << std::endl;
    std::cout << "[ARMS] PEBS audit: " << (valid ? "valid" : "INVALID") << ", accepted=" << s.reasons[accepted]
              << ", lost=" << s.lost << ", late=" << s.late << std::endl;
}

static bool monitored_cpu(int cpu)
{
#ifdef C220G5
    return !(cpu >= 10 && cpu < 20);
#elif GSL_OPTANE
    return !(cpu >= 16 && cpu < 32);
#else
    (void)cpu;
    return true;
#endif
}

static uint64_t monotonic_ns()
{
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) throw std::runtime_error("CLOCK_MONOTONIC unavailable");
    return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}

static perf_sample legacy_sample(const pebs_ordering::sample &s)
{
    perf_sample result{};
    result.ip = s.ip; result.pid = s.pid; result.tid = s.tid; result.addr = s.addr;
    return result;
}

static void run_ordered_collector()
{
    struct batch { std::vector<pebs_ordering::sample> records; uint64_t watermark = 0; };
    struct channel {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<batch> batches;
        size_t queued = 0;
        bool finished = false;
    } channel;
    constexpr size_t max_records = 4000000; // Bounded memory; overflow invalidates the run.
    std::atomic<bool> stop{false};
    collector_stats stats;
    const uint64_t delay = model_pebs_reorder_ns();
    const bool discard_filtered = discard_filtered_model_pebs_enabled();
    std::thread reader([&] {
        try {
            register_tiering_runtime_tid();
            cpu_set_t cpus;
            CPU_ZERO(&cpus); CPU_SET(0, &cpus);
            if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0)
                throw std::runtime_error("Cannot pin PEBS drain thread to CPU 0");
            uint64_t sequence = 0;
            bool final_drain = false;
            while (!stop.load(std::memory_order_relaxed)) {
                if (terminated.load(std::memory_order_relaxed)) {
                    for (int cpu = 0; cpu < PEBS_NPROCS; ++cpu) if (monitored_cpu(cpu))
                        for (int type = 0; type < NPBUFTYPES; ++type)
                            if (ioctl(perf_fd[cpu][type], PERF_EVENT_IOC_DISABLE, 0) != 0) ++stats.errors;
                    final_drain = true;
                }
                // Every ring head is read after this boundary. Retain a grace
                // period for delayed PEBS delivery; detect any later violation.
                const uint64_t boundary = monotonic_ns();
                batch current;
                current.watermark = final_drain ? UINT64_MAX : (boundary > delay ? boundary - delay : 0);
                for (int cpu = 0; cpu < PEBS_NPROCS; ++cpu) if (monitored_cpu(cpu)) {
                    for (int type = 0; type < NPBUFTYPES; ++type) {
                        auto *header = perf_page[cpu][type];
                        const char *ring = reinterpret_cast<const char *>(header) + header->data_offset;
                        const uint64_t size = header->data_size;
                        const uint64_t head = __atomic_load_n(&header->data_head, __ATOMIC_ACQUIRE);
                        uint64_t tail = header->data_tail;
                        while (tail != head) {
                            perf_event_header ph{};
                            if (!pebs_ordering::copy_ring(ring, size, head, tail, &ph, sizeof(ph)) ||
                                ph.size < sizeof(ph) || ph.size > head - tail || ph.size > size) {
                                ++stats.malformed; tail = head; break;
                            }
                            std::array<char, 64> raw{};
                            if (ph.size > raw.size() ||
                                !pebs_ordering::copy_ring(ring, size, head, tail, raw.data(), ph.size)) {
                                ++stats.malformed; tail += ph.size; continue;
                            }
                            if (ph.type == PERF_RECORD_SAMPLE) {
                                pebs_ordering::sample value;
                                if (!pebs_ordering::decode_sample(raw.data(), ph.size, true, value)) ++stats.malformed;
                                else {
                                    value.cpu = cpu; value.type = type; value.sequence = sequence++;
                                    const auto ps = legacy_sample(value);
                                    const auto reason = classify_sample(ps);
                                    value.reason = reason;
                                    ++stats.reasons[reason];
                                    if (reason == accepted || (!discard_filtered && reason >= runtime_tid && reason <= helper_ip)) {
                                        if (current.records.size() == max_records) { ++stats.overflow; stop.store(true); }
                                        else current.records.push_back(value);
                                    }
                                }
                            } else if (ph.type == PERF_RECORD_LOST) {
                                if (ph.size < sizeof(perf_sample_lost)) ++stats.malformed;
                                else { perf_sample_lost loss{}; std::memcpy(&loss, raw.data(), sizeof(loss)); stats.lost += loss.lost; }
                            } else if (ph.type == PERF_RECORD_LOST_SAMPLES) {
                                if (ph.size < sizeof(ph)+8) ++stats.malformed;
                                else { uint64_t loss; std::memcpy(&loss, raw.data()+sizeof(ph), 8); stats.lost += loss; }
                            } else if (ph.type == PERF_RECORD_THROTTLE || ph.type == PERF_RECORD_UNTHROTTLE) ++stats.throttles;
                            else ++stats.unknown;
                            tail += ph.size;
                        }
                        __atomic_store_n(&header->data_tail, tail, __ATOMIC_RELEASE);
                    }
                }
                ++stats.drain_cycles;
                {
                    std::lock_guard<std::mutex> lock(channel.mutex);
                    if (channel.queued + current.records.size() > max_records) {
                        ++stats.overflow; stop.store(true);
                    } else if (current.records.empty() && !channel.batches.empty()) {
                        channel.batches.back().watermark = current.watermark;
                    } else {
                        channel.queued += current.records.size();
                        stats.max_queue = std::max<uint64_t>(stats.max_queue, channel.queued);
                        channel.batches.push_back(std::move(current));
                    }
                }
                channel.ready.notify_one();
                if (final_drain) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (const std::exception &error) {
            ++stats.errors;
            std::cerr << "[ARMS] PEBS reader failed: " << error.what() << std::endl;
        }
        { std::lock_guard<std::mutex> lock(channel.mutex); channel.finished = true; }
        channel.ready.notify_one();
    });
    pebs_ordering::merger merger;
    bool processor_error = false;
    try {
        for (;;) {
            batch current;
            {
                std::unique_lock<std::mutex> lock(channel.mutex);
                channel.ready.wait(lock, [&] { return channel.finished || !channel.batches.empty(); });
                if (channel.batches.empty()) break;
                current = std::move(channel.batches.front());
                channel.batches.pop_front();
                channel.queued -= current.records.size();
            }
            if (merger.size() + current.records.size() > max_records)
                throw std::runtime_error("PEBS reorder buffer limit exceeded");
            for (const auto &value : current.records) merger.push(value);
            stats.max_pending = std::max<uint64_t>(stats.max_pending, merger.size());
            merger.release(current.watermark, [&](const pebs_ordering::sample &value) {
                const auto ps = legacy_sample(value);
                apply_sample(ps, value.type, static_cast<sample_reason>(value.reason));
                ++stats.processed;
            });
        }
        merger.release(UINT64_MAX, [&](const pebs_ordering::sample &value) {
            const auto ps = legacy_sample(value);
            apply_sample(ps, value.type, static_cast<sample_reason>(value.reason));
            ++stats.processed;
        });
    } catch (const std::exception &error) {
        processor_error = true;
        stop.store(true);
        std::cerr << "[ARMS] Ordered PEBS processing failed: " << error.what() << std::endl;
    }
    reader.join();
    stats.errors += processor_error;
    stats.late = merger.late_samples;
    write_collector_stats(stats);
}
#endif

} // namespace

void *pebs_scan_thread(void *arg)
{
    (void)arg;
    register_tiering_runtime_tid();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(SCANNING_THREAD_CPU, &cpuset);
    int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    if (s != 0)
    {
        perror("pthread_setaffinity_np");
        assert(0);
    }

#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
    if (ordered_model_pebs_enabled()) {
        run_ordered_collector();
        return nullptr;
    }
    collector_stats stats;
#endif
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
#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
                        ++stats.malformed;
#endif
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

#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
                        const auto reason = classify_sample(*ps);
                        stats.reasons[reason]++;
                        apply_sample(*ps, type, reason);
#else
                        // Preserve May 14 ARMS sample accounting; model collection stays separate.
                        const bool apply_filters = (!ENABLE_MIGRATION_WORKERS || VIRTUAL_FEATURES_ENABLED);

                        if (apply_filters && static_cast<pid_t>(ps->pid) != target_pid)
                        {
                            note_other_pid_sample_filtered();
                            break;
                        }

                        if (apply_filters && is_tiering_runtime_tid(static_cast<pid_t>(ps->tid)))
                        {
                            note_tiering_runtime_tid_sample_filtered();
                            account_tiering_related_skipped_sample(ps);
                            break;
                        }

                        if (apply_filters && is_preload_library_ip(ps->ip))
                        {
                            note_preload_library_sample_filtered();
                            account_tiering_related_skipped_sample(ps);
                            break;
                        }

                        // Filter common same-pid runtime/helper libraries so their
                        // read-heavy bookkeeping does not stretch virtual steps.
                        if (apply_filters && is_helper_library_ip(ps->ip))
                        {
                            note_helper_library_sample_filtered();
                            account_tiering_related_skipped_sample(ps);
                            break;
                        }

                        if (apply_filters && type == WRITE && ps->addr == 0)
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
#endif
                        break;
                    }
#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
                    case PERF_RECORD_LOST: {
                        if (ph_local.size >= sizeof(perf_sample_lost)) {
                            perf_sample_lost lost{};
                            std::memcpy(&lost, record_ptr, sizeof(lost));
                            stats.lost += lost.lost;
                        } else ++stats.malformed;
                        break;
                    }
                    case PERF_RECORD_LOST_SAMPLES: {
                        uint64_t lost = 0;
                        if (ph_local.size >= sizeof(perf_event_header) + sizeof(lost)) {
                            std::memcpy(&lost, record_ptr + sizeof(perf_event_header), sizeof(lost));
                            stats.lost += lost;
                        } else ++stats.malformed;
                        break;
                    }
#endif
                    case PERF_RECORD_THROTTLE:
                    case PERF_RECORD_UNTHROTTLE:
#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
                        ++stats.throttles;
#endif
                        std::cout << "[ARMS] Warning: "
                                  << (ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE")
                                  << " event received, which is unexpected on cpu " << cpu << "." << std::endl;
                        break;
                    default:
#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
                        ++stats.unknown;
#endif
                        std::cout << "[ARMS] ERROR: Unknown perf_event type " << ph->type << " on cpu " << cpu << "."
                                  << std::endl;
                        break;
                    }

                    header->data_tail += ph_local.size;
                }
            }
        }
    }

#if USE_MODEL || VIRTUAL_FEATURES_ENABLED
    write_collector_stats(stats);
#endif
    std::cout << "[ARMS] Scanning thread terminating..." << std::endl;
    return nullptr;
}
