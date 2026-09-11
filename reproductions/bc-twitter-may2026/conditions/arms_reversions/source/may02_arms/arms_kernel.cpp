/*
 * ARMS Kernel-based Memory Tiering System
 *
 * This is a port of ARMS to use kernel-based page management instead of
 * userspace devdax + userfaultfd. The PEBS-based policy and scoring logic
 * from ARMS is retained, but page management and migration now relies on
 * the kernel via numa_move_pages() system calls.
 *
 */

#include <algorithm>
#include <asm/unistd.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <math.h>
#include <memory>
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <regex>
#include <sched.h>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "arms_kernel.h"
#include "arms_kernel_threads.h"
#include "defs.h"
#include "groups.h"
#include "logging.h"
#include "model.h"
#include "page.h"
#include "timer.h"
#include <atomic>

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef FAST_MEMORY_SIZE_GB
uint64_t FAST_MEMORY_SIZE = (FAST_MEMORY_SIZE_GB * (1024L * 1024L * 1024L)); // size of fast tier memory in bytes
#else
uint64_t FAST_MEMORY_SIZE = 0;
#endif

// Global state
uint64_t dramsize = 0;

bool initialized = false;

std::atomic<uint64_t> scan_generation{0};

std::unordered_map<uint64_t, page_ptr> pages_map;
std::unordered_map<uint64_t, page_ptr> detached_logging_pages;
std::shared_mutex pages_map_lock;

namespace
{
std::vector<ip_range> preload_ip_ranges;
std::string preload_library_path;
std::shared_mutex preload_ip_ranges_lock;
std::atomic<uint64_t> preload_library_filtered_samples{0};
std::vector<ip_range> helper_library_ip_ranges;
std::shared_mutex helper_library_ip_ranges_lock;
std::atomic<uint64_t> helper_library_filtered_samples{0};
std::atomic<uint64_t> tiering_runtime_tid_filtered_samples{0};
std::atomic<uint64_t> other_pid_filtered_samples{0};

static bool ip_in_sorted_ranges(const std::vector<ip_range> &ranges, uint64_t ip)
{
    if (ip == 0 || ranges.empty())
    {
        return false;
    }

    auto it = std::upper_bound(ranges.begin(), ranges.end(), ip,
                               [](uint64_t value, const ip_range &range) { return value < range.start; });
    if (it == ranges.begin())
    {
        return false;
    }

    --it;
    return ip >= it->start && ip < it->end;
}

static uint64_t parse_virtual_step_samples_env()
{
    const char *raw_value = std::getenv("VIRTUAL_STEP_SAMPLES");
    if (raw_value == nullptr || raw_value[0] == '\0')
    {
        return static_cast<uint64_t>(VIRTUAL_STEP_SAMPLES);
    }

    std::string normalized;
    normalized.reserve(std::strlen(raw_value));
    for (const char *cursor = raw_value; *cursor != '\0'; ++cursor)
    {
        if (*cursor != '_')
        {
            normalized.push_back(*cursor);
        }
    }

    if (normalized.empty())
    {
        std::cerr << "[ARMS] Warning: empty VIRTUAL_STEP_SAMPLES override; using default " << VIRTUAL_STEP_SAMPLES
                  << std::endl;
        return static_cast<uint64_t>(VIRTUAL_STEP_SAMPLES);
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(normalized.c_str(), &end, 10);
    if (errno != 0 || end == normalized.c_str() || *end != '\0' || parsed == 0)
    {
        std::cerr << "[ARMS] Warning: invalid VIRTUAL_STEP_SAMPLES='" << raw_value << "'; using default "
                  << VIRTUAL_STEP_SAMPLES << std::endl;
        return static_cast<uint64_t>(VIRTUAL_STEP_SAMPLES);
    }

    return static_cast<uint64_t>(parsed);
}
} // namespace

uint64_t get_virtual_step_samples()
{
    static const uint64_t virtual_step_samples = parse_virtual_step_samples_env();
    return virtual_step_samples;
}

void set_preload_ip_ranges(const char *library_path, const struct ip_range *ranges, size_t range_count)
{
    std::vector<ip_range> new_ranges;
    new_ranges.reserve(range_count);

    for (size_t i = 0; i < range_count; ++i)
    {
        if (ranges[i].end > ranges[i].start)
        {
            new_ranges.push_back(ranges[i]);
        }
    }

    std::sort(new_ranges.begin(), new_ranges.end(),
              [](const ip_range &lhs, const ip_range &rhs) { return lhs.start < rhs.start; });

    {
        std::unique_lock<std::shared_mutex> lock(preload_ip_ranges_lock);
        preload_library_path = (library_path != nullptr) ? library_path : "";
        preload_ip_ranges = std::move(new_ranges);
    }

    preload_library_filtered_samples.store(0, std::memory_order_relaxed);
    helper_library_filtered_samples.store(0, std::memory_order_relaxed);
    tiering_runtime_tid_filtered_samples.store(0, std::memory_order_relaxed);
    other_pid_filtered_samples.store(0, std::memory_order_relaxed);
}

void set_helper_library_ip_ranges(const struct ip_range *ranges, size_t range_count)
{
    std::vector<ip_range> new_ranges;
    new_ranges.reserve(range_count);

    for (size_t i = 0; i < range_count; ++i)
    {
        if (ranges[i].end > ranges[i].start)
        {
            new_ranges.push_back(ranges[i]);
        }
    }

    std::sort(new_ranges.begin(), new_ranges.end(),
              [](const ip_range &lhs, const ip_range &rhs) { return lhs.start < rhs.start; });

    {
        std::unique_lock<std::shared_mutex> lock(helper_library_ip_ranges_lock);
        helper_library_ip_ranges = std::move(new_ranges);
    }

    helper_library_filtered_samples.store(0, std::memory_order_relaxed);
}

bool is_preload_library_ip(uint64_t ip)
{
    std::shared_lock<std::shared_mutex> lock(preload_ip_ranges_lock);
    return ip_in_sorted_ranges(preload_ip_ranges, ip);
}

void note_preload_library_sample_filtered()
{
    preload_library_filtered_samples.fetch_add(1, std::memory_order_relaxed);
}

uint64_t take_preload_library_filtered_samples()
{
    return preload_library_filtered_samples.exchange(0, std::memory_order_relaxed);
}

bool is_helper_library_ip(uint64_t ip)
{
    std::shared_lock<std::shared_mutex> lock(helper_library_ip_ranges_lock);
    return ip_in_sorted_ranges(helper_library_ip_ranges, ip);
}

void note_helper_library_sample_filtered()
{
    helper_library_filtered_samples.fetch_add(1, std::memory_order_relaxed);
}

uint64_t take_helper_library_filtered_samples()
{
    return helper_library_filtered_samples.exchange(0, std::memory_order_relaxed);
}

void note_tiering_runtime_tid_sample_filtered()
{
    tiering_runtime_tid_filtered_samples.fetch_add(1, std::memory_order_relaxed);
}

uint64_t take_tiering_runtime_tid_filtered_samples()
{
    return tiering_runtime_tid_filtered_samples.exchange(0, std::memory_order_relaxed);
}

void note_other_pid_sample_filtered()
{
    other_pid_filtered_samples.fetch_add(1, std::memory_order_relaxed);
}

uint64_t take_other_pid_filtered_samples()
{
    return other_pid_filtered_samples.exchange(0, std::memory_order_relaxed);
}

void set_application_thread_far_memory_default()
{
    if (numa_available() < 0)
    {
        std::cerr << "[ARMS] NUMA not available; cannot apply thread memory policy." << std::endl;
        return;
    }

    struct bitmask *slow_tier_nodemask = numa_allocate_nodemask();
    if (slow_tier_nodemask == nullptr)
    {
        perror("[ARMS] numa_allocate_nodemask failed");
        return;
    }

    numa_bitmask_clearall(slow_tier_nodemask);
    numa_bitmask_setbit(slow_tier_nodemask, SLOW_TIER);
    numa_set_membind(slow_tier_nodemask);
    numa_bitmask_free(slow_tier_nodemask);

    std::cout << "[ARMS] Set application thread to prefer far memory by default." << std::endl;
}

void set_application_thread_near_memory_default()
{
    if (numa_available() < 0)
    {
        std::cerr << "[ARMS] NUMA not available; cannot apply thread memory policy." << std::endl;
        return;
    }

    struct bitmask *fast_tier_nodemask = numa_allocate_nodemask();
    if (fast_tier_nodemask == nullptr)
    {
        perror("[ARMS] numa_allocate_nodemask failed");
        return;
    }

    numa_bitmask_clearall(fast_tier_nodemask);
    numa_bitmask_setbit(fast_tier_nodemask, FAST_TIER);
    numa_set_membind(fast_tier_nodemask);
    numa_bitmask_free(fast_tier_nodemask);

    std::cout << "[ARMS] Set application thread to prefer near memory by default." << std::endl;
}

void set_application_thread_near_memory_preferred()
{
    if (numa_available() < 0)
    {
        std::cerr << "[ARMS] NUMA not available; cannot apply thread memory policy." << std::endl;
        return;
    }

    // Prefer near memory for new allocations but allow fallback when near tier
    // is full so we still allocate as much as possible in near memory.
    numa_set_preferred(FAST_TIER);

    std::cout << "[ARMS] Set application thread to prefer near memory preferred." << std::endl;
}

bool is_access_log_page(uint64_t page_base)
{
    return access_log != nullptr && access_log->overlaps_with_logging_region(page_base, PAGE_SIZE);
}

bool is_kernel_page(uint64_t page_base)
{
    return page_base >= 0x7fffffffffff;
}

page_ptr get_tracked_page(uint64_t page_va)
{
    std::shared_lock<std::shared_mutex> lock(pages_map_lock);
    auto it = pages_map.find(page_va & HUGE_PFN_MASK);
    if (it != pages_map.end())
    {
        return it->second;
    }
    return nullptr;
}

page_ptr get_or_create_tracked_page(uint64_t page_va, uint64_t last_seen_scan, uint64_t last_access_generation,
                                    bool in_dram, bool *added_new_page)
{
    const uint64_t page_key = page_va & HUGE_PFN_MASK;

    page_ptr page = get_tracked_page(page_key);
    if (page != nullptr)
    {
        return page;
    }

    page = std::make_shared<page_info>();
    page->va = page_key;
    page->last_seen_scan = last_seen_scan;
    page->last_access_generation = last_access_generation;
    page->found_in_pebs = true;
    page->in_dram = in_dram;

    // void *page_addrs[2] = {(void *)((uintptr_t)page_va & HUGE_PFN_MASK), (void *)((uintptr_t)page_va &
    // BASE_PFN_MASK)}; int status[2]; numa_move_pages(0, 2, page_addrs, nullptr, status, 0); std::cout << "[ARMS]
    // Tracking new page " << status[0] << " " << status[1] << std::endl;

    {
        std::unique_lock<std::shared_mutex> lock(pages_map_lock);

        // If this page was temporarily detached for virtual-step retention,
        // revive it into the active map when it is sampled again.
        auto detached_it = detached_logging_pages.find(page_key);
        if (detached_it != detached_logging_pages.end())
        {
            page_ptr revived = detached_it->second;
            detached_logging_pages.erase(detached_it);
            revived->last_seen_scan = last_seen_scan;
            revived->last_access_generation = last_access_generation;
            revived->found_in_pebs = true;
            revived->in_dram = in_dram;
            pages_map[page_key] = revived;
            if (added_new_page != nullptr)
            {
                *added_new_page = false;
            }
            return revived;
        }

        auto [it, inserted] = pages_map.emplace(page_key, page);
        if (inserted)
        {
            if (added_new_page != nullptr)
            {
                *added_new_page = true;
            }
            return page;
        }

        // Another thread inserted this page concurrently; always use canonical entry.
        if (added_new_page != nullptr)
        {
            *added_new_page = false;
        }
        return it->second;
    }
}

int pagemap_fd = -1;
pid_t target_pid = 0;

int perf_fd[PEBS_NPROCS][NPBUFTYPES];
struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];

