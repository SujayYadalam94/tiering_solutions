// BEGIN isolated migration diagnostic. No policy decisions or retry changes.
#include <atomic>
#include <cerrno>
#include <cstdint>

namespace {
#define MIGRATION_PROBE_COUNTERS(X) \
    X(batch_entries) X(input_hugepages) X(filtered_hugepages) \
    X(mixed_filtered_batches) X(unfiltered_nonempty_batches) \
    X(all_filtered_batches) X(empty_batches) \
    X(started_calls) X(completed_calls) X(inflight_calls) \
    X(requested_addresses) X(completed_call_nanoseconds) \
    X(return_zero_calls) X(return_positive_calls) X(return_positive_sum) \
    X(return_minus_one_calls) X(return_other_negative_calls) \
    X(errno_enomem_calls) X(errno_efault_calls) X(errno_einval_calls) \
    X(errno_eacces_calls) X(errno_eperm_calls) X(errno_ebusy_calls) \
    X(errno_eio_calls) X(errno_other_calls) \
    X(valid_status_entries) X(status_target) X(status_other_node) \
    X(status_enomem) X(status_ebusy) X(status_enoent) X(status_efault) \
    X(status_untouched_sentinel) X(status_other_negative) \
    X(positive_errno_enomem_calls) X(positive_errno_changed_calls) \
    X(positive_success_statuses) X(positive_errno_enomem_success_statuses) \
    X(early_enomem_branch_calls) X(early_enomem_valid_success_statuses) \
    X(wrong_index_opportunity_slots) X(wrong_index_loop_reached_slots) \
    X(wrong_index_valid_success_slots)

enum ProbeCounter {
#define PROBE_ENUM(name) name,
    MIGRATION_PROBE_COUNTERS(PROBE_ENUM)
#undef PROBE_ENUM
    probe_counter_count
};
static constexpr const char *probe_counter_names[] = {
#define PROBE_NAME(name) #name,
    MIGRATION_PROBE_COUNTERS(PROBE_NAME)
#undef PROBE_NAME
};
#undef MIGRATION_PROBE_COUNTERS

// Value initialization gives zeroed constant-initialized atomic storage. No
// allocation, dynamically registered callback, or global constructor is added.
struct alignas(64) ProbeLane {
    std::atomic<uint64_t> values[probe_counter_count]{};
};
static ProbeLane probe_lanes[4]{};
static std::atomic<bool> probe_reported{false};
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "The diagnostic requires lock-free uint64 counters");

static unsigned probe_lane(int target_node, bool base_pages)
{
    return (target_node == FAST_TIER ? 0U : 2U) + (base_pages ? 1U : 0U);
}

static void probe_add(ProbeLane &lane, ProbeCounter counter, uint64_t value)
{
    if (value != 0)
        lane.values[counter].fetch_add(value, std::memory_order_relaxed);
}

static void probe_note_huge_batch(size_t input, size_t filtered, int target_node)
{
    const int saved_errno = errno;
    ProbeLane &lane = probe_lanes[probe_lane(target_node, false)];
    probe_add(lane, batch_entries, 1);
    probe_add(lane, input_hugepages, input);
    probe_add(lane, filtered_hugepages, filtered);
    if (input == 0)
        probe_add(lane, empty_batches, 1);
    else if (filtered == input)
        probe_add(lane, all_filtered_batches, 1);
    else if (filtered != 0)
        probe_add(lane, mixed_filtered_batches, 1);
    else
        probe_add(lane, unfiltered_nonempty_batches, 1);
    errno = saved_errno;
}

