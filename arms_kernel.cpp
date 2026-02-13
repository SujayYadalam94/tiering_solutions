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

#ifdef FAST_MEMORY_SIZE_GB
uint64_t FAST_MEMORY_SIZE = (FAST_MEMORY_SIZE_GB * (1024L * 1024L * 1024L)); // size of fast tier memory in bytes
#else
uint64_t FAST_MEMORY_SIZE = 0;
#endif

// Global state
uint64_t dramsize = 0;

bool initialized = false;

#define SYSCALL_EVENT_QUEUE_CAPACITY (128 * 1024)
std::atomic<size_t> syscall_queue_index{0};
std::atomic<size_t> syscall_queue_size{0};
std::atomic<struct syscall_event *> syscall_event_queue{nullptr};
std::atomic<uint64_t> scan_generation{0};

std::unordered_map<uint64_t, std::shared_ptr<page_info>> pages_map;
std::mutex pages_map_lock;

bool is_access_log_page(uint64_t page_base)
{
    return access_log != nullptr && access_log->overlaps_with_logging_region(page_base, PAGE_SIZE);
}

int pagemap_fd = -1;
pid_t target_pid = 0;

int perf_fd[PEBS_NPROCS][NPBUFTYPES];
struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];

pthread_t scan_thread;
pthread_t pagemap_scan_thread;
pthread_t policy_thread;
pthread_t migration_threads[MIGRATION_WORKER_COUNT];
pthread_t madvise_thread;

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
std::atomic<uint64_t> max_dram_base_pages_seen{0};
std::atomic<uint64_t> total_dram_base_pages_accum{0};
std::atomic<uint64_t> dram_samples{0};
std::atomic<bool> shutdown_started{false};
static constexpr const char *MAX_DRAM_HUGEPAGE_LOG = "max_dram_hugepages.log";

float dram_bw_ewma = 0.0;
float nvm_bw_ewma = 0.0;
float nvm_bw_std = 0.0;
float cusum = 0.0;
uint32_t time_since_recn = 0;

float promotion_cost_avg = MIN_PROMOTION_COST;
float demotion_cost_avg = MIN_DEMOTION_COST;
float latency_diff = UNLOADED_NVM_LAT - UNLOADED_DRAM_LAT;

bool terminated = false;
struct group_tracker *grp_tracker = NULL;

std::atomic<bool> madvise_thread_running{false};

// ============================================================================
// PERF Event Setup
// ============================================================================

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    int ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
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

#elif defined C220G5