pthread_t scan_thread;
pthread_t pagemap_scan_thread;
pthread_t policy_thread;
pthread_t migration_threads[MIGRATION_WORKER_COUNT];
int32_t policy_thread_interval = PEBS_KSWAPD_INTERVAL_BIG;

volatile uint64_t global_version = 0;
volatile uint8_t curr_access_version = 0;
volatile uint8_t prev_access_version = 1;
volatile uint8_t curr_window_index = 0;

const float *active_bias = hist_bias; // Current bias (hist or recency)
enum sampling_modes sampling_mode = DEFAULT_SAMPLING;

std::atomic<uint64_t> migrations_up{0};
std::atomic<uint64_t> migrations_down{0};
std::atomic<uint64_t> migrations_up_period{0};
std::atomic<uint64_t> migrations_down_period{0};
uint64_t total_samples[NPBUFTYPES] = {0};
std::atomic<uint64_t> max_dram_hugepages_seen{0};
std::atomic<uint64_t> total_dram_hugepages_accum{0};
std::atomic<uint64_t> dram_samples{0};
std::atomic<uint64_t> virtual_sample_total{0};
std::atomic<uint64_t> virtual_step{0};
std::shared_mutex virtual_features_lock;
std::atomic<bool> shutdown_started{false};
static constexpr const char *MAX_DRAM_HUGEPAGE_LOG = "max_dram_hugepages.log";
static constexpr const char *OFFCORE_WRITE_L3_METRICS_LOG = "offcore_write_l3_metrics.log";

// Optional verbose-only counters used to estimate demand-RFO L3 write hit-rate.
// They do not affect page sampling or migration policy.
int offcore_rfo_any_response_fd[PEBS_NPROCS];
int offcore_rfo_l3_miss_fd[PEBS_NPROCS];
bool offcore_write_metrics_enabled = false;
uint64_t previous_offcore_rfo_any_response = 0;
uint64_t previous_offcore_rfo_l3_miss = 0;

float dram_bw_ewma = 0.0;
float nvm_bw_ewma = 0.0;
float nvm_bw_std = 0.0;
float cusum = 0.0;
uint32_t time_since_recn = 0;

float promotion_cost_avg = MIN_PROMOTION_COST;
float demotion_cost_avg = MIN_DEMOTION_COST;
float latency_diff = UNLOADED_NVM_LAT - UNLOADED_DRAM_LAT;

std::atomic<bool> terminated{false};
struct group_tracker *grp_tracker = NULL;
struct group_tracker *virtual_grp_tracker = NULL;

// ============================================================================
// PERF Event Setup
// ============================================================================

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    int ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

// OFFCORE_RESPONSE EventSel={B7H,BBH}, UMask=01H.
// We use B7H by default (config=0x1b7), and put response filters in config1.
#define OFFCORE_RESPONSE_EVENT_CONFIG 0x01b7
#define OFFCORE_DEMAND_RFO_ANY_RESPONSE_MASK 0x0000000000010002ULL
#define OFFCORE_DEMAND_RFO_L3_MISS_ANY_SNOOP_MASK 0x0000003FBC000002ULL

static int open_raw_counting_event(__u64 config, __u64 config1, int cpu, const char *event_name)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));

    pe.type = PERF_TYPE_RAW;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.config1 = config1;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    int fd = perf_event_open(&pe, -1, cpu, -1, 0);
    if (fd == -1 && ARMS_VERBOSE)
    {
        fprintf(stderr, "[ARMS] Failed to open %s on CPU %d (config=0x%llx, config1=0x%llx): %s\n", event_name, cpu,
                static_cast<unsigned long long>(config), static_cast<unsigned long long>(config1), strerror(errno));
    }

    return fd;
}