// This is a local wrapper, not ELF/libnuma interposition. Restore errno before
// the real call and after all timing/counting so stale errno remains observable
// to the original control flow exactly as supplied by libnuma.
static long probe_move_pages(std::vector<void *> &vas, std::vector<int> &nodes,
                             std::vector<int> &status, int target_node,
                             bool base_pages, const std::vector<page_ptr> *pages)
{
    const int incoming_errno = errno;
    ProbeLane &lane = probe_lanes[probe_lane(target_node, base_pages)];
    probe_add(lane, started_calls, 1);
    probe_add(lane, requested_addresses, vas.size());
    lane.values[inflight_calls].fetch_add(1, std::memory_order_relaxed);

    const auto start = std::chrono::steady_clock::now();
    errno = incoming_errno;
    const long ret = numa_move_pages(0, vas.size(), vas.data(), nodes.data(),
                                    status.data(), MPOL_MF_MOVE_ALL);
    const int result_errno = errno;
    const auto end = std::chrono::steady_clock::now();

    uint64_t local[probe_counter_count]{};
    local[completed_call_nanoseconds] = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    if (ret == 0)
        ++local[return_zero_calls];
    else if (ret > 0) {
        ++local[return_positive_calls];
        local[return_positive_sum] = static_cast<uint64_t>(ret);
        if (result_errno == ENOMEM)
            ++local[positive_errno_enomem_calls];
        if (result_errno != incoming_errno)
            ++local[positive_errno_changed_calls];
    } else if (ret == -1) {
        ++local[return_minus_one_calls];
        switch (result_errno) {
        case ENOMEM: ++local[errno_enomem_calls]; break;
        case EFAULT: ++local[errno_efault_calls]; break;
        case EINVAL: ++local[errno_einval_calls]; break;
        case EACCES: ++local[errno_eacces_calls]; break;
        case EPERM: ++local[errno_eperm_calls]; break;
        case EBUSY: ++local[errno_ebusy_calls]; break;
        case EIO: ++local[errno_eio_calls]; break;
        default: ++local[errno_other_calls]; break;
        }
    } else
        ++local[return_other_negative_calls];

    const bool early_enomem = ret != 0 && result_errno == ENOMEM;
    if (early_enomem)
        ++local[early_enomem_branch_calls];

    // Every original caller initializes status to -100. Still classify status
    // outcomes only for ret >= 0, where move_pages documents them as valid.
    for (size_t i = 0; i < status.size(); ++i) {
        const bool wrong_index = pages != nullptr &&
            reinterpret_cast<void *>((*pages)[i]->va) != vas[i];
        if (wrong_index) {
            ++local[wrong_index_opportunity_slots];
            if (!early_enomem)
                ++local[wrong_index_loop_reached_slots];
        }
        if (ret < 0)
            continue;
        ++local[valid_status_entries];
        const int s = status[i];
        if (s == target_node) {
            ++local[status_target];
            if (ret > 0)
                ++local[positive_success_statuses];
            if (ret > 0 && result_errno == ENOMEM)
                ++local[positive_errno_enomem_success_statuses];
            if (early_enomem)
                ++local[early_enomem_valid_success_statuses];
            if (wrong_index && !early_enomem)
                ++local[wrong_index_valid_success_slots];
        } else if (s >= 0)
            ++local[status_other_node];
        else if (s == -ENOMEM)
            ++local[status_enomem];
        else if (s == -EBUSY)
            ++local[status_ebusy];
        else if (s == -ENOENT)
            ++local[status_enoent];
        else if (s == -EFAULT)
            ++local[status_efault];
        else if (s == -100)
            ++local[status_untouched_sentinel];
        else
            ++local[status_other_negative];
    }
    for (unsigned i = 0; i < probe_counter_count; ++i)
        probe_add(lane, static_cast<ProbeCounter>(i), local[i]);
    probe_add(lane, completed_calls, 1);
    lane.values[inflight_calls].fetch_sub(1, std::memory_order_relaxed);
    errno = result_errno;
    return ret;
}
} // namespace

__attribute__((visibility("hidden"))) void migration_probe_report()
{
    const int saved_errno = errno;
    if (probe_reported.exchange(true, std::memory_order_relaxed)) {
        errno = saved_errno;
        return;
    }
    // Snapshot before _exit, retaining the original no-join shutdown behavior.
    // Atomics make each field safe to read; fields are not a coherent snapshot.
    uint64_t snapshot[4][probe_counter_count];
    for (unsigned lane = 0; lane < 4; ++lane)
        for (unsigned i = 0; i < probe_counter_count; ++i)
            snapshot[lane][i] = probe_lanes[lane].values[i].load(std::memory_order_relaxed);

    static constexpr const char *lane_names[] = {
        "promotion_huge", "promotion_base", "demotion_huge", "demotion_base"};
    char output[16384];
    size_t used = static_cast<size_t>(snprintf(output, sizeof(output),
        "[MIGRATION_PROBE] {\"schema\":1,\"boundary\":\"before_original_exit_no_join\","
        "\"snapshot_coherent\":false,\"workers_may_be_inflight\":true,\"lanes\":{\n"));
    for (unsigned lane = 0; lane < 4 && used < sizeof(output); ++lane) {
        used += static_cast<size_t>(snprintf(output + used, sizeof(output) - used,
                                           "%s\"%s\":{", lane ? ",\n" : "", lane_names[lane]));
        for (unsigned i = 0; i < probe_counter_count && used < sizeof(output); ++i)
            used += static_cast<size_t>(snprintf(output + used, sizeof(output) - used,
                "%s\"%s\":%llu", i ? "," : "", probe_counter_names[i],
                static_cast<unsigned long long>(snapshot[lane][i])));
        if (used < sizeof(output))
            used += static_cast<size_t>(snprintf(output + used, sizeof(output) - used, "}"));
    }
    if (used < sizeof(output))
        used += static_cast<size_t>(snprintf(output + used, sizeof(output) - used, "}}\n"));
    if (used < sizeof(output))
        (void)::write(STDERR_FILENO, output, used);
    else {
        static constexpr char error[] = "[MIGRATION_PROBE] report_buffer_overflow\n";
        (void)::write(STDERR_FILENO, error, sizeof(error) - 1);
    }
    errno = saved_errno;
}
// END isolated migration diagnostic.