int bw_fds[NUM_TIERS][NUM_EVENTS][NUM_IMC];
uint64_t prev_bw_val[NUM_TIERS][NUM_EVENTS][NUM_IMC] = {0};

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

    for (unsigned long i = 0; i < NUM_TIERS; i++)
    {
        for (unsigned long j = 0; j < NUM_EVENTS; j++)
        {
            for (unsigned long k = 0; k < NUM_IMC; k++)
            {
                memset(&pe, 0, sizeof(pe));
                pe.type = i + 12; // TODO: read type from /sys/devices/uncore_imc_x/type
                pe.size = sizeof(pe);
                pe.disabled = 1;
                pe.inherit = 1;
                pe.config = (j == 0) ? 0x304 : 0xC04;

                fd = perf_event_open(&pe, -1, 10, -1, 0);
                if (fd == -1)
                {
                    std::cerr << "ERROR: Failed to open perf event for BW monitoring" << std::endl;
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
    struct perf_event_mmap_page *p =
        (struct perf_event_mmap_page *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd[cpu][type], 0);
    if (p == MAP_FAILED)
    {
        perror("mmap");
    }
    assert(p != MAP_FAILED);

    return p;
}

#ifdef SCAILP
#define L3_LOAD_MISS_LOCAL 0x2d3
#define L3_LOAD_MISS_REMOTE 0x10d3
#elif defined C220G5
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x2d3
#endif

static void setup_perf_events()
{
    std::cout << "[ARMS] Setting up PEBS counters..." << std::endl;

    for (int i = 0; i < PEBS_NPROCS; i++)
    {
#ifdef C220G5
        // Skip node 1 cores on C220G5 (cores 10-19 are on NUMA node 1)
        if (i >= 10 && i < 20)
            continue;
#endif
        perf_page[i][DRAMREAD] = perf_setup(L3_LOAD_MISS_LOCAL, 0, i, DRAMREAD);
        perf_page[i][NVMREAD] = perf_setup(L3_LOAD_MISS_REMOTE, 0, i, NVMREAD);
        perf_page[i][WRITE] = perf_setup(0x82d0, 0, i, WRITE); // MEM_INST_RETIRED.ALL_STORES

        if (!perf_page[i][DRAMREAD] || !perf_page[i][NVMREAD])
        {
            fprintf(stderr, "[ARMS] Failed to setup perf events for CPU %d\n", i);
        }
    }

    std::cout << "[ARMS] PEBS counters setup complete." << std::endl;
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

uint64_t get_fasttier_total_mem()
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
            return std::stoull(match[1].str());
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

static float compute_score(const std::shared_ptr<page_info> &page, struct data_row &row)
{
    float score = 0;
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        score += (active_bias[i] * page->w[i]);
    }
    row.arms_score = score;
    return score;
}

static int score_compare(const void *a, const void *b)
{
    const score_entry *ea = (const score_entry *)a;
    const score_entry *eb = (const score_entry *)b;
    return (ea->score > eb->score) ? -1 : (ea->score < eb->score);
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
    std::cout << "[ARMS] Updating page scores and deciding migrations..." << std::endl;

    std::vector<score_entry> scores;

    reset_group_hash(grp_tracker);

    // Update scores for all pages
    size_t accesses_total = 0;
    {
        std::lock_guard<std::mutex> lock(pages_map_lock);
        for (auto &kv : pages_map)
        {
            auto page = kv.second;

            page->update_window(prev_access_version, sampling_mode);
            accesses_total += page->count;

            page->prev_score = page->score;

            if (page->seen_pages <= 0)
                continue;
            scores.push_back({page, 0});
        }
    }

    // Only process events if we have a valid queue (will be NULL on first call)
    size_t total_malloc_calls = 0;
    size_t size = atomic_load(&syscall_queue_size);
    size_t index = atomic_load(&syscall_queue_index);
    printf("Processing %ld syscall events\n", size - index);
    for (; index < size; atomic_store(&syscall_queue_index, (++index)))
    {
        struct syscall_event item = atomic_load(&syscall_event_queue)[index % SYSCALL_EVENT_QUEUE_CAPACITY];
        uint64_t round_down = (uint64_t)item.addr & ~(PAGE_SIZE - 1);
        if (item.len / PAGE_SIZE > 1000) // this would be 2GB
        {
            std::cout << "[ARMS] Warning: Skipping syscall event with large length: " << item.len << " bytes"
                      << std::endl;
            continue;
        }
        for (uint64_t off = 0; off < item.len; off += PAGE_SIZE)
        {
            std::shared_ptr<page_info> page;
            {
                std::lock_guard<std::mutex> lock(pages_map_lock);
                if (pages_map.find(round_down + off) == pages_map.end())
                {
                    break;
                }
                page = pages_map[round_down + off];
            }

            switch (item.type)
            {
            case MALLOC_SYSCALL:
                page->min_malloc_bytes =
                    (page->min_malloc_bytes == -1)
                        ? item.len
                        : (((int64_t)item.len < page->min_malloc_bytes) ? item.len : page->min_malloc_bytes);
                page->max_malloc_bytes =
                    (page->max_malloc_bytes == -1)
                        ? item.len
                        : (((int64_t)item.len > page->max_malloc_bytes) ? item.len : page->max_malloc_bytes);
                page->sum_malloc_bytes += item.len;
                page->malloc_call++;
                total_malloc_calls++;
                break;
            case READ_SYSCALL:
                page->read_bytes += item.len;
                page->read_syscalls++;
                break;
            case WRITE_SYSCALL:
                page->write_bytes += item.len;
                page->write_syscalls++;
                break;
            default:
                printf("Unknown syscall type %d\n", item.type);
                break;
            }
        }
    }

#if FULL_LOGS == (true)
    std::sort(scores.begin(), scores.end(),
              [](const score_entry &a, const score_entry &b) { return sort_entry_cmp_by_ewma5(&a, &b) < 0; });
#endif
    // double cpu_usage = access_log->calc_cpu_usage_pct();
    for (size_t i = 0; i < scores.size(); i++)
    {
        scores[i].page->update_derivative_features(i, scores.size(), accesses_total, total_malloc_calls);
        update_group_entry(grp_tracker, scores[i].page, accesses_total, total_malloc_calls);
    }

    for (auto &score_entry : scores)
    {
        struct data_row row = access_log->extract_row(timestep, score_entry.page, grp_tracker,
                                                      accesses_total); // Extract previous row data
        score_entry.page->arms_score = compute_score(score_entry.page, row);

        if (score_entry.page->seen_pages <= 0)
        {
            // do not log row, and do no waste time on inference
            score_entry.page->score = score_entry.page->arms_score;
            score_entry.score = score_entry.page->score;
            continue;
        }

        row.model_score = model_predict(row, *score_entry.page);

#if MIN_MAX_HISTORY == (true)
        float history_model_score = score_entry.page->pages_in_dram > 0 ? score_entry.page->max_model_score_history()
                                                                        : (score_entry.page->min_model_score_history());
#else
        float history_model_score = score_entry.page->pages_in_dram > 0
                                        ? score_entry.page->average_model_score_history()
                                        : 0.9 * (score_entry.page->average_model_score_history());
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
        access_log->log_row(score_entry.page, row);
    }

    if (scores.empty())
        return;

    // Sort by score (descending)
    std::sort(scores.begin(), scores.end(),
              [](const score_entry &a, const score_entry &b) { return score_compare(&a, &b) < 0; });

    for (size_t i = 0; i < scores.size(); i++)
    {
        scores[i].page->update_can_promote(dramsize / PAGE_SIZE, i);
    }

    double dram_pages = 0;
    double far_pages = 0;
    for (const auto &entry : scores)
    {
        // std::lock_guard<std::mutex> lock(entry.page->page_lock);

        if (false)
        {
            if (entry.page->pages_in_dram != entry.page->seen_pages)
                std::cout << entry.page->pages_in_dram << "/" << entry.page->seen_pages << std::endl;
            else if (entry.page->pages_in_dram > 512 || entry.page->seen_pages > 512)
                std::cout << entry.page->pages_in_dram << "/" << entry.page->seen_pages << std::endl;
            else if (entry.page->pages_in_dram < 0 || entry.page->seen_pages < 0)
                std::cout << entry.page->pages_in_dram << "/" << entry.page->seen_pages << std::endl;
        }
        dram_pages += entry.page->pages_in_dram;
        far_pages += (entry.page->seen_pages - entry.page->pages_in_dram);
    }

    // Print the max and min scores for debugging
    std::cout << "[ARMS] Number of tracked pages: " << scores.size()
              << " (DRAM: " << dram_pages / BASE_PAGE_PER_HUGEPAGE << ", NVM: " << far_pages / BASE_PAGE_PER_HUGEPAGE
              << "), Max score: " << scores[0].score << ", Median score: " << scores[scores.size() / 2].score
              << ", Min score: " << scores[scores.size() - 1].score << std::endl;

    // Identify promotion and demotion candidates
    std::vector<uint64_t> promote_list;
    std::vector<uint64_t> demote_list;

    uint64_t promote_idx = 0;
    uint64_t demote_idx = scores.size() - 1;
    double migrated_count = 0;

    uint32_t max_migrations_cur_interval =
        ((policy_thread_interval) / (promotion_cost_avg + demotion_cost_avg)) * MIGRATION_WORKER_COUNT;
    std::cout << "[ARMS] Max migrations allowed this interval: " << max_migrations_cur_interval << std::endl;

    int64_t fasttier_free_kb = std::max(get_fasttier_free_mem() - MIN_FREE_MEMORY, (int64_t)0);
    int64_t fasttier_free_base_pages = (fasttier_free_kb == (int64_t)-1) ? 0 : (fasttier_free_kb * 1024) / BASE_PAGE;
    int64_t free_base_pages = fasttier_free_base_pages;

    if (free_base_pages > 0)
    {
        numa_set_preferred(0);
    }

    std::shared_ptr<page_info> backup_page;
    // Try to promote hot NVM pages, demoting cold DRAM pages if necessary
    std::cout << "[ARMS] Fast tier free memory: " << fasttier_free_kb << " KB (" << fasttier_free_base_pages
              << " base pages)" << std::endl;
    while (promote_idx < scores.size() && promote_idx < demote_idx)
    {
        int64_t hot_need_base_pages = 0;
        if ((migrated_count / BASE_PAGE_PER_HUGEPAGE) >= max_migrations_cur_interval)
        {
            break; // Reached max migrations for this interval
        }

        std::shared_ptr<page_info> hot_page = scores[promote_idx++].page;

        if (hot_page->pages_in_dram > 0)
        {
            // std::lock_guard<std::mutex> lock(hot_page->page_lock);
            if (hot_page->pages_in_dram < hot_page->seen_pages)
            {
                // push back pages that got missed during migration
                hot_need_base_pages = hot_page->seen_pages - hot_page->pages_in_dram;
                migrated_count += hot_need_base_pages;
            }
            else
            {
                continue;
            }
        }
        else
        {
            if (!backup_page)
            {
                backup_page = hot_page;
            }
#if USE_MODEL == (true) || LOGGING_RUN == (true)
            // only backoff if there are no pages already in dram
            if (hot_page->promote_backoff > 0)
            {
                continue;
            }
#else
            if (!hot_page->can_promote)
            {
                continue;
            }
#endif

            hot_need_base_pages = hot_page->seen_pages - hot_page->pages_in_dram;
            migrated_count += hot_need_base_pages;
        }

        // Perform cost-benefit analysis before promoting

#if USE_MODEL == (true)
        float cost = CB_MULTIPLIER * (promotion_cost_avg + demotion_cost_avg);
        float benefit = 0.9 * hot_page->score * HF_SAMPLE_PERIOD * latency_diff;
#else
        float cost = CB_MULTIPLIER * (promotion_cost_avg + demotion_cost_avg);
        float benefit = hot_page->score * hot_page->hot_age * HF_SAMPLE_PERIOD * latency_diff;
#endif
        if (benefit < cost)
        {
            // Stop promotions - not profitable
            break;
        }

        // Ensure enough free base pages; demote cold pages if necessary
        while (demote_idx > promote_idx && ((int64_t)free_base_pages) < hot_need_base_pages)
        {
            while (demote_idx > promote_idx && scores[demote_idx].page->pages_in_dram == 0)
            {
                demote_idx--;
                continue;
            }

            std::shared_ptr<page_info> cold_page = scores[demote_idx--].page;
#if USE_MODEL == (true) || LOGGING_RUN == (true)
            if (cold_page->promote_backoff > 0)
            {
                continue;
            }
#endif
            int64_t cold_free_base_pages = cold_page->pages_in_dram;

            // Migration checks for cold page suitability
#if USE_MODEL == (false)
            float hot_page_min_avg = std::min(hot_page->w[0], hot_page->w[2]);
            float cold_page_max_avg = std::max(cold_page->w[0], cold_page->w[2]);

            if (hot_page_min_avg < cold_page_max_avg)
            {
                demote_idx--; // Skip this cold page and try the next candidate
                continue;
            }
#endif

#if USE_MODEL == (true)
            benefit -= cold_page->score * HF_SAMPLE_PERIOD * latency_diff;
#else
            benefit -= cold_page->score * hot_page->hot_age * HF_SAMPLE_PERIOD * latency_diff;
#endif

            if (benefit < cost)
            {
                // Stop promotions - not profitable after considering demotion
                demote_idx = promote_idx;
                break;
            }

            migrated_count += cold_free_base_pages;

            if ((migrated_count / BASE_PAGE_PER_HUGEPAGE) >= max_migrations_cur_interval)
            {
                demote_idx = promote_idx;
                break;
            }

            // std::cout << "[ARMS] Pairing promotion of page " << std::hex << hot_page->va << std::dec << " (need "
            //           << hot_need_base_pages << " base pages) with demotion of page " << std::hex << cold_page->va
            //           << std::dec << " (freeing " << cold_free_base_pages << " base pages)" << std::endl;

            cold_page->num_demotions++;
            demote_list.push_back(cold_page->va);
            free_base_pages += cold_free_base_pages;
        }

        if (free_base_pages < hot_need_base_pages)
        {
            break;
        }

        hot_page->num_promotions++;
        promote_list.push_back(hot_page->va);
        free_base_pages -= hot_need_base_pages;
        migrated_count += hot_need_base_pages;
    }
    if (promote_list.empty() && backup_page)
    {
#if USE_MODEL == (true) || LOGGING_RUN == (true)
        promote_list.push_back(backup_page->va);
#endif
    }

    std::cout << "[ARMS] Scores updated. Promote candidates: " << promote_list.size()
              << ", Demote candidates: " << demote_list.size() << " migrated count: " << migrated_count << std::endl;

    // Perform migrations by pairing promotions and demotions in the same tasks
    if (!promote_list.empty() || !demote_list.empty())
    {
        enqueue_migration_task(promote_list, demote_list);
    }

    // If no migrations happened, decay the cost estimates
    if (promote_list.empty() && demote_list.empty())
    {
        promotion_cost_avg = std::max(promotion_cost_avg / MIGRATION_COST_DECAY_RATE, (double)MIN_PROMOTION_COST);
        demotion_cost_avg = std::max(demotion_cost_avg / MIGRATION_COST_DECAY_RATE, (double)MIN_DEMOTION_COST);
    }
}

// ============================================================================
// Public API
// ============================================================================

struct syscall_event *create_syscall_event_queue()
{
    struct syscall_event *queue =
        (struct syscall_event *)malloc(2 * SYSCALL_EVENT_QUEUE_CAPACITY * sizeof(struct syscall_event));
    if (queue == NULL)
    {
        perror("Failed to allocate memory for syscall event queue\n");
        exit(EXIT_FAILURE);
    }
    return queue;
}

inline void pebs_log_syscall(void *addr, size_t len, enum syscall_type type)
{
    size_t end = atomic_load(&syscall_queue_size);
    size_t start = atomic_load(&syscall_queue_index);

    if (end - start >= SYSCALL_EVENT_QUEUE_CAPACITY)
    {
        // queue full, drop event
        return;
    }

    size_t idx = (atomic_fetch_add(&syscall_queue_size, 1)) % SYSCALL_EVENT_QUEUE_CAPACITY;
    struct syscall_event *queue = atomic_load(&syscall_event_queue);
    queue[idx] = (struct syscall_event){.type = type, .addr = addr, .len = len};
}

void pebs_log_read(void *addr, size_t len)
{
    pebs_log_syscall(addr, len, READ_SYSCALL);
}
void pebs_log_write(void *addr, size_t len)
{
    pebs_log_syscall(addr, len, WRITE_SYSCALL);
}
void pebs_log_malloc(void *addr, size_t len)
{
    pebs_log_syscall(addr, len, MALLOC_SYSCALL);
}

void arms_start_tiering()
{
    std::cout << "[ARMS] Initializing ARMS..." << std::endl;

    std::cout << "[ARMS] USE_MODEL = " << (USE_MODEL ? "true" : "false") << std::endl;
    std::cout << "[ARMS] PRINT_TRAINING_DATA = " << (PRINT_TRAINING_DATA ? "true" : "false") << std::endl;
    std::cout << "[ARMS] PEBS_KSWAPD_INTERVAL_BIG = " << PEBS_KSWAPD_INTERVAL_BIG << std::endl;
    std::cout << "[ARMS] PEBS_KSWAPD_INTERVAL_SMALL = " << PEBS_KSWAPD_INTERVAL_SMALL << std::endl;

    numa_set_preferred(0);

    struct syscall_event *syscall_queue = create_syscall_event_queue();
    syscall_event_queue.store(syscall_queue, std::memory_order_relaxed);

    grp_tracker = create_group_tracker();

    access_log = new class access_log;
    printf("Allocated %f GB for scores_log\n", access_log->get_gb_allocated());

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
    uint64_t memtotal_kb = get_fasttier_total_mem();
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

    // Initialize memory controller BW counters
    setup_imc_bw_counters();

    initialized = true;

    // Start async madvise worker so scans do not block when faulting new pages
    // madvise_thread_running.store(true, std::memory_order_relaxed);
    // pthread_create(&madvise_thread, nullptr, madvise_worker_thread, nullptr);

    // Start scanning and policy threads
    pthread_create(&scan_thread, nullptr, pebs_scan_thread, nullptr);
    pthread_create(&pagemap_scan_thread, nullptr, pagemap_scan_thread_fn, nullptr);

    // Start migration worker threads
    for (size_t i = 0; i < MIGRATION_WORKER_COUNT; i++)
    {
        pthread_create(&migration_threads[i], nullptr, migration_worker, nullptr);
    }

    pthread_create(&policy_thread, nullptr, arms_policy_thread, nullptr);

    std::cout << "[ARMS] Initialization complete." << std::endl;
}

static void write_max_dram_usage_to_file()
{
    if (BASE_PAGE_PER_HUGEPAGE == 0)
    {
        fprintf(stderr, "[ARMS] BASE_PAGE_PER_HUGEPAGE is zero; skipping DRAM usage logging.\n");
        return;
    }

    uint64_t max_base_pages = max_dram_base_pages_seen.load(std::memory_order_relaxed);
    double max_hugepage_equiv = static_cast<double>(max_base_pages) / BASE_PAGE_PER_HUGEPAGE;

    uint64_t samples = dram_samples.load(std::memory_order_relaxed);
    double avg_base_pages = 0.0;
    if (samples > 0)
    {
        avg_base_pages = static_cast<double>(total_dram_base_pages_accum.load(std::memory_order_relaxed)) /
                         static_cast<double>(samples);
    }
    double avg_hugepage_equiv = avg_base_pages / BASE_PAGE_PER_HUGEPAGE;

    int fd = open(MAX_DRAM_HUGEPAGE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("[ARMS] Failed to open max_dram_hugepages.log");
        return;
    }

    if (dprintf(fd, "max_hugepage_equivalents_in_dram=%f\n", max_hugepage_equiv) < 0 ||
        dprintf(fd, "max_base_pages_in_dram=%llu\n", static_cast<unsigned long long>(max_base_pages)) < 0 ||
        dprintf(fd, "avg_hugepage_equivalents_in_dram=%f\n", avg_hugepage_equiv) < 0 ||
        dprintf(fd, "avg_base_pages_in_dram=%f\n", avg_base_pages) < 0 ||
        dprintf(fd, "samples=%llu\n", static_cast<unsigned long long>(samples)) < 0 ||
        dprintf(fd, "hugepage_size_bytes=%llu\n", static_cast<unsigned long long>(HUGEPAGE_SIZE)) < 0)
    {
        perror("[ARMS] Failed to write max_dram_hugepages.log");
    }

    close(fd);
}

void arms_kernel_shutdown()
{
    if (!PRINT_TRAINING_DATA)
    {
        exit(0);
    }

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

    terminated = true;
    printf("[ARMS] Shutting down...\n");

    // Wake workers so they can notice termination
    madvise_cv.notify_all();
    migration_cv.notify_all();

    // Gracefully join background threads
    pthread_join(scan_thread, nullptr);
    pthread_join(pagemap_scan_thread, nullptr);
    pthread_join(policy_thread, nullptr);
    for (size_t i = 0; i < MIGRATION_WORKER_COUNT; i++)
    {
        pthread_join(migration_threads[i], nullptr);
    }

    if (madvise_thread_running.load(std::memory_order_relaxed))
    {
        pthread_join(madvise_thread, nullptr);
        madvise_thread_running.store(false, std::memory_order_relaxed);
    }

    close_perf_events();

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