static void setup_offcore_write_l3_metrics_counters()
{
    for (int i = 0; i < PEBS_NPROCS; i++)
    {
        offcore_rfo_any_response_fd[i] = -1;
        offcore_rfo_l3_miss_fd[i] = -1;
    }

    offcore_write_metrics_enabled = false;
    previous_offcore_rfo_any_response = 0;
    previous_offcore_rfo_l3_miss = 0;

    if (!ARMS_VERBOSE)
    {
        return;
    }

    std::cout << "[ARMS] Setting up OFFCORE DEMAND_RFO counters for L3 write hit-rate metrics..." << std::endl;

    int counters_opened = 0;
    for (int cpu = 0; cpu < PEBS_NPROCS; cpu++)
    {
#ifdef C220G5
        if (cpu >= 10 && cpu < 20)
            continue;
#endif

#ifdef GSL_OPTANE
        if (cpu >= 16 && cpu < 32)
            continue;
#endif

        int any_fd = open_raw_counting_event(OFFCORE_RESPONSE_EVENT_CONFIG, OFFCORE_DEMAND_RFO_ANY_RESPONSE_MASK, cpu,
                                             "OFFCORE_RESPONSE:DEMAND_RFO:ANY_RESPONSE");
        int l3_miss_fd =
            open_raw_counting_event(OFFCORE_RESPONSE_EVENT_CONFIG, OFFCORE_DEMAND_RFO_L3_MISS_ANY_SNOOP_MASK, cpu,
                                    "OFFCORE_RESPONSE:DEMAND_RFO:L3_MISS.ANY_SNOOP");

        if (any_fd == -1 || l3_miss_fd == -1)
        {
            if (any_fd >= 0)
            {
                close(any_fd);
            }
            if (l3_miss_fd >= 0)
            {
                close(l3_miss_fd);
            }
            continue;
        }

        offcore_rfo_any_response_fd[cpu] = any_fd;
        offcore_rfo_l3_miss_fd[cpu] = l3_miss_fd;

        ioctl(any_fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(l3_miss_fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(any_fd, PERF_EVENT_IOC_ENABLE, 0);
        ioctl(l3_miss_fd, PERF_EVENT_IOC_ENABLE, 0);

        counters_opened++;
    }

    if (counters_opened > 0)
    {
        offcore_write_metrics_enabled = true;
        std::cout << "[ARMS] OFFCORE DEMAND_RFO counters enabled on " << counters_opened << " CPUs." << std::endl;
    }
    else
    {
        std::cerr << "[ARMS] OFFCORE DEMAND_RFO counters unavailable; skipping write L3 hit-rate logging." << std::endl;
    }
}

static uint64_t read_counter_sum(const int fds[PEBS_NPROCS], const char *counter_name)
{
    uint64_t total = 0;
    for (int cpu = 0; cpu < PEBS_NPROCS; cpu++)
    {
        if (fds[cpu] < 0)
        {
            continue;
        }

        uint64_t value = 0;
        ssize_t bytes = read(fds[cpu], &value, sizeof(value));
        if (bytes != static_cast<ssize_t>(sizeof(value)))
        {
            if (ARMS_VERBOSE)
            {
                fprintf(stderr, "[ARMS] Failed to read %s on CPU %d: %s\n", counter_name, cpu, strerror(errno));
            }
            continue;
        }

        total += value;
    }
    return total;
}

static uint64_t clamp_offcore_miss_count(uint64_t any, uint64_t miss)
{
    return (miss > any) ? any : miss;
}

bool get_offcore_write_l3_hit_rate(double *hit_rate, uint64_t *demand_rfo_any, uint64_t *demand_rfo_l3_miss)
{
    if (!offcore_write_metrics_enabled)
    {
        return false;
    }

    const uint64_t any = read_counter_sum(offcore_rfo_any_response_fd, "OFFCORE_RESPONSE:DEMAND_RFO:ANY_RESPONSE");
    const uint64_t miss = read_counter_sum(offcore_rfo_l3_miss_fd, "OFFCORE_RESPONSE:DEMAND_RFO:L3_MISS.ANY_SNOOP");

    if (demand_rfo_any != nullptr)
    {
        *demand_rfo_any = any;
    }

    if (demand_rfo_l3_miss != nullptr)
    {
        *demand_rfo_l3_miss = miss;
    }

    if (hit_rate != nullptr)
    {
        if (any == 0)
        {
            *hit_rate = 0.0;
        }
        else
        {
            const uint64_t clamped_miss = (miss > any) ? any : miss;
            *hit_rate = 1.0 - (static_cast<double>(clamped_miss) / static_cast<double>(any));
        }
    }

    return true;
}

struct offcore_write_l3_step_metrics
{
    bool available = false;
    double step_miss_rate = 0.0;
};

// OFFCORE counters are cumulative from setup time, so per-step rates are derived
// from deltas between successive policy-step snapshots.
static offcore_write_l3_step_metrics snapshot_offcore_write_l3_step_metrics()
{
    offcore_write_l3_step_metrics metrics;

    uint64_t cumulative_any = 0;
    uint64_t cumulative_miss = 0;
    if (!get_offcore_write_l3_hit_rate(nullptr, &cumulative_any, &cumulative_miss))
    {
        return metrics;
    }

    const uint64_t step_any = (cumulative_any >= previous_offcore_rfo_any_response)
                                  ? (cumulative_any - previous_offcore_rfo_any_response)
                                  : cumulative_any;
    const uint64_t step_miss_raw = (cumulative_miss >= previous_offcore_rfo_l3_miss)
                                       ? (cumulative_miss - previous_offcore_rfo_l3_miss)
                                       : cumulative_miss;
    const uint64_t step_miss = clamp_offcore_miss_count(step_any, step_miss_raw);

    previous_offcore_rfo_any_response = cumulative_any;
    previous_offcore_rfo_l3_miss = cumulative_miss;

    metrics.available = true;
    if (step_any > 0)
    {
        metrics.step_miss_rate = static_cast<double>(step_miss) / static_cast<double>(step_any);
    }

    return metrics;
}

static void close_offcore_write_l3_metrics_counters()
{
    for (int cpu = 0; cpu < PEBS_NPROCS; cpu++)
    {
        if (offcore_rfo_any_response_fd[cpu] >= 0)
        {
            close(offcore_rfo_any_response_fd[cpu]);
            offcore_rfo_any_response_fd[cpu] = -1;
        }

        if (offcore_rfo_l3_miss_fd[cpu] >= 0)
        {
            close(offcore_rfo_l3_miss_fd[cpu]);
            offcore_rfo_l3_miss_fd[cpu] = -1;
        }
    }

    offcore_write_metrics_enabled = false;
    previous_offcore_rfo_any_response = 0;
    previous_offcore_rfo_l3_miss = 0;
}

static void write_offcore_write_l3_metrics_to_file()
{
    if (!offcore_write_metrics_enabled)
    {
        return;
    }

    for (int cpu = 0; cpu < PEBS_NPROCS; cpu++)
    {
        if (offcore_rfo_any_response_fd[cpu] >= 0)
        {
            ioctl(offcore_rfo_any_response_fd[cpu], PERF_EVENT_IOC_DISABLE, 0);
        }
        if (offcore_rfo_l3_miss_fd[cpu] >= 0)
        {
            ioctl(offcore_rfo_l3_miss_fd[cpu], PERF_EVENT_IOC_DISABLE, 0);
        }
    }

    const uint64_t demand_rfo_any =
        read_counter_sum(offcore_rfo_any_response_fd, "OFFCORE_RESPONSE:DEMAND_RFO:ANY_RESPONSE");
    const uint64_t demand_rfo_l3_miss =
        read_counter_sum(offcore_rfo_l3_miss_fd, "OFFCORE_RESPONSE:DEMAND_RFO:L3_MISS.ANY_SNOOP");
    const uint64_t clamped_demand_rfo_l3_miss = clamp_offcore_miss_count(demand_rfo_any, demand_rfo_l3_miss);

    const uint64_t demand_rfo_l3_hit = demand_rfo_any - clamped_demand_rfo_l3_miss;

    const double miss_rate =
        (demand_rfo_any > 0) ? (static_cast<double>(clamped_demand_rfo_l3_miss) / static_cast<double>(demand_rfo_any))
                             : 0.0;
    const double hit_rate = (demand_rfo_any > 0) ? (1.0 - miss_rate) : 0.0;

    int fd = open(OFFCORE_WRITE_L3_METRICS_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("[ARMS] Failed to open offcore_write_l3_metrics.log");
        return;
    }

    if (dprintf(fd, "event=OFFCORE_RESPONSE:request=DEMAND_RFO:response=L3_MISS.ANY_SNOOP\n") < 0 ||
        dprintf(fd, "eventsel_umask_config=0x%llx\n", static_cast<unsigned long long>(OFFCORE_RESPONSE_EVENT_CONFIG)) <
            0 ||
        dprintf(fd, "offcore_mask_any_response=0x%llx\n",
                static_cast<unsigned long long>(OFFCORE_DEMAND_RFO_ANY_RESPONSE_MASK)) < 0 ||
        dprintf(fd, "offcore_mask_l3_miss_any_snoop=0x%llx\n",
                static_cast<unsigned long long>(OFFCORE_DEMAND_RFO_L3_MISS_ANY_SNOOP_MASK)) < 0 ||
        dprintf(fd, "demand_rfo_any_response=%llu\n", static_cast<unsigned long long>(demand_rfo_any)) < 0 ||
        dprintf(fd, "demand_rfo_l3_miss_any_snoop=%llu\n",
                static_cast<unsigned long long>(clamped_demand_rfo_l3_miss)) < 0 ||
        dprintf(fd, "demand_rfo_l3_hit_any_snoop=%llu\n", static_cast<unsigned long long>(demand_rfo_l3_hit)) < 0 ||
        dprintf(fd, "demand_rfo_l3_miss_rate=%0.8f\n", miss_rate) < 0 ||
        dprintf(fd, "demand_rfo_l3_hit_rate=%0.8f\n", hit_rate) < 0)
    {
        perror("[ARMS] Failed to write offcore_write_l3_metrics.log");
    }

    close(fd);

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Wrote OFFCORE write L3 metrics to " << OFFCORE_WRITE_L3_METRICS_LOG << std::endl;
    }
}

// ============================================================================
// Bandwidth monitoring initialization
// ============================================================================

#ifdef SCAILP
int mem_fd = -1;
void *imc_mmio_addr[NUM_IMC];
uint64_t prev_ctr_val[NUM_TIERS][NUM_IMC][NUM_BW_COUNTERS] = {0};

static uint32_t get_imc_bw_counter_offset(enum imc_bw_counters e)
{
    switch (e)
    {
    case DRAM_READS:
        return PCM_SERVER_IMC_DRAM_READS;
    case DRAM_WRITES:
        return PCM_SERVER_IMC_DRAM_WRITES;
    case NVM_READS:
        return PCM_SERVER_IMC_PMM_READS;
    case NVM_WRITES:
        return PCM_SERVER_IMC_PMM_WRITES;
    default:
        assert(!"Unknown IMC counter");
    }
}

uint64_t measure_bw(int tier)
{
    int i, j;
    uint64_t cur_ctr_val = 0;
    uint64_t cur_bw = 0;

    if (tier == 0)
    { // DRAM
        for (i = 0; i < NUM_IMC; i++)
        {
            for (j = 0; j < 2; j++)
            {
                cur_ctr_val = *((uint64_t *)(imc_mmio_addr[i] + get_imc_bw_counter_offset(j)));
                cur_bw += cur_ctr_val - prev_ctr_val[0][i][j];
                prev_ctr_val[tier][i][j] = cur_ctr_val;
            }
        }
    }
    else if (tier == 1)
    { // NVM
        for (i = 0; i < NUM_IMC; i++)
        {
            for (j = 2; j < 4; j++)
            {
                cur_ctr_val = *((uint64_t *)(imc_mmio_addr[i] + get_imc_bw_counter_offset(j)));
                cur_bw += cur_ctr_val - prev_ctr_val[1][i][j];
                prev_ctr_val[tier][i][j] = cur_ctr_val;
            }
        }
    }

    return cur_bw;
}

static int setup_imc_bw_counters()
{
    mem_fd = open("/dev/mem", O_RDONLY);
    if (mem_fd == -1)
    {
        perror("open");
        return -1;
    }

    for (int i = 0; i < NUM_IMC; i++)
    {
        // Base address of each iMC increases by 0x80000
        imc_mmio_addr[i] = (char *)libc_mmap(NULL, PCM_SERVER_IMC_MMAP_SIZE, PROT_READ, MAP_SHARED, mem_fd,
                                             IMC_BASE_ADDR + (0x80000 * i));
        if (imc_mmio_addr[i] == MAP_FAILED)
        {
            perror("mmap");
            return -1;
        }
    }

    // Measure the bandwidth once to get the initial values
    for (int i = 0; i < NUM_TIERS; i++)
    {
        measure_nvm_bw(i);
    }

    return 0;
}

#elif defined C220G5 || defined GSL_OPTANE

int bw_fds[NUM_TIERS][NUM_EVENTS][NUM_IMC];
uint64_t prev_bw_val[NUM_TIERS][NUM_EVENTS][NUM_IMC] = {0};

static int read_int_from_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        return -1;
    }

    int value = -1;
    file >> value;
    return value;
}

