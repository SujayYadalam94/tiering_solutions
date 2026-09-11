#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <regex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "arms_kernel_threads.h"
#include "timer.h"

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

namespace
{
inline bool shutdown_requested()
{
    return terminated.load(std::memory_order_relaxed);
}

inline uint64_t parse_node_pages(const std::string &line, int node)
{
    const std::string token = "N" + std::to_string(node) + "=";
    const size_t token_pos = line.find(token);
    if (token_pos == std::string::npos)
    {
        return 0;
    }

    const char *value_begin = line.c_str() + token_pos + token.size();
    char *value_end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(value_begin, &value_end, 10);
    if (value_end == value_begin || errno != 0)
    {
        return 0;
    }

    return static_cast<uint64_t>(value);
}

inline bool parse_start_va(const std::string &line, uint64_t *va_out)
{
    if (va_out == nullptr)
    {
        return false;
    }

    const size_t separator = line.find(' ');
    if (separator == std::string::npos)
    {
        return false;
    }

    const std::string va_str = line.substr(0, separator);
    char *parse_end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(va_str.c_str(), &parse_end, 16);
    if (parse_end == va_str.c_str() || *parse_end != '\0' || errno != 0)
    {
        return false;
    }

    *va_out = static_cast<uint64_t>(parsed);
    return true;
}

struct stale_page_cleanup_result
{
    size_t removed_pages = 0;
    uint64_t total_hugepages_in_dram = 0;
    size_t tracked_pages = 0;
};

struct page_slice
{
    page_ptr page;
    size_t start_index;
};

static inline bool has_pending_virtual_accesses(const page_ptr &page)
{
    if (!VIRTUAL_FEATURES_ENABLED || page == nullptr)
    {
        return false;
    }

    return (page->virtual_accesses[READ] != 0) || (page->virtual_accesses[WRITE] != 0);
}

static void refresh_page_residency(uint64_t cur_scan, const std::vector<page_ptr> &pages_to_refresh)
{
    if (pages_to_refresh.empty() || shutdown_requested())
    {
        return;
    }

    std::vector<void *> addr_batch;
    addr_batch.reserve(PAGEMAP_BATCH_HUGEPAGES);

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

        int count = 0;
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
                count++;
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
        std::cout << "[ARMS] error found for " << count << " hugepages in batch of " << addr_batch.size() << " pages."
                  << std::endl;

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

        if (addr_batch.size() >= PAGEMAP_BATCH_HUGEPAGES)
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
            if (VIRTUAL_FEATURES_ENABLED)
            {
                detached_logging_pages[it->first] = it->second;
            }
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

static void process_batch(std::vector<void *> &addr_batch, std::vector<int> &status_batch,
                          std::vector<page_ptr> &to_add_near, std::vector<page_ptr> &to_add_far,
                          std::vector<uint64_t> &to_remove, int &total_dram, int &total_cxl, uint64_t cur_scan)
{
    long ret = numa_move_pages(0, static_cast<unsigned long>(addr_batch.size()), addr_batch.data(), nullptr,
                               status_batch.data(), 0);
    if (ret != 0)
    {
        perror("[ARMS] Warning: numa_move_pages batch failed");
        return;
    }

    int fast = 0;
    int slow = 0;
    int error = 0;

    for (int i = 0; i < status_batch.size(); i++)
    {
        int status = status_batch[i];
        uint64_t va = reinterpret_cast<uint64_t>(addr_batch[i]);

        if (status == FAST_TIER)
        {
            fast++;
            total_dram++;
            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;
                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_near.push_back(page);
            }
            page->in_dram = true;
            page->fragmented = false;
        }
        else if (status == SLOW_TIER)
        {
            slow++;
            total_cxl++;

            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;

                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_far.push_back(page);
            }
            page->in_dram = false;
            page->fragmented = false;
        }
        else if (status == -EFAULT || status == -ENOENT)
        {
            // Page not present or inaccessible, treat as not in DRAM
            auto page = get_tracked_page(va);
            if (page != nullptr)
            {
                if (page->found_in_pebs)
                {
                    page->found_in_pebs = false;
                    page->fragmented = true;
                }
                else if (!page->fragmented)
                {
                    bool keep_for_virtual_flush = false;
                    if (VIRTUAL_FEATURES_ENABLED)
                    {
                        std::shared_lock<std::shared_mutex> virtual_lock(virtual_features_lock);
                        keep_for_virtual_flush = has_pending_virtual_accesses(page);
                    }

                    if (!keep_for_virtual_flush)
                    {
                        to_remove.push_back(va);
                    }
                }
            }
        }
        else if (status < 0)
        {
            error++;
        }
    }
    addr_batch.clear();
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
    std::vector<page_ptr> pages_to_refresh;
    std::vector<uint64_t> new_pages_to_populate;
    std::vector<int> status_batch;
    status_batch.resize(PAGEMAP_BATCH_HUGEPAGES);

    int total_dram = 0;
    int total_cxl = 0;

    std::vector<page_ptr> to_add_near;
    std::vector<page_ptr> to_add_far;
    std::vector<uint64_t> to_remove;
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
        if (is_kernel_page(start_addr))
            continue;

        std::vector<void *> addr_batch;
        addr_batch.reserve(PAGEMAP_BATCH_HUGEPAGES);

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

            addr_batch.push_back(reinterpret_cast<void *>(va));
            if (addr_batch.size() >= PAGEMAP_BATCH_HUGEPAGES)
            {
                process_batch(addr_batch, status_batch, to_add_near, to_add_far, to_remove, total_dram, total_cxl,
                              cur_scan);
                addr_batch.clear();
            }
        }
        process_batch(addr_batch, status_batch, to_add_near, to_add_far, to_remove, total_dram, total_cxl, cur_scan);
    }

    for (const auto &page : to_add_near)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        pages_map.emplace(page->va, page);
    }
    for (const auto &page : to_add_far)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        pages_map.emplace(page->va, page);
    }
    for (const auto &va : to_remove)
    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);
        auto it = pages_map.find(va);
        if (it == pages_map.end())
        {
            continue;
        }

        bool can_remove = true;
        if (VIRTUAL_FEATURES_ENABLED)
        {
            std::shared_lock<std::shared_mutex> virtual_lock(virtual_features_lock);
            can_remove = !has_pending_virtual_accesses(it->second);
        }

        if (can_remove)
        {
            if (VIRTUAL_FEATURES_ENABLED)
            {
                detached_logging_pages[va] = it->second;
            }
            pages_map.erase(it);
        }
    }

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Full scan added " << to_add_near.size() << " hugepages in DRAM and " << to_add_far.size()
                  << " hugepages in CXL, and removed " << to_remove.size()
                  << " pages from tracking. Total DRAM hugepages: " << total_dram
                  << ", Total CXL hugepages: " << total_cxl << std::endl;
    }
}

} // namespace

void *pagemap_scan_thread_fn(void *arg)
{
    (void)arg;
    register_tiering_runtime_tid();
    residency_probe::begin();

    struct ptimer loop_timer;
    ptimer_init(&loop_timer, "Page Map Scan Loop");
    uint64_t iteration = 0;
    while (!shutdown_requested())
    {
        ptimer_start(&loop_timer);
        scan_process_pages_full();
        ptimer_stop(&loop_timer);
        residency_probe::maybe_sample(); // Outside migration-cost timers.
        if (ARMS_VERBOSE)
        {
            ptimer_print(&loop_timer);
        }

        double elapsed_us = loop_timer.elapsed_us;

        if (!shutdown_requested() && elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval - elapsed_us);
        }

        iteration++;
    }

    return nullptr;
}
