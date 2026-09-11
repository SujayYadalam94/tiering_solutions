// Isolated read-only placement diagnostic. Included only in the copied scanner.
#include <algorithm>
#include <array>
#include <climits>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>

namespace residency_probe
{
constexpr size_t candidates_max = 64;
constexpr size_t regions_max = 32;
constexpr size_t base_pages = 512;
constexpr uint64_t region_bytes = 2097152;
constexpr uint64_t base_bytes = 4096;
constexpr uint64_t soft_budget_ns = 250000000;
constexpr int unknown_status = INT_MIN;
static uint64_t started_ns = 0;
static unsigned next_round = 0; // Scanner-thread owned; no extra thread.

struct candidate
{
    uint64_t key = 0;
    uint64_t hash = 0;
    page_ptr page;
};

struct flags
{
    uint64_t va = UINT64_MAX;
    unsigned char near = 255;
    unsigned char fragmented = 255;
};

struct observation
{
    size_t candidate_index = 0;
    long region_ret = -1;
    int region_errno = 0;
    long head_before_ret = -1;
    long head_after_ret = -1;
    int head_before_errno = 0;
    int head_after_errno = 0;
    int head_before = unknown_status;
    int head_region = unknown_status;
    int head_after = unknown_status;
    size_t near = 0, far = 0, other_node = 0, enoent = 0, efault = 0, other_error = 0;
    uint64_t elapsed_ns = 0;
    bool complete = false;
};

static uint64_t now_ns()
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
        return 0;
    return static_cast<uint64_t>(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}

static uint64_t hash_key(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static bool less_candidate(const candidate &a, const candidate &b)
{
    return a.hash < b.hash || (a.hash == b.hash && a.key < b.key);
}

// No direct reads of the mutable bool fields: the original writers do not
// consistently take page_lock/pages_map_lock. A self kernel copy avoids adding
// C++ reader races, but does NOT make concurrent field values coherent.
static long read_flags(const std::array<candidate, candidates_max> &items, size_t count,
                       std::array<flags, candidates_max> &out, int *error)
{
    std::array<struct iovec, candidates_max * 3> local{};
    std::array<struct iovec, candidates_max * 3> remote{};
    for (size_t i = 0; i < count; ++i)
    {
        local[3 * i] = {&out[i].va, sizeof(out[i].va)};
        local[3 * i + 1] = {&out[i].near, 1};
        local[3 * i + 2] = {&out[i].fragmented, 1};
        remote[3 * i] = {&items[i].page->va, sizeof(out[i].va)};
        remote[3 * i + 1] = {&items[i].page->in_dram, 1};
        remote[3 * i + 2] = {&items[i].page->fragmented, 1};
    }
    errno = 0;
    const long ret = syscall(SYS_process_vm_readv, getpid(), local.data(), count * 3,
                             remote.data(), count * 3, 0UL);
    *error = ret < 0 ? errno : 0;
    return ret;
}

static long query(void **addresses, size_t count, int *statuses, int *error)
{
    std::fill(statuses, statuses + count, unknown_status);
    errno = 0;
    // nodes=nullptr is a residency query, with flags=0; never a migration.
    const long ret = numa_move_pages(0, count, addresses, nullptr, statuses, 0);
    *error = ret < 0 ? errno : 0;
    return ret;
}

static void emit(const char *buf, int count)
{
    if (count > 0)
        (void)write(STDERR_FILENO, buf, static_cast<size_t>(count));
}

static void sample(unsigned round, uint64_t due_ns)
{
    const uint64_t begin = now_ns();
    std::array<candidate, candidates_max> items{};
    size_t count = 0, map_size = 0, null_entries = 0, unaligned_keys = 0;
    bool map_locked = false;
    {
        std::shared_lock<std::shared_mutex> lock(pages_map_lock, std::try_to_lock);
        map_locked = lock.owns_lock();
        if (map_locked)
        {
            map_size = pages_map.size();
            size_t largest = 0;
            for (const auto &kv : pages_map)
            {
                if ((kv.first & HUGE_PFN_MASK) != kv.first)
                    ++unaligned_keys;
                if (!kv.second)
                {
                    ++null_entries;
                    continue;
                }
                const uint64_t h = hash_key(kv.first);
                if (count == candidates_max &&
                    (h > items[largest].hash || (h == items[largest].hash && kv.first >= items[largest].key)))
                    continue;
                size_t slot = count < candidates_max ? count++ : largest;
                items[slot] = {kv.first, h, kv.second}; // Pin canonical map entry.
                largest = 0;
                for (size_t i = 1; i < count; ++i)
                    if (less_candidate(items[largest], items[i]))
                        largest = i;
            }
        }
    } // Release map lock before any kernel query or output.
    const uint64_t copy_end = now_ns();
    std::sort(items.begin(), items.begin() + count, less_candidate);

    std::array<flags, candidates_max> before{}, after{};
    int before_error = 0, after_error = 0;
    long before_ret = -1, after_ret = -1;
    size_t metadata_calls = 0;
    bool budget_hit = copy_end - begin >= soft_budget_ns;
    if (count && !budget_hit)
    {
        before_ret = read_flags(items, count, before, &before_error);
        ++metadata_calls;
    }
    const bool before_valid = count && before_ret == static_cast<long>(count * 10);
    size_t candidates_near = 0, candidates_far = 0, candidate_key_va_mismatch = 0;
    std::array<size_t, regions_max> selected{};
    std::array<bool, candidates_max> chosen{};
    size_t selected_count = 0;
    if (before_valid)
    {
        for (size_t i = 0; i < count; ++i)
        {
            candidates_near += before[i].near == 1;
            candidates_far += before[i].near == 0;
            candidate_key_va_mismatch += items[i].key != before[i].va;
        }
        // Balance within the 64 deterministic candidates, without scanning all
        // mutable flags in a potentially much larger map.
        for (unsigned near = 0; near <= 1; ++near)
        {
            size_t used = 0;
            for (size_t i = 0; i < count && used < regions_max / 2; ++i)
                if (before[i].near == near && before[i].fragmented <= 1)
                {
                    selected[selected_count++] = i;
                    chosen[i] = true;
                    ++used;
                }
        }
        for (size_t i = 0; i < count && selected_count < regions_max; ++i)
            if (!chosen[i] && before[i].near <= 1 && before[i].fragmented <= 1)
                selected[selected_count++] = i;
    }

    std::array<observation, regions_max> observations{};
    size_t queried = 0, query_calls = 0;
    uint64_t query_elapsed_ns = 0;
    for (size_t n = 0; n < selected_count; ++n)
    {
        if (terminated.load(std::memory_order_relaxed) || now_ns() - begin >= soft_budget_ns)
        {
            budget_hit = now_ns() - begin >= soft_budget_ns;
            break;
        }
        auto &o = observations[n];
        o.candidate_index = selected[n];
        // Use map key rather than an unsynchronized direct read of page->va.
        const uint64_t key = items[o.candidate_index].key;
        std::array<void *, base_pages> addresses{};
        std::array<int, base_pages> statuses{};
        for (size_t k = 0; k < base_pages; ++k)
            addresses[k] = reinterpret_cast<void *>(key + k * base_bytes);
        const uint64_t qstart = now_ns();
        o.head_before_ret = query(addresses.data(), 1, &o.head_before, &o.head_before_errno);
        ++query_calls;
        o.region_ret = query(addresses.data(), base_pages, statuses.data(), &o.region_errno);
        ++query_calls;
        o.head_after_ret = query(addresses.data(), 1, &o.head_after, &o.head_after_errno);
        ++query_calls;
        o.elapsed_ns = now_ns() - qstart;
        query_elapsed_ns += o.elapsed_ns;
        o.complete = o.region_ret == 0;
        if (o.complete)
        {
            o.head_region = statuses[0];
            for (int status : statuses)
            {
                if (status == FAST_TIER) ++o.near;
                else if (status == SLOW_TIER) ++o.far;
                else if (status >= 0) ++o.other_node;
                else if (status == -ENOENT) ++o.enoent;
                else if (status == -EFAULT) ++o.efault;
                else ++o.other_error;
            }
        }
        ++queried;
    }
    if (before_valid)
    {
        after_ret = read_flags(items, count, after, &after_error);
        ++metadata_calls;
    }
    const bool after_valid = count && after_ret == static_cast<long>(count * 10);
    const uint64_t measure_end = now_ns();
    char line[2048];
    for (size_t n = 0; n < queried; ++n)
    {
        const auto &o = observations[n];
        const size_t i = o.candidate_index;
        const bool heads_valid = o.head_before_ret == 0 && o.head_after_ret == 0 && o.complete;
        const bool unstable_head = heads_valid &&
            (o.head_before != o.head_region || o.head_region != o.head_after);
        const bool changed = after_valid && (before[i].near != after[i].near ||
            before[i].fragmented != after[i].fragmented || before[i].va != after[i].va);
        const uint64_t hidden = o.complete && before[i].near == 0 ? o.near * base_bytes : 0;
        const uint64_t excess = o.complete && before[i].near == 1 ? region_bytes - o.near * base_bytes : 0;
        const int len = snprintf(line, sizeof(line),
            "[RESIDENCY_PROBE] {\"kind\":\"region\",\"schema\":1,\"round\":%u,\"index\":%zu,"
            "\"key\":\"0x%llx\",\"page_va_before\":\"0x%llx\",\"key_aligned\":%s,"
            "\"tracked_near_before\":%u,\"fragmented_before\":%u,\"flags_after_valid\":%s,"
            "\"tracked_near_after\":%u,\"fragmented_after\":%u,\"metadata_changed\":%s,"
            "\"query_ret\":%ld,\"query_errno\":%d,\"head_before_ret\":%ld,\"head_before_errno\":%d,"
            "\"head_after_ret\":%ld,\"head_after_errno\":%d,\"head_before\":%d,\"head_region\":%d,"
            "\"head_after\":%d,\"heads_valid\":%s,\"unstable_head\":%s,\"near_pages\":%zu,"
            "\"far_pages\":%zu,\"other_node_pages\":%zu,\"enoent_pages\":%zu,\"efault_pages\":%zu,"
            "\"other_error_pages\":%zu,\"near_hidden_bytes\":%llu,\"near_credit_excess_bytes\":%llu,"
            "\"query_elapsed_ns\":%llu}\n",
            round, n, (unsigned long long)items[i].key, (unsigned long long)before[i].va,
            (items[i].key & HUGE_PFN_MASK) == items[i].key ? "true" : "false",
            (unsigned)before[i].near, (unsigned)before[i].fragmented, after_valid ? "true" : "false",
            (unsigned)after[i].near, (unsigned)after[i].fragmented, changed ? "true" : "false",
            o.region_ret, o.region_errno, o.head_before_ret, o.head_before_errno,
            o.head_after_ret, o.head_after_errno, o.head_before, o.head_region, o.head_after,
            heads_valid ? "true" : "false", unstable_head ? "true" : "false",
            o.near, o.far, o.other_node, o.enoent, o.efault, o.other_error,
            (unsigned long long)hidden, (unsigned long long)excess, (unsigned long long)o.elapsed_ns);
        if (len > 0 && static_cast<size_t>(len) < sizeof(line)) emit(line, len);
    }
    const uint64_t end = now_ns();
    const int len = snprintf(line, sizeof(line),
        "[RESIDENCY_PROBE] {\"kind\":\"summary\",\"schema\":1,\"round\":%u,\"due_s\":%llu,"
        "\"runtime_s\":%.6f,\"map_lock_acquired\":%s,\"map_size\":%zu,\"map_null_entries\":%zu,"
        "\"map_unaligned_keys\":%zu,\"candidates\":%zu,\"candidates_near\":%zu,\"candidates_far\":%zu,"
        "\"candidate_key_va_mismatch\":%zu,\"selected\":%zu,\"queried\":%zu,\"query_calls\":%zu,"
        "\"metadata_calls\":%zu,\"metadata_before_ret\":%ld,\"metadata_before_errno\":%d,"
        "\"metadata_after_ret\":%ld,\"metadata_after_errno\":%d,\"map_copy_ns\":%llu,"
        "\"query_elapsed_ns\":%llu,\"measure_elapsed_ns\":%llu,\"elapsed_before_summary_write_ns\":%llu,"
        "\"soft_budget_ns\":%llu,\"budget_hit\":%s,\"selection\":\"hash64_then_balance32\"}\n",
        round, (unsigned long long)(due_ns / 1000000000ULL), (begin - started_ns) / 1e9,
        map_locked ? "true" : "false", map_size, null_entries, unaligned_keys, count,
        candidates_near, candidates_far, candidate_key_va_mismatch, selected_count, queried,
        query_calls, metadata_calls, before_ret, before_error, after_ret, after_error,
        (unsigned long long)(copy_end - begin), (unsigned long long)query_elapsed_ns,
        (unsigned long long)(measure_end - begin), (unsigned long long)(end - begin),
        (unsigned long long)soft_budget_ns,
        (budget_hit || measure_end - begin >= soft_budget_ns) ? "true" : "false");
    if (len > 0 && static_cast<size_t>(len) < sizeof(line)) emit(line, len);
}

static void begin()
{
    const int saved_errno = errno;
    started_ns = now_ns();
    errno = saved_errno;
}

static void maybe_sample()
{
    if (next_round >= 2 || !started_ns)
        return;
    const int saved_errno = errno;
    const uint64_t due_ns = next_round == 0 ? 50000000000ULL : 75000000000ULL;
    if (now_ns() - started_ns >= due_ns)
    {
        ++next_round; // At most two attempts, even if locked/denied/budget-limited.
        sample(next_round, due_ns);
    }
    errno = saved_errno;
}
} // namespace residency_probe