static int read_first_cpu_from_cpumask(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        return -1;
    }

    std::string cpumask;
    std::getline(file, cpumask);
    if (cpumask.empty())
    {
        return -1;
    }

    std::stringstream ss(cpumask);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        size_t dash_pos = token.find('-');
        std::string first_cpu = (dash_pos == std::string::npos) ? token : token.substr(0, dash_pos);
        try
        {
            return std::stoi(first_cpu);
        }
        catch (...)
        {
            continue;
        }
    }

    return -1;
}

static uint64_t read_imc_event_config(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        return 0;
    }

    std::string line;
    std::getline(file, line);
    if (line.empty())
    {
        return 0;
    }

    int event = -1;
    int umask = -1;

    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        size_t eq_pos = part.find('=');
        if (eq_pos == std::string::npos)
        {
            continue;
        }

        std::string key = part.substr(0, eq_pos);
        std::string value = part.substr(eq_pos + 1);

        try
        {
            int parsed = std::stoi(value, nullptr, 0);
            if (key == "event")
            {
                event = parsed;
            }
            else if (key == "umask")
            {
                umask = parsed;
            }
        }
        catch (...)
        {
            continue;
        }
    }

    if (event < 0 || umask < 0)
    {
        return 0;
    }

    return ((uint64_t)umask << 8) | (uint64_t)event;
}

uint64_t measure_bw(int tier)
{
    uint64_t cur_bw = 0;
    uint64_t cur_val = 0;

    for (int j = 0; j < NUM_EVENTS; j++)
    {
        for (int k = 0; k < NUM_IMC; k++)
        {
            if (read(bw_fds[tier][j][k], &cur_val, sizeof(cur_val)) == -1)
            {
                std::cerr << "ERROR: Failed to read perf event for BW monitoring" << std::endl;
                exit(1);
            }
            cur_bw += cur_val - prev_bw_val[tier][j][k];
            prev_bw_val[tier][j][k] = cur_val;
        }
    }

    return cur_bw;
}

void open_perf_events()
{
    int fd;
    struct perf_event_attr pe;

    std::cout << "[ARMS] Setting up IMC bandwidth monitoring perf events..." << std::endl;
    std::cout << "[ARMS] NUM_TIERS=" << NUM_TIERS << ", NUM_EVENTS=" << NUM_EVENTS << ", NUM_IMC=" << NUM_IMC
              << std::endl;

    for (unsigned long i = 0; i < NUM_TIERS; i++)
    {
        for (unsigned long j = 0; j < NUM_EVENTS; j++)
        {
            for (unsigned long k = 0; k < NUM_IMC; k++)
            {
                std::string imc_base = "/sys/bus/event_source/devices/uncore_imc_" + std::to_string(k);
                int imc_type = read_int_from_file(imc_base + "/type");
                int imc_cpu = read_first_cpu_from_cpumask(imc_base + "/cpumask");
                uint64_t event_config = (j == 0) ? read_imc_event_config(imc_base + "/events/cas_count_read")
                                                 : read_imc_event_config(imc_base + "/events/cas_count_write");

                if (imc_type < 0 || imc_cpu < 0 || event_config == 0)
                {
                    std::cerr << "ERROR: Failed to read IMC perf metadata from " << imc_base << " (type=" << imc_type
                              << ", cpu=" << imc_cpu << ", config=0x" << std::hex << event_config << std::dec << ")"
                              << std::endl;
                    exit(1);
                }

                memset(&pe, 0, sizeof(pe));
                pe.type = (uint32_t)imc_type;
                pe.size = sizeof(pe);
                pe.disabled = 1;
                pe.inherit = 1;
                pe.config = event_config;

                fd = perf_event_open(&pe, -1, imc_cpu, -1, 0);
                if (fd == -1)
                {
                    perror("ERROR: Failed to open perf event for BW monitoring");
                    exit(1);
                }
                bw_fds[i][j][k] = fd;
            }
        }
    }
}

static int setup_imc_bw_counters()
{
    open_perf_events();

    // Reset the counters
    for (int i = 0; i < NUM_TIERS; i++)
    {
        for (int j = 0; j < NUM_EVENTS; j++)
        {
            for (int k = 0; k < NUM_IMC; k++)
            {
                ioctl(bw_fds[i][j][k], PERF_EVENT_IOC_RESET, 0);
                ioctl(bw_fds[i][j][k], PERF_EVENT_IOC_ENABLE, 0);
            }
        }
        measure_bw(i); // Measure once to get the initial values
    }

    return 0;
}
#endif

static struct perf_event_mmap_page *perf_setup(__u64 config, __u64 config1, __u16 cpu, __u16 type)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));

    pe.type = PERF_TYPE_RAW;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.config1 = config1;
    pe.sample_period = DEFAULT_SAMPLE_PERIOD;
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
    pe.pinned = 1;
    pe.disabled = 0;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.exclude_callchain_kernel = 1;
    pe.exclude_callchain_user = 1;
    pe.precise_ip = 1;

    perf_fd[cpu][type] = perf_event_open(&pe, -1, cpu, -1, 0);
    if (perf_fd[cpu][type] == -1)
    {
        perror("perf_event_open");
        fprintf(stderr, "Failed to open perf event for CPU %d, type %d\n", cpu, type);
    }
    assert(perf_fd[cpu][type] != -1);

    size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
    std::cout << "[ARMS] PERF mmap size: " << (mmap_size / (1024.0 * 1024.0)) << " MB" << std::endl;
    struct perf_event_mmap_page *p =
        (struct perf_event_mmap_page *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd[cpu][type], 0);
    if (p == MAP_FAILED)
    {
        perror("mmap");
    }
    assert(p != MAP_FAILED);

    return p;
}

#define MEM_LOAD_RETIRED_L3_MISS_PS 0x20d1
#define MEM_INST_RETIRED_ALL_LOADS_PS 0x81d0
#define MEM_INST_RETIRED_ALL_STORES_PS 0x82d0

static constexpr __u64 read_perf_event_config()
{
    return MEM_LOAD_RETIRED_L3_MISS_PS;
}

static void setup_perf_events()
{
    const __u64 read_event = read_perf_event_config();

    std::cout << "[ARMS] Setting up PEBS counters..." << std::endl;
    std::cout << "[ARMS] READ_EVENT=0x" << std::hex << read_event << std::dec << ", WRITE_EVENT=0x" << std::hex
              << MEM_INST_RETIRED_ALL_STORES_PS << std::dec << std::endl;

    for (int i = 0; i < PEBS_NPROCS; i++)
    {
#ifdef C220G5
        // Skip node 1 cores on C220G5 (cores 10-19 are on NUMA node 1)
        if (i >= 10 && i < 20)
            continue;
#endif

#ifdef GSL_OPTANE
        // Skip node 1 cores on GSL_OPTANE (cores 10-19 are on NUMA node 1)
        if (i >= 16 && i < 32)
            continue;
#endif

        perf_page[i][READ] = perf_setup(read_event, 0, i, READ);
        perf_page[i][WRITE] = perf_setup(MEM_INST_RETIRED_ALL_STORES_PS, 0, i, WRITE);

        if (!perf_page[i][READ])
        {
            fprintf(stderr, "[ARMS] Failed to setup perf events for CPU %d\n", i);
        }
    }

    std::cout << "[ARMS] PEBS counters setup complete with a sample rate of: " << HF_SAMPLE_PERIOD << std::endl;
}

static void close_perf_events()
{
    std::cout << "[ARMS] Closing PEBS counters..." << std::endl;

    for (int i = 0; i < PEBS_NPROCS; i++)
    {
        for (int j = 0; j < NPBUFTYPES; j++)
        {
            if (perf_fd[i][j] && perf_page[i][j])
            {
                ioctl(perf_fd[i][j], PERF_EVENT_IOC_DISABLE, 0);
                munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
                close(perf_fd[i][j]);
                perf_page[i][j] = nullptr;
            }
        }
    }
}

// Returns amount of free memory in node 0 in KB
int64_t get_fasttier_free_mem()
{
    std::string path = "/sys/devices/system/node/node0/meminfo";
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "Failed to open " << path << std::endl;
        return -1;
    }
    std::string line;
    std::regex regex(R"(MemFree:\s+(\d+) kB)");
    while (getline(file, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, regex))
        {
            return std::stoll(match[1].str());
        }
    }
    return -1;
}

int64_t get_fasttier_total_mem()
{
    std::string path = "/sys/devices/system/node/node0/meminfo";
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "Failed to open " << path << std::endl;
        return -1;
    }
    std::string line;
    std::regex regex(R"(MemTotal:\s+(\d+) kB)");
    while (getline(file, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, regex))
        {
            return std::stoll(match[1].str());
        }
    }
    return -1;
}

void change_sampling_frequency()
{
    int ret = 0;
    uint64_t sample_period = DEFAULT_SAMPLE_PERIOD;

    if (sampling_mode == HIGH_FIDELITY)
    {
        sample_period = HF_SAMPLE_PERIOD;
    }

    for (int i = 0; i < PEBS_NPROCS; i++)
    {
#if defined C220G5
        if (i >= 10 && i < 20)
        {
            continue;
        }
#endif

#if defined GSL_OPTANE
        if (i >= 16 && i < 32)
        {
            continue;
        }
#endif

        for (int j = 0; j < NPBUFTYPES; j++)
        {
            ret = ioctl(perf_fd[i][j], PERF_EVENT_IOC_PERIOD, &sample_period);
            if (ret != 0)
            {
                perror("PERF_EVENT_IOC_PERIOD");
            }
        }
    }
}

// ============================================================================
// Hot-Change Detection
// ============================================================================

void detect_hot_change()
{
    float cur_dram_bw, cur_nvm_bw;

    cur_dram_bw = (float)measure_bw(0) / (1024.0 * 1024.0 * 1024.0); // GB/s
    cur_nvm_bw = (float)measure_bw(1) / (1024.0 * 1024.0 * 1024.0);  // GB/s

    // Update EWMA of bandwidth
    dram_bw_ewma = (1 - HCD_EWMA_ALPHA) * dram_bw_ewma + HCD_EWMA_ALPHA * cur_dram_bw;
    nvm_bw_ewma = (1 - HCD_EWMA_ALPHA) * nvm_bw_ewma + HCD_EWMA_ALPHA * cur_nvm_bw;
    nvm_bw_std =
        (1 - HCD_STD_ALPHA) * nvm_bw_std + HCD_STD_ALPHA * (cur_nvm_bw - nvm_bw_ewma) * (cur_nvm_bw - nvm_bw_ewma);
    nvm_bw_std = sqrtf(fmaxf(nvm_bw_std, 1e-12f)); // Avoid stddev of 0

    // Scale drift and threshold based on stddev
    // This allows the algorithm to adapt to different levels of noise in the measurements
    float drift = HCD_PH_DRIFT * nvm_bw_std;
    float threshold = HCD_PH_THRESHOLD * nvm_bw_std;

    // CUSUM (Page-Hinkley test) for phase change detection
    cusum += ((cur_nvm_bw - nvm_bw_ewma) - drift);
    cusum = fmaxf(cusum, 0.0f); // Only interested in positive deviations
    if (cusum > threshold)
    {
        // Switch to recency bias if currently using history bias and NVM bandwidth is high
        if (active_bias == hist_bias && cur_nvm_bw > HCD_RECN_MIN_NVM_BW)
        {
            active_bias = recn_bias;
            time_since_recn = 0;
            std::cout << "[ARMS-HCD] Phase change detected! Switching to RECENCY bias "
                      << "(NVM BW: " << cur_nvm_bw << " GB/s, CUSUM: " << cusum << ")" << std::endl;
        }
        cusum = 0;
    }
    else if (active_bias == recn_bias)
    {
        // Check if we should switch back to history bias
        time_since_recn++;
        if (time_since_recn >= HCD_RECN_MAX_PERIODS && cusum <= 0)
        {
            active_bias = hist_bias;
            time_since_recn = 0;
            std::cout << "[ARMS-HCD] Stable phase detected. Switching back to HISTORY bias" << std::endl;
        }
    }
}

// ============================================================================
// Scoring and Migration Policy
// ============================================================================

static float compute_score(const page_ptr &page)
{
    float score = 0;
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        score += (active_bias[i] * page->w[i]);
    }
    return score;
}

static int score_compare(const void *a, const void *b)
{
    const score_entry *ea = (const score_entry *)a;
    const score_entry *eb = (const score_entry *)b;
    return (ea->score > eb->score) ? -1 : (ea->score < eb->score);
}

#if FULL_LOGS == (true)
static int sort_entry_cmp_by_ewma5(const void *a, const void *b);
#endif

struct ranking_plan
{
    uint32_t max_migrations_cur_interval;
    size_t candidate_window;
    bool use_partial_ranking;
};

struct page_distribution
{
    double dram_pages = 0;
    double far_pages = 0;
};

struct migration_decision
{
    std::vector<page_ptr> promote_list;
    std::vector<page_ptr> demote_list;
    double migrated_count = 0;
};

static inline int hugepage_dram_count(const page_ptr &page)
{
    return page->in_dram ? 1 : 0;
}

static std::vector<page_ptr> snapshot_tracked_pages()
{
    std::vector<page_ptr> page_snapshot;

    std::shared_lock<std::shared_mutex> lock(pages_map_lock);
    page_snapshot.reserve(pages_map.size());
    for (auto &kv : pages_map)
    {
        page_snapshot.push_back(kv.second);
    }

    return page_snapshot;
}

static size_t update_page_scores(const std::vector<page_ptr> &page_snapshot, std::vector<score_entry> &scores)
{
    size_t accesses_total = 0;

#if USE_MODEL == (true) || PRINT_TRAINING_DATA == (true) || LOGGING_RUN == (true)
    reset_group_hash(grp_tracker);
#endif

    scores.reserve(page_snapshot.size());
    for (auto &page : page_snapshot)
    {
        page->update_window(prev_access_version, sampling_mode);
        accesses_total += page->count;

        page->prev_score = page->score;

        page->arms_score = compute_score(page);

#if USE_MODEL == (false)
        page->score = page->arms_score;
        scores.push_back({page, page->score});
#else
        scores.push_back({page, 0});
#endif
    }

    return accesses_total;
}

static void update_model_scores_and_log(std::vector<score_entry> &scores, size_t timestep, size_t accesses_total)
{
#if USE_MODEL == (false) && PRINT_TRAINING_DATA == (false) && LOGGING_RUN == (false)
    (void)scores;
    (void)timestep;
    (void)accesses_total;
    return;
#endif
#if FULL_LOGS == (true)
    std::sort(scores.begin(), scores.end(),
              [](const score_entry &a, const score_entry &b) { return sort_entry_cmp_by_ewma5(&a, &b) < 0; });
#endif
    const offcore_write_l3_step_metrics offcore_step_metrics = snapshot_offcore_write_l3_step_metrics();

    for (size_t i = 0; i < scores.size(); i++)
    {
        scores[i].page->update_derivative_features(i, scores.size(), accesses_total);
        update_group_entry(grp_tracker, scores[i].page, accesses_total);
    }

    std::vector<struct data_row> rows;
    std::vector<struct std::shared_ptr<page_info>> model_pages;
    rows.reserve(scores.size());
    model_pages.reserve(scores.size());

    for (auto &score_entry : scores)
    {
        struct data_row row = access_log->extract_row(timestep, score_entry.page, grp_tracker,
                                                      accesses_total); // Extract previous row data

        // Keep policy-time inference in sync with synthetic virtual-step logging for
        // freshly allocated pages that have not yet completed a virtual step.
        if (VIRTUAL_FEATURES_ENABLED && row.step == 0 && row.age == 0)
        {
            const uint64_t current_virtual_step = virtual_step.load(std::memory_order_relaxed);
            if (current_virtual_step > 0)
            {
                row.step = static_cast<size_t>(current_virtual_step - 1);
            }

            zero_cold_start_virtual_row_fields(row);
            fill_virtual_neighbor_group_features(virtual_grp_tracker, score_entry.page, row);
            row.num_demotions = 0;
            row.num_promotions = 0;
        }

        if (offcore_step_metrics.available)
        {
            row.step_offcore_write_l3_miss_rate = static_cast<float>(offcore_step_metrics.step_miss_rate);
        }
        row.arms_score = compute_score(score_entry.page);
        score_entry.page->arms_score = row.arms_score;
        rows.push_back(row);
        model_pages.push_back(score_entry.page);
    }

    model_predict_batch(rows, model_pages);

    for (size_t i = 0; i < scores.size(); ++i)
    {
        auto &score_entry = scores[i];
        auto &row = rows[i];

#if MODEL_SCORE_HISTORY_SUMMARY == MODEL_SCORE_HISTORY_SUMMARY_MIN_MAX
        float history_model_score = score_entry.page->in_dram ? score_entry.page->max_model_score_history()
                                                              : score_entry.page->min_model_score_history();
#elif MODEL_SCORE_HISTORY_SUMMARY == MODEL_SCORE_HISTORY_SUMMARY_AVERAGE
        float history_model_score = score_entry.page->average_model_score_history();
#elif MODEL_SCORE_HISTORY_SUMMARY == MODEL_SCORE_HISTORY_SUMMARY_MOVING_AVERAGE
        float history_model_score = score_entry.page->adjusted_moving_average_model_score();
#endif

        if (USE_MODEL)
        {
            score_entry.page->score = history_model_score;
        }
        else
        {
            score_entry.page->score = score_entry.page->arms_score;
        }
        score_entry.score = score_entry.page->score;
        row.score = score_entry.score;
        if (!VIRTUAL_FEATURES_ENABLED)
        {
            access_log->log_row(score_entry.page, row);
        }
    }
}

static ranking_plan rank_scores_for_migration(size_t dram_pages, size_t free_hugepages,
                                              std::vector<score_entry> &scores)
{
    uint32_t max_migrations = 0;
    if (ENABLE_MIGRATION_WORKERS)
    {
        int32_t policy_time = policy_thread_interval;
        if (free_hugepages * promotion_cost_avg >= policy_thread_interval)
        {
            max_migrations = policy_thread_interval / promotion_cost_avg;
        }
        else
        {
            policy_time -= (static_cast<int32_t>(free_hugepages * promotion_cost_avg));
            max_migrations = free_hugepages + (policy_time / (promotion_cost_avg + demotion_cost_avg));
        }
    }
    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] free_hugepages: " << free_hugepages << ", max_migrations: " << max_migrations << std::endl;
    }
    ranking_plan plan = {ENABLE_MIGRATION_WORKERS ? max_migrations * MIGRATION_WORKER_COUNT : 0U, scores.size(), false};

    auto score_desc = [](const score_entry &a, const score_entry &b) { return score_compare(&a, &b) < 0; };

    std::sort(scores.begin(), scores.end(), score_desc);
    plan.candidate_window = scores.size();

    for (size_t i = 0; i < plan.candidate_window; i++)
    {
        scores[i].page->update_can_promote(dram_pages + free_hugepages, i);
    }

    return plan;
}

static page_distribution summarize_page_distribution(const std::vector<score_entry> &scores)
{
    page_distribution distribution;

    for (const auto &entry : scores)
    {
        if (entry.page->in_dram)
        {
            distribution.dram_pages++;
        }
        else
        {
            distribution.far_pages++;
        }
    }

    return distribution;
}

static int64_t fasttier_free_kb()
{
    int64_t fasttier_free_kb = std::max(get_fasttier_free_mem() - (get_fasttier_total_mem() / 100 * 5), 0l);
    return fasttier_free_kb;
}

static int64_t calculate_free_hugepages()
{
    int64_t free_hugepages = fasttier_free_kb() * 1024 / PAGE_SIZE;
    return free_hugepages;
}

static migration_decision select_migration_candidates(const std::vector<score_entry> &scores, const ranking_plan &plan)
{
    migration_decision decision;
    size_t promote_idx = 0;
    int64_t demote_idx = static_cast<int64_t>(scores.size()) - 1;

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Max migrations allowed this interval: " << plan.max_migrations_cur_interval << std::endl;
    }

    int64_t free_hugepages = calculate_free_hugepages();

    std::vector<page_ptr> backup_pages;
    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Fast tier free memory: " << fasttier_free_kb() << " KB (" << free_hugepages
                  << " hugepages)" << std::endl;
    }

    while (promote_idx < static_cast<size_t>(demote_idx))
    {
        if (decision.migrated_count >= plan.max_migrations_cur_interval)
        {
            break;
        }

        page_ptr hot_page = scores[promote_idx++].page;

        if (hot_page->in_dram)
        {
            continue;
        }
        if (hot_page->promote_backoff > 0)
        {
            continue;
        }
#if USE_MODEL == (false) && LOGGING_RUN == (false)
        if (!hot_page->can_promote)
        {
            continue;
        }
#endif

        float cost = PROMOTION_COST_MULTIPLIER * (promotion_cost_avg);
#if USE_MODEL == (true)
        // float benefit = SWITCH_SCALER * hot_page->score * HF_SAMPLE_PERIOD * latency_diff;
        float benefit = hot_page->score * HF_SAMPLE_PERIOD * latency_diff;
        // std::cout << "cost: " << cost << " PROMOTION_COST_MULTIPLIER: " << PROMOTION_COST_MULTIPLIER
        //           << " MODEL_DISCOUNT_PERCENT: " << MODEL_DISCOUNT_PERCENT
        //           << " promotion_cost_avg: " << promotion_cost_avg << " hot_page->score: " << hot_page->score;
        // std::cout << " scaler: " << SWITCH_SCALER << " mul: " << HF_SAMPLE_PERIOD * latency_diff << std::endl;
#else
        float benefit = hot_page->score * hot_page->hot_age * HF_SAMPLE_PERIOD * latency_diff;
#endif

        if (benefit < cost)
        {
            break;
        }

        page_ptr selected_cold_page = nullptr;
        if (free_hugepages <= 0)
        {
            while (demote_idx >= 0 && demote_idx > static_cast<int64_t>(promote_idx))
            {
                page_ptr cold_page = scores[demote_idx--].page;
                if (cold_page->promote_backoff > 0)
                {
                    continue;
                }

#if USE_MODEL == (false) && LOGGING_RUN == (false)
                float hot_page_min_avg = std::min(hot_page->w[0], hot_page->w[2]);
                float cold_page_max_avg = std::max(cold_page->w[0], cold_page->w[2]);

                if (hot_page_min_avg < cold_page_max_avg)
                {
                    break;
                }
#endif

                if (cold_page->in_dram)
                {
                    selected_cold_page = cold_page;
                    cost += DEMOTION_COST_MULTIPLIER * (demotion_cost_avg);
#if USE_MODEL == (true)
                    benefit -= cold_page->score * HF_SAMPLE_PERIOD * latency_diff;
#else
                    benefit -= cold_page->score * hot_page->hot_age * HF_SAMPLE_PERIOD * latency_diff;
#endif
                    break;
                }
            }
        }

        // We only want to migrate very hot fragmented pages
        hot_page->would_migrate_fragmented = (benefit > (1 * cost));
        if (hot_page->fragmented && !hot_page->would_migrate_fragmented)
        {
            continue;
        }

        if (benefit > cost)
        {
            if (selected_cold_page)
            {
                selected_cold_page->would_migrate_fragmented = hot_page->would_migrate_fragmented;
                selected_cold_page->num_demotions++;
                decision.demote_list.push_back(selected_cold_page);
                selected_cold_page->promote_backoff = BACKOFF_PERIOD;
                free_hugepages++;
                decision.migrated_count++;
            }
            hot_page->num_promotions++;
            decision.promote_list.push_back(hot_page);
            hot_page->promote_backoff = BACKOFF_PERIOD;
            free_hugepages--;
            decision.migrated_count++;
        }

        if (decision.migrated_count >= plan.max_migrations_cur_interval)
        {
            break;
        }
    }

    return decision;
}

#if FULL_LOGS == (true)
// Sorts in descending order by ewma5
static int sort_entry_cmp_by_ewma5(const void *a, const void *b)
{
    struct score_entry _a = *(const struct score_entry *)a;
    struct score_entry _b = *(const struct score_entry *)b;

    return (_a.page->w[1] > _b.page->w[1]) ? -1 : (_a.page->w[1] < _b.page->w[1]);
}
#endif

void update_scores_and_migrate(size_t timestep)
{
#if USE_MODEL == (false) && PRINT_TRAINING_DATA == (false) && LOGGING_RUN == (false)
    (void)timestep;
#endif

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Updating page scores and deciding migrations..." << std::endl;
    }

    std::vector<score_entry> scores;
    std::vector<page_ptr> page_snapshot = snapshot_tracked_pages();
    size_t accesses_total = update_page_scores(page_snapshot, scores);
    update_model_scores_and_log(scores, timestep, accesses_total);

    if (scores.empty())
        return;

    page_distribution distribution = summarize_page_distribution(scores);

    int64_t free_hugepages = calculate_free_hugepages();
    ranking_plan plan = rank_scores_for_migration(distribution.dram_pages, free_hugepages, scores);

    // Print the max and min scores for debugging
    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Number of tracked pages: " << scores.size()
                  << " (DRAM: " << static_cast<int>(distribution.dram_pages)
                  << ", NVM: " << static_cast<int>(distribution.far_pages) << "), Max score: " << scores[0].score
                  << ", Median score: " << scores[scores.size() / 2].score
                  << ", Min score: " << scores[scores.size() - 1].score << std::endl;
    }

    migration_decision decision = select_migration_candidates(scores, plan);

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Scores updated. Promote candidates: " << decision.promote_list.size()
                  << ", Demote candidates: " << decision.demote_list.size()
                  << " migrated count: " << decision.migrated_count << " hugepages" << std::endl;
    }

    // Perform migrations by pairing promotions and demotions in the same tasks
    if (!decision.promote_list.empty() || !decision.demote_list.empty())
    {
        enqueue_migration_task(decision.promote_list, decision.demote_list);
    }

    // If no migrations happened, decay the cost estimates
    if (decision.promote_list.empty() && decision.demote_list.empty())
    {
        promotion_cost_avg = std::max(promotion_cost_avg / MIGRATION_COST_DECAY_RATE, (double)MIN_PROMOTION_COST);
        demotion_cost_avg = std::max(demotion_cost_avg / MIGRATION_COST_DECAY_RATE, (double)MIN_DEMOTION_COST);
    }
}

// ============================================================================
// Public API
// ============================================================================

void arms_start_tiering()
{
    std::cout << "[ARMS] Initializing ARMS..." << std::endl;

    std::cout << "[ARMS] USE_MODEL = " << (USE_MODEL ? "true" : "false") << std::endl;
    std::cout << "[ARMS] LOGGING_RUN = " << (LOGGING_RUN ? "true" : "false") << std::endl;
    std::cout << "[ARMS] ENABLE_MIGRATION_WORKERS = " << (ENABLE_MIGRATION_WORKERS ? "true" : "false") << std::endl;
    std::cout << "[ARMS] MODEL_SCORE_HISTORY_SUMMARY = " << MODEL_SCORE_HISTORY_SUMMARY_NAME << std::endl;
    std::cout << "[ARMS] MODEL_SCORE_MOVING_AVERAGE_ALPHA_IDX = " << MODEL_SCORE_MOVING_AVERAGE_ALPHA_IDX << std::endl;
    std::cout << "[ARMS] MODEL_SCORE_MOVING_AVERAGE_ALPHA = " << w_ewma_alpha[MODEL_SCORE_MOVING_AVERAGE_ALPHA_IDX]
              << std::endl;
    std::cout << "[ARMS] HISTORY_LENGTH = " << HISTORY_LENGTH << std::endl;

    std::cout << "[ARMS] PRINT_TRAINING_DATA = " << (PRINT_TRAINING_DATA ? "true" : "false") << std::endl;
    std::cout << "[ARMS] ENABLE_MIGRATION_WORKERS = " << (ENABLE_MIGRATION_WORKERS ? "true" : "false") << std::endl;
    std::cout << "[ARMS] VIRTUAL_FEATURES_ENABLED = " << (VIRTUAL_FEATURES_ENABLED ? "true" : "false") << std::endl;
    std::cout << "[ARMS] VIRTUAL_STEP_SAMPLES = " << get_virtual_step_samples() << std::endl;
    std::cout << "[ARMS] PEBS_KSWAPD_INTERVAL_BIG = " << PEBS_KSWAPD_INTERVAL_BIG << std::endl;
    std::cout << "[ARMS] PEBS_KSWAPD_INTERVAL_SMALL = " << PEBS_KSWAPD_INTERVAL_SMALL << std::endl;

    struct bitmask *default_nodemask = numa_allocate_nodemask();
    if (default_nodemask == nullptr)
    {
        perror("[ARMS] numa_allocate_nodemask failed");
    }
    else
    {
        numa_bitmask_clearall(default_nodemask);

        if (LOGGING_RUN)
        {
            std::cout << "[ARMS] Running in LOGGING_RUN mode - binding to FAST_TIER only" << std::endl;
            numa_bitmask_setbit(default_nodemask, FAST_TIER);
        }
        else
        {
            std::cout << "[ARMS] Running in tiering mode - binding to SLOW_TIER only" << std::endl;
            numa_bitmask_setbit(default_nodemask, SLOW_TIER);
        }

        numa_set_membind(default_nodemask);
        numa_bitmask_free(default_nodemask);
    }

#if USE_MODEL == (true) || PRINT_TRAINING_DATA == (true) || LOGGING_RUN == (true)
    grp_tracker = create_group_tracker();
    virtual_grp_tracker = create_group_tracker();
    virtual_sample_total.store(0, std::memory_order_relaxed);
    virtual_step.store(0, std::memory_order_relaxed);

    access_log = new class access_log;
    printf("Allocated %f GB for scores_log\n", access_log->get_gb_allocated());
#else
    grp_tracker = nullptr;
    virtual_grp_tracker = nullptr;
    access_log = nullptr;
#endif

    // Open pagemap
    target_pid = getpid();
    char pagemap_path[256];
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", target_pid);
    pagemap_fd = open(pagemap_path, O_RDONLY);
    if (pagemap_fd < 0)
    {
        perror("Failed to open pagemap");
        return;
    }

    // Check NUMA configuration
    if (numa_available() < 0)
    {
        fprintf(stderr, "[ARMS] NUMA not available\n");
        close(pagemap_fd);
        return;
    }

    // Set DRAM size from MemTotal (node0)
    uint64_t memtotal_kb = get_fasttier_free_mem();
    if (memtotal_kb == (uint64_t)-1)
    {
        std::cerr << "[ARMS] Failed to read MemTotal; falling back to FAST_MEMORY_SIZE" << std::endl;
        dramsize = FAST_MEMORY_SIZE;
    }
    else
    {
        dramsize = memtotal_kb * 1024ULL;
        FAST_MEMORY_SIZE = dramsize;
    }

    std::cout << "[ARMS] DRAM size (bytes): " << dramsize << std::endl;

    std::cout << "[ARMS] NUMA nodes available: " << numa_max_node() + 1 << std::endl;

    // Setup PEBS
    setup_perf_events();
    setup_offcore_write_l3_metrics_counters();
    // Initialize memory controller BW counters
    setup_imc_bw_counters();

    initialized = true;

    pthread_create(&scan_thread, nullptr, pebs_scan_thread, nullptr);

    // Start scanning and policy threads
    pthread_create(&pagemap_scan_thread, nullptr, pagemap_scan_thread_fn, nullptr);

    // Logging-only far-memory runs keep ARMS sampling active but leave migrations disabled.
    if (ENABLE_MIGRATION_WORKERS)
    {
        for (size_t i = 0; i < MIGRATION_WORKER_COUNT; i++)
        {
            pthread_create(&migration_threads[i], nullptr, migration_worker, nullptr);
        }
    }

    pthread_create(&policy_thread, nullptr, arms_policy_thread, nullptr);

    std::cout << "[ARMS] Initialization complete." << std::endl;
}

static void write_max_dram_usage_to_file()
{
    if (PAGE_SIZE == 0)
    {
        fprintf(stderr, "[ARMS] PAGE_SIZE is zero; skipping DRAM usage logging.\n");
        return;
    }

    uint64_t max_hugepages = max_dram_hugepages_seen.load(std::memory_order_relaxed);

    uint64_t samples = dram_samples.load(std::memory_order_relaxed);
    double avg_hugepages = 0.0;
    if (samples > 0)
    {
        avg_hugepages = static_cast<double>(total_dram_hugepages_accum.load(std::memory_order_relaxed)) /
                        static_cast<double>(samples);
    }

    int fd = open(MAX_DRAM_HUGEPAGE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("[ARMS] Failed to open max_dram_hugepages.log");
        return;
    }

    if (dprintf(fd, "max_hugepages_in_dram=%llu\n", static_cast<unsigned long long>(max_hugepages)) < 0 ||
        dprintf(fd, "avg_hugepages_in_dram=%f\n", avg_hugepages) < 0 ||
        dprintf(fd, "samples=%llu\n", static_cast<unsigned long long>(samples)) < 0 ||
        dprintf(fd, "hugepage_size_bytes=%llu\n", static_cast<unsigned long long>(HUGEPAGE_SIZE)) < 0)
    {
        perror("[ARMS] Failed to write max_dram_hugepages.log");
    }

    close(fd);
}

void arms_kernel_shutdown()
{
    bool expected = false;
    if (!shutdown_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return; // Already shutting down
    }

    if (!initialized)
    {
        fprintf(stderr, "[ARMS] Shutdown requested before initialization; skipping cleanup.\n");
        return;
    }

    terminated.store(true, std::memory_order_relaxed);

    if (!PRINT_TRAINING_DATA)
    {
        write_offcore_write_l3_metrics_to_file();
        close_offcore_write_l3_metrics_counters();
        // must exit using syscall to avoid invoking atexit handlers that might try to use ARMS data structures after
        // they've been cleaned up
        _exit(0);
    }

    printf("[ARMS] Shutting down...\n");

    if (ENABLE_MIGRATION_WORKERS)
    {
        // Wake workers so they can notice termination.
        migration_cv.notify_all();
    }

    // Gracefully join background threads
    pthread_join(scan_thread, nullptr);
    pthread_join(pagemap_scan_thread, nullptr);
    pthread_join(policy_thread, nullptr);
    if (ENABLE_MIGRATION_WORKERS)
    {
        for (size_t i = 0; i < MIGRATION_WORKER_COUNT; i++)
        {
            pthread_join(migration_threads[i], nullptr);
        }
    }
    close_perf_events();
    write_offcore_write_l3_metrics_to_file();
    close_offcore_write_l3_metrics_counters();

    printf("[ARMS] Closed PEBS counters.\n");

    if (pagemap_fd >= 0)
    {
        close(pagemap_fd);
    }

    printf("[ARMS] Closed pagemap.\n");

    if (access_log != nullptr)
    {
        access_log->pebs_write_log();
    }

    write_max_dram_usage_to_file();
    printf("[ARMS] Wrote peak DRAM usage to %s\n", MAX_DRAM_HUGEPAGE_LOG);

    printf("[ARMS] Shutdown complete.\n");
}