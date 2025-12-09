/*
 * ARMS Kernel-based Memory Tiering System
 *
 * This is a port of ARMS to use kernel-based page management instead of
 * userspace devdax + userfaultfd. The PEBS-based policy and scoring logic
 * from ARMS is retained, but page management and migration now relies on
 * the kernel via numa_move_pages() system calls.
 *
 */

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <cassert>
#include <pthread.h>
#include <cerrno>
#include <fstream>
#include <cstdint>
#include <string>
#include <set>
#include <numa.h>
#include <numaif.h>
#include <errno.h>
#include <syscall.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <vector>
#include <unordered_map>
#include <fcntl.h>
#include <sched.h>
#include <math.h>
#include <regex>

#include "arms_kernel.h"
#include "timer.h"

#ifdef FAST_MEMORY_SIZE_GB
uint64_t FAST_MEMORY_SIZE = (FAST_MEMORY_SIZE_GB * (1024L * 1024L * 1024L)); // size of fast tier memory in bytes
#else
uint64_t FAST_MEMORY_SIZE = 0;
#endif

static const float w_ewma_alpha[WINDOW_SIZE] = W_EWMA_ALPHA;
static const float hist_bias[WINDOW_SIZE] = HIST_BIAS;
static const float recn_bias[WINDOW_SIZE] = RECN_BIAS;

// Global state
static uint64_t dramsize = FAST_MEMORY_SIZE;

static std::unordered_map<uint64_t, arms_page_info*> pages_map;
static std::mutex pages_map_lock;

static int pagemap_fd = -1;
static pid_t target_pid = 0;

static int perf_fd[PEBS_NPROCS][NPBUFTYPES];
static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];

static volatile bool running = true;
static pthread_t scan_thread;
static pthread_t policy_thread;
static uint32_t policy_thread_interval = PEBS_KSWAPD_INTERVAL_BIG;

static volatile uint64_t global_version = 0;
static volatile uint8_t curr_access_version = 0;
static volatile uint8_t prev_access_version = 1;
static volatile uint8_t curr_window_index = 0;

static const float* active_bias = hist_bias;  // Current bias (hist or recency)
static uint32_t sampling_mode = DEFAULT_SAMPLING;

// Statistics
static uint64_t migrations_up = 0;
static uint64_t migrations_down = 0;
static uint64_t total_samples[NPBUFTYPES] = {0};

// Hot-Change Detection state
static float dram_bw_ewma = 0.0;
static float nvm_bw_ewma = 0.0;
static float nvm_bw_std = 0.0;
static float cusum = 0.0;
static uint32_t time_since_recn = 0;

// Migration cost tracking
static float promotion_cost_avg = MIN_PROMOTION_COST;
static float demotion_cost_avg = MIN_DEMOTION_COST;
static float latency_diff = UNLOADED_NVM_LAT - UNLOADED_DRAM_LAT;

// Forward declarations
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                           int cpu, int group_fd, unsigned long flags);
static void setup_perf_events();
static void update_scores_and_migrate();
static int migrate_pages_to_node(std::vector<uint64_t>& vas, int target_node);
static void scan_process_pages();

// ============================================================================
// PERF Event Setup
// ============================================================================

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                           int cpu, int group_fd, unsigned long flags) {
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

static uint32_t get_imc_bw_counter_offset(enum imc_bw_counters e) {
  switch(e) {
    case DRAM_READS:  return PCM_SERVER_IMC_DRAM_READS;
    case DRAM_WRITES: return PCM_SERVER_IMC_DRAM_WRITES;
    case NVM_READS:   return PCM_SERVER_IMC_PMM_READS;
    case NVM_WRITES:  return PCM_SERVER_IMC_PMM_WRITES;
    default: assert(!"Unknown IMC counter");
  }
}

uint64_t measure_bw(int tier)
{
  int i, j;
  uint64_t cur_ctr_val = 0;
  uint64_t cur_bw = 0;

  if (tier == 0) { // DRAM
    for (i=0; i<NUM_IMC; i++) {
      for (j=0; j<2; j++) {
        cur_ctr_val = *((uint64_t *)(imc_mmio_addr[i] + get_imc_bw_counter_offset(j)));
        cur_bw     += cur_ctr_val - prev_ctr_val[0][i][j];
        prev_ctr_val[tier][i][j] = cur_ctr_val;
      }
    }
  } else if (tier == 1) { // NVM
    for (i=0; i<NUM_IMC; i++) {
      for (j=2; j<4; j++) {
        cur_ctr_val = *((uint64_t *)(imc_mmio_addr[i] + get_imc_bw_counter_offset(j)));
        cur_bw     += cur_ctr_val - prev_ctr_val[1][i][j];
        prev_ctr_val[tier][i][j] = cur_ctr_val;
      }
    }
  }

  return cur_bw;
}

static int setup_imc_bw_counters() {
  mem_fd = open("/dev/mem", O_RDONLY);
  if (mem_fd == -1) {
    perror("open");
    return -1;
  }

  for (int i = 0; i < NUM_IMC; i++) {
    // Base address of each iMC increases by 0x80000
    imc_mmio_addr[i] = (char *)libc_mmap(NULL, PCM_SERVER_IMC_MMAP_SIZE, PROT_READ, MAP_SHARED, mem_fd, IMC_BASE_ADDR + (0x80000 * i));
    if (imc_mmio_addr[i] == MAP_FAILED) {
      perror("mmap");
      return -1;
    }
  }

  // Measure the bandwidth once to get the initial values
  for (int i = 0; i < NUM_TIERS; i++) {
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

  for (int j = 0; j < NUM_EVENTS; j++) {
    for (int k = 0; k < NUM_IMC; k++) {
      if (read(bw_fds[tier][j][k], &cur_val, sizeof(cur_val)) == -1) {
        std::cerr << "ERROR: Failed to read perf event for BW monitoring" << std::endl;
        exit(1);
      }
      cur_bw                 += cur_val - prev_bw_val[tier][j][k];
      prev_bw_val[tier][j][k] = cur_val;
    }
  }

  return cur_bw;
}
void open_perf_events()
{
  int fd;
  struct perf_event_attr pe;

  for (unsigned long i = 0; i < NUM_TIERS; i++) {
    for (unsigned long j = 0; j < NUM_EVENTS; j++) {
      for (unsigned long k = 0; k < NUM_IMC; k++) {
        memset(&pe, 0, sizeof(pe));
        pe.type = i + 12; // TODO: read type from /sys/devices/uncore_imc_x/type
        pe.size = sizeof(pe);
        pe.disabled = 1;
        pe.inherit = 1;
        pe.config = (j == 0) ? 0x304:0xC04;

        fd = perf_event_open(&pe, -1, 10, -1, 0);
        if (fd == -1) {
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
  for (int i = 0; i < NUM_TIERS; i++) {
    for (int j = 0; j < NUM_EVENTS; j++) {
      for (int k = 0; k < NUM_IMC; k++) {
        ioctl(bw_fds[i][j][k], PERF_EVENT_IOC_RESET, 0);
        ioctl(bw_fds[i][j][k], PERF_EVENT_IOC_ENABLE, 0);
      }
    }
    measure_bw(i); // Measure once to get the initial values
  }

  return 0;
}
#endif

static struct perf_event_mmap_page* perf_setup(__u64 config, __u64 config1, __u16 cpu, __u16 type)
{
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));

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
  if (perf_fd[cpu][type] == -1) {
    perror("perf_event_open");
    fprintf(stderr, "Failed to open perf event for CPU %d, type %d\n", cpu, type);
  }
  assert(perf_fd[cpu][type] != -1);

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  struct perf_event_mmap_page *p = (struct perf_event_mmap_page *)mmap(
      NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd[cpu][type], 0);
  if (p == MAP_FAILED) {
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

static void setup_perf_events() {
  std::cout << "[ARMS] Setting up PEBS counters..." << std::endl;

  for (int i = 0; i < PEBS_NPROCS; i++) {
#ifdef C220G5
    // Skip node 1 cores on C220G5 (cores 10-19 are on NUMA node 1)
    if (i >= 10 && i < 20) continue;
#endif
    perf_page[i][DRAMREAD] = perf_setup(L3_LOAD_MISS_LOCAL, 0, i, DRAMREAD);
    perf_page[i][NVMREAD]  = perf_setup(L3_LOAD_MISS_REMOTE, 0, i, NVMREAD);
    perf_page[i][WRITE]    = perf_setup(0x82d0, 0, i, WRITE);  // MEM_INST_RETIRED.ALL_STORES

    if (!perf_page[i][DRAMREAD] || !perf_page[i][NVMREAD]) {
      fprintf(stderr, "[ARMS] Failed to setup perf events for CPU %d\n", i);
    }
  }

  std::cout << "[ARMS] PEBS counters setup complete." << std::endl;
}

static void close_perf_events() {
  std::cout << "[ARMS] Closing PEBS counters..." << std::endl;

  for (int i = 0; i < PEBS_NPROCS; i++) {
    for (int j = 0; j < NPBUFTYPES; j++) {
      if (perf_page[i][j]) {
        ioctl(perf_fd[i][j], PERF_EVENT_IOC_DISABLE, 0);
        munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
        close(perf_fd[i][j]);
        perf_page[i][j] = nullptr;
      }
    }
  }
}

// Returns amount of free memory in node 0 in KB
uint64_t get_fasttier_free_mem() {
  std::string path = "/sys/devices/system/node/node0/meminfo";
  std::ifstream file(path);
  if (!file) {
      std::cerr << "Failed to open " << path << std::endl;
      return -1;
  }
  std::string line;
  std::regex regex(R"(MemFree:\s+(\d+) kB)");
  while (getline(file, line)) {
      std::smatch match;
      if (std::regex_search(line, match, regex)) {
          return std::stoull(match[1].str());
      }
  }
  return -1;
}

// ============================================================================
// PEBS Scanning Thread
// ============================================================================

static void* pebs_scan_thread(void *arg) {
  (void)arg;
  // Set thread affinity if needed
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(SCANNING_THREAD_CPU, &cpuset);  // Use a dedicated core
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  for (;;) {
    for (int cpu = 0; cpu < PEBS_NPROCS; cpu++) {
    #ifdef C220G5
      if (cpu >= 10 && cpu < 20) continue;
    #endif
      for (int type = 0; type < NPBUFTYPES; type++) {
        struct perf_event_mmap_page *header = perf_page[cpu][type];
        char *pbuf = (char *)header + header->data_offset;
        __sync_synchronize();

        if(header->data_head == header->data_tail) {
          continue;
        }

        struct perf_event_header *ph = (struct perf_event_header *)(pbuf + (header->data_tail % header->data_size));
        struct perf_sample* ps;

        switch (ph->type) {
          case PERF_RECORD_SAMPLE:
            ps = (struct perf_sample *)(ph);
            assert(ps != nullptr);

            if (ps->addr != 0) {
              uint64_t page_va = ps->addr & HUGE_PFN_MASK;  // Align to page

              if (page_va != 0) {
                auto it = pages_map.find(page_va);
                if (it == pages_map.end()) {
                  // New page discovered
                  // arms_page_info* page = new arms_page_info();
                  // page->va = page_va;
                  // pages_map[page_va] = page;
                  // it = pages_map.find(page_va);
                  // std::cout << "[ARMS] Discovered new page: VA = 0x" << std::hex << page_va << std::dec << std::endl;
                  break;
                }
                assert (it != pages_map.end());

                // Increment access count
                arms_page_info* page = it->second;
                page->accesses[type][curr_access_version]++;
                total_samples[type]++;
              }
            }
            break;

          case PERF_RECORD_THROTTLE:
          case PERF_RECORD_UNTHROTTLE:
            // Ignore for now
            break;
          default:
            std::cerr << "[ARMS] ERROR: Unknown perf_event type " << ph->type << std::endl;
            break;
        }

        header->data_tail += ph->size;
      }
    }
  }

  return nullptr;
}

static void change_sampling_frequency()
{
  int ret = 0;
  uint64_t sample_period = DEFAULT_SAMPLE_PERIOD;

  if (sampling_mode == HF_SAMPLING) {
    sample_period = HF_SAMPLE_PERIOD;
  }

  for (int i = 0; i < PEBS_NPROCS; i++) {
#if defined C220G5
      if (i >= 10 && i < 20) {
      continue;
      }
#endif
    for (int j = 0; j < NPBUFTYPES; j++) {
      ret = ioctl(perf_fd[i][j], PERF_EVENT_IOC_PERIOD, &sample_period);
      if (ret != 0) {
        perror("PERF_EVENT_IOC_PERIOD");
      }
    }
  }
}

// ============================================================================
// Page Discovery via /proc/self/pagemap
// ============================================================================

static void scan_process_pages() {
  if (pagemap_fd < 0) return;

  // Read /proc/self/maps to get VMA ranges
  std::ifstream maps_file("/proc/self/maps");
  if (!maps_file.is_open()) {
    perror("Failed to open /proc/self/maps");
    return;
  }

  std::string line;
  while (std::getline(maps_file, line)) {
    uint64_t start_addr, end_addr;
    char perms[5];

    // Parse the maps line
    if (sscanf(line.c_str(), "%lx-%lx %4s", &start_addr, &end_addr, perms) != 3) {
      continue;
    }

    // Skip non-readable or non-writable regions
    if (perms[0] != 'r' || perms[1] != 'w') continue;

    // Skip kernel regions
    if (start_addr >= 0x7fffffffffff) continue;

    // Scan this VMA range
    for (uint64_t va = start_addr; va < end_addr; va += PAGE_SIZE) {
      // Align to page size
      va = va & HUGE_PFN_MASK;
      uint64_t pagemap_index = (va / 4096) * sizeof(uint64_t);
      uint64_t pagemap_entry;

      ssize_t ret = pread(pagemap_fd, &pagemap_entry, sizeof(pagemap_entry), pagemap_index);
      if (ret != sizeof(pagemap_entry)) continue;

      uint64_t pfn = pagemap_entry & 0x7fffffffffffff;
      bool present = (pagemap_entry >> 63) & 1;

      if (present && pfn > 0) {
        std::lock_guard<std::mutex> lock(pages_map_lock);

        if (pages_map.find(va) == pages_map.end()) {
          arms_page_info* page = new arms_page_info();
          page->va = va;

          // Determine which node this page is on
          int status = -1;
          void* addr = (void*)va;
          numa_move_pages(0, 1, &addr, nullptr, &status, 0);
          page->in_dram = (status == FAST_TIER);

          pages_map[va] = page;
        }
      }
    }
  }

  std::cout << "[ARMS] Number of process pages tracked: " << pages_map.size() << std::endl;

  maps_file.close();
}


// ============================================================================
// Hot-Change Detection
// ============================================================================

static void detect_hot_change() {
  float cur_dram_bw, cur_nvm_bw;

  // Measure current bandwidth
  cur_dram_bw = (float)measure_bw(0) / (1024.0 * 1024.0 * 1024.0); // GB/s
  cur_nvm_bw  = (float)measure_bw(1) / (1024.0 * 1024.0 * 1024.0); // GB/s

  // Update EWMA of bandwidth
  dram_bw_ewma = (1 - HCD_EWMA_ALPHA) * dram_bw_ewma + HCD_EWMA_ALPHA * cur_dram_bw;
  nvm_bw_ewma = (1 - HCD_EWMA_ALPHA) * nvm_bw_ewma + HCD_EWMA_ALPHA * cur_nvm_bw;
  nvm_bw_std = (1 - HCD_STD_ALPHA) * nvm_bw_std + HCD_STD_ALPHA * (cur_nvm_bw - nvm_bw_ewma) * (cur_nvm_bw - nvm_bw_ewma);
  nvm_bw_std = sqrtf(fmaxf(nvm_bw_std, 1e-12f)); // Avoid stddev of 0

  // Scale drift and threshold based on stddev
  // This allows the algorithm to adapt to different levels of noise in the measurements
  float drift = HCD_PH_DRIFT * nvm_bw_std;
  float threshold = HCD_PH_THRESHOLD * nvm_bw_std;

  // CUSUM (Page-Hinkley test) for phase change detection
  cusum += ((cur_nvm_bw - nvm_bw_ewma) - drift);
  cusum = fmaxf(cusum, 0.0f); // Only interested in positive deviations
  if (cusum > threshold) {
    // Switch to recency bias if currently using history bias and NVM bandwidth is high
    if (active_bias == hist_bias && cur_nvm_bw > HCD_RECN_MIN_NVM_BW) {
      active_bias = recn_bias;
      time_since_recn = 0;
      std::cout << "[ARMS-HCD] Phase change detected! Switching to RECENCY bias "
                << "(NVM BW: " << cur_nvm_bw << " GB/s, CUSUM: " << cusum << ")" << std::endl;
    }
    cusum = 0;
  } else if (active_bias == recn_bias) {
    // Check if we should switch back to history bias
    time_since_recn++;
    if (time_since_recn >= HCD_RECN_MAX_PERIODS && cusum <= 0) {
      active_bias = hist_bias;
      time_since_recn = 0;
      std::cout << "[ARMS-HCD] Stable phase detected. Switching back to HISTORY bias" << std::endl;
    }
  }
}

// ============================================================================
// Scoring and Migration Policy
// ============================================================================

static void update_window(arms_page_info* page) {
  uint32_t accesses = page->accesses[DRAMREAD][prev_access_version] +
                      page->accesses[NVMREAD][prev_access_version] +
                      (NVM_WRITES_WEIGHT * page->accesses[WRITE][prev_access_version]);

  if (sampling_mode == DEFAULT_SAMPLING) {
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
      page->w[i] = (1. - w_ewma_alpha[i]) * page->w[i] +
          (w_ewma_alpha[i] * ((DEFAULT_SAMPLE_PERIOD/HF_SAMPLE_PERIOD) * accesses)); // We maintain counters in high-fidelity
    }
  } else if (sampling_mode == HF_SAMPLING) {
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
      page->w[i] = (1. - w_ewma_alpha[i]) * page->w[i] + (w_ewma_alpha[i] * accesses);
    }
  }
}

static float compute_score(const arms_page_info* page) {
  float score = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    score += (active_bias[i] * page->w[i]);
  }
  return score;
}

static int score_compare(const void *a, const void *b) {
  const score_entry *ea = (const score_entry *)a;
  const score_entry *eb = (const score_entry *)b;
  return (ea->score > eb->score) ? -1 : (ea->score < eb->score);
}

static int migrate_pages_to_node(std::vector<uint64_t>& vas, int target_node) {
  if (vas.empty()) return 0;

  auto start = std::chrono::high_resolution_clock::now();

  size_t num_pages = vas.size();
  std::vector<void*> pages(num_pages);
  std::vector<int> nodes(num_pages, target_node);
  std::vector<int> status(num_pages, -1);

  for (size_t i = 0; i < num_pages; i++) {
    pages[i] = (void*)vas[i];
  }

  int ret = numa_move_pages(0, num_pages, pages.data(), nodes.data(),
                           status.data(), MPOL_MF_MOVE_ALL);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  float time_per_page_us = (float)duration_us / (float)num_pages;

  // Update migration cost estimates
  if (target_node == FAST_TIER) {
    // Promotion
    promotion_cost_avg = MIGRATION_COST_ALPHA * time_per_page_us +
                        (1 - MIGRATION_COST_ALPHA) * promotion_cost_avg;
  } else {
    // Demotion
    demotion_cost_avg = MIGRATION_COST_ALPHA * time_per_page_us +
                       (1 - MIGRATION_COST_ALPHA) * demotion_cost_avg;
  }

  if (ret != 0) {
    perror("numa_move_pages");
    return -1;
  }

  return num_pages;
}

static void update_scores_and_migrate() {
  std::vector<score_entry> scores;

  {
    std::lock_guard<std::mutex> lock(pages_map_lock);

    // Update scores for all pages
    for (auto& kv : pages_map) {
      arms_page_info* page = kv.second;

      update_window(page);
      page->prev_score = page->score;
      page->score = compute_score(page);
      // Clear access counts for next interval
      for (int i = 0; i < NPBUFTYPES; i++) {
        page->accesses[i][prev_access_version] = 0;
      }

      score_entry entry;
      entry.page = page;
      entry.score = page->score;
      scores.push_back(entry);
    }
  }

  if (scores.empty()) return;

  // Sort by score (descending)
  qsort(scores.data(), scores.size(), sizeof(score_entry), score_compare);

  // Update the hot age of pages
  uint64_t i;
  for (i = 0; i < (dramsize/PAGE_SIZE) && i < scores.size(); i++) {
    arms_page_info* page = scores[i].page;
    if (page->score != 0) {
      page->hot_age++;
      if (page->hot_age > 1 && (page->score >= page->prev_score)) {
        page->can_promote = true;
      }
    }
  }
  for (; i < scores.size(); i++) {
    arms_page_info* page = scores[i].page;
    page->hot_age = 0;
    page->can_promote = false;
  }

  // Print the max and min scores for debugging
  std::cout << "[ARMS] Number of tracked pages: " << scores.size()
            << ", Max score: " << scores[0].score
            << ", Min score: " << scores[scores.size() - 1].score << std::endl;

  // Identify promotion and demotion candidates
  std::vector<uint64_t> promote_list;
  std::vector<uint64_t> demote_list;

  uint64_t promote_idx = 0;
  uint64_t demote_idx = scores.size() - 1;
  size_t migrated_count = 0;

  uint32_t max_migrations_cur_interval = ((policy_thread_interval) / (promotion_cost_avg + demotion_cost_avg));

  uint64_t fasttier_free_kb = get_fasttier_free_mem();
  uint64_t fasttier_free_pages = fasttier_free_kb / (PAGE_SIZE / 1024);

  // Calculate current DRAM usage and enforce watermark
  uint64_t max_dram_pages = dramsize / PAGE_SIZE;
  uint64_t current_dram_pages = 0;
  for (const auto& entry : scores) {
    if (entry.page->in_dram) {
      current_dram_pages++;
    }
  }

  std::cout << "[ARMS] Current DRAM pages: " << current_dram_pages
            << ", Max DRAM pages: " << max_dram_pages
            << ", Free DRAM pages: " << fasttier_free_pages << std::endl;

  // Enforce watermark: demote excess pages from the coldest end
  if (dramsize > 0) {
    while (current_dram_pages > max_dram_pages && demote_idx > 0) {
      if (migrated_count >= max_migrations_cur_interval) break;

      if (scores[demote_idx].page->in_dram) {
        demote_list.push_back(scores[demote_idx].page->va);
        current_dram_pages--;
        migrated_count++;
      }
      demote_idx--;
    }
  }

  // Try to promote hot NVM pages, demoting cold DRAM pages if necessary
  while (promote_idx < (dramsize/PAGE_SIZE) && promote_idx < demote_idx) {
    if (migrated_count >= max_migrations_cur_interval) {
      break; // Reached max migrations for this interval
    }

    // Find the hottest page that needs promotion
    while (promote_idx < (dramsize/PAGE_SIZE) && scores[promote_idx].page->in_dram) {
      promote_idx++;
    }
    if (promote_idx >= demote_idx || promote_idx > (dramsize/PAGE_SIZE)) break;

    arms_page_info* hot_page = scores[promote_idx].page;
    if (!hot_page->can_promote) {
      promote_idx++;
      continue;
    }
    if (hot_page->score == 0) {
      // No more hot pages to promote
      break;
    }

    // Perform cost-benefit analysis before promoting
    float cost = CB_MULTIPLIER * promotion_cost_avg;
    float benefit = hot_page->score * hot_page->hot_age * HF_SAMPLE_PERIOD * latency_diff;
    if (benefit < cost) {
      // Stop promotions - not profitable
      break;
    }

    // Check if we can promote without demoting
    bool system_has_space = promote_list.size() < fasttier_free_pages;
    bool app_has_space = (current_dram_pages + promote_list.size() < max_dram_pages);

    if (system_has_space && app_has_space) {
      // There is enough free space in DRAM - no need to demote
      promote_list.push_back(hot_page->va);
      promote_idx++;
      migrated_count++;
      continue;
    }


    // Find next cold page in DRAM
    while (demote_idx > promote_idx && !scores[demote_idx].page->in_dram) {
      demote_idx--;
    }
    if (demote_idx <= promote_idx || demote_idx <= (dramsize/PAGE_SIZE)) break;

    arms_page_info* cold_page = scores[demote_idx].page;


    // Migration checks
    // 1. Hot page should have all EWMAs greater than the max EWMA of cold page
    float hot_page_min_avg = hot_page->w[0];
    float cold_page_max_avg = cold_page->w[0];
    for (int i = 1; i < WINDOW_SIZE; i++) {
      if (hot_page->w[i] < hot_page_min_avg) {
        hot_page_min_avg = hot_page->w[i];
      }
      if (cold_page->w[i] > cold_page_max_avg) {
        cold_page_max_avg = cold_page->w[i];
      }
    }

    if (hot_page_min_avg < cold_page_max_avg) {
      // Stop migrations - not worth it anymore
      break;
    }

    // 2. Cost-benefit analysis
    cost = CB_MULTIPLIER * (promotion_cost_avg + demotion_cost_avg);
    benefit = (hot_page->score - cold_page->score) * hot_page->hot_age *
                    HF_SAMPLE_PERIOD * latency_diff;

    if (benefit < cost) {
      // Stop migrations - not profitable
      break;
    }

    // Both checks passed - add to migration lists
    promote_list.push_back(hot_page->va);
    demote_list.push_back(cold_page->va);

    promote_idx++;
    demote_idx--;
    migrated_count++;
  }

  std::cout << "[ARMS] Scores updated. Promote candidates: " << promote_list.size()
            << ", Demote candidates: " << demote_list.size() << std::endl;

  // Perform migrations
  if (!demote_list.empty()) {
    int ret = migrate_pages_to_node(demote_list, SLOW_TIER);
    if (ret > 0) {
      migrations_down += ret;
      std::cout << "[ARMS] Demoted " << ret << " pages to NVM" << std::endl;

      // Update page state
      std::lock_guard<std::mutex> lock(pages_map_lock);
      for (uint64_t va : demote_list) {
        auto it = pages_map.find(va);
        if (it != pages_map.end()) {
          it->second->in_dram = false;
        }
      }
    }
  }

  if (!promote_list.empty()) {
    int ret = migrate_pages_to_node(promote_list, FAST_TIER);
    if (ret > 0) {
      migrations_up += ret;
      std::cout << "[ARMS] Promoted " << ret << " pages to DRAM" << std::endl;

      // Update page state
      std::lock_guard<std::mutex> lock(pages_map_lock);
      for (uint64_t va : promote_list) {
        auto it = pages_map.find(va);
        if (it != pages_map.end()) {
          it->second->in_dram = true;
        }
      }
    }
  }

  // If no migrations happened, decay the cost estimates
  if (promote_list.empty() && demote_list.empty()) {
    promotion_cost_avg /= MIGRATION_COST_DECAY_RATE;
    demotion_cost_avg /= MIGRATION_COST_DECAY_RATE;
    if (promotion_cost_avg < MIN_PROMOTION_COST) {
      promotion_cost_avg = MIN_PROMOTION_COST;
    }
    if (demotion_cost_avg < MIN_DEMOTION_COST) {
      demotion_cost_avg = MIN_DEMOTION_COST;
    }
  }
}

// ============================================================================
// Policy Thread
// ============================================================================

static void* arms_policy_thread(void *arg) {
  (void)arg;
  // Set thread affinity
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(POLICY_THREAD_CPU, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  struct ptimer loop_timer;
  ptimer_init(&loop_timer, "Policy loop timer");

  for(;;) {
    ptimer_start(&loop_timer);
    curr_window_index = global_version % WINDOW_SIZE;
    global_version++;
    prev_access_version = curr_access_version;
    curr_access_version = 1 - curr_access_version;
    __sync_synchronize();

    if (global_version % (1000000 / policy_thread_interval) == 0) {
      detect_hot_change();

      // Calculate latency diff between DRAM and NVM
      float dram_lat = UNLOADED_DRAM_LAT;
      float nvm_lat = UNLOADED_NVM_LAT;
      if (dram_bw_ewma > DRAM_BW_KNEE) {
        dram_lat += (dram_bw_ewma - DRAM_BW_KNEE) * DRAM_BW_SLOPE;
      }
      if (nvm_bw_ewma > NVM_RD_BW_KNEE) {
        nvm_lat += (nvm_bw_ewma - NVM_RD_BW_KNEE) * NVM_BW_SLOPE;
      }
      latency_diff = nvm_lat - dram_lat;
    }

    // Periodically scan for new pages
    static int scan_counter = 0;
    if (++scan_counter >= 10) {  // Every 5 seconds
      scan_process_pages();
      scan_counter = 0;
    }

    update_scores_and_migrate();

    // Update sampling frequency if hotset change detected
    if ((active_bias == recn_bias) && sampling_mode != HF_SAMPLING) {
      change_sampling_frequency();
      sampling_mode = HF_SAMPLING;
    } else if ((active_bias == hist_bias) && sampling_mode != DEFAULT_SAMPLING) {
      change_sampling_frequency();
      sampling_mode = DEFAULT_SAMPLING;
    }

    // Print total samples
    std::cout << "[ARMS] Total samples - DRAMREAD: " << total_samples[DRAMREAD]
              << ", NVMREAD: " << total_samples[NVMREAD]
              << ", WRITE: " << total_samples[WRITE] << std::endl;
    total_samples[DRAMREAD] = total_samples[NVMREAD] = total_samples[WRITE] = 0;

    ptimer_stop_and_print(&loop_timer);
    double elapsed_us = loop_timer.elapsed_us;

    if (elapsed_us < policy_thread_interval) {
      usleep(policy_thread_interval - elapsed_us);
    }
  }

  return nullptr;
}

// ============================================================================
// Public API
// ============================================================================

void arms_start_tiering() {
  std::cout << "[ARMS] Initializing ARMS..." << std::endl;

  // Open pagemap
  target_pid = getpid();
  char pagemap_path[256];
  snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", target_pid);
  pagemap_fd = open(pagemap_path, O_RDONLY);
  if (pagemap_fd < 0) {
    perror("Failed to open pagemap");
    return;
  }

  // Check NUMA configuration
  if (numa_available() < 0) {
    fprintf(stderr, "[ARMS] NUMA not available\n");
    close(pagemap_fd);
    return;
  }

  std::cout << "[ARMS] NUMA nodes available: " << numa_max_node() + 1 << std::endl;

  // Setup PEBS
  setup_perf_events();

  // Initialize memory controller BW counters
  setup_imc_bw_counters();

  // Start scanning and policy threads
  pthread_create(&scan_thread, nullptr, pebs_scan_thread, nullptr);
  pthread_create(&policy_thread, nullptr, arms_policy_thread, nullptr);

  std::cout << "[ARMS] Initialization complete." << std::endl;
}

void arms_kernel_shutdown() {
  std::cout << "[ARMS] Shutting down..." << std::endl;

  pthread_join(scan_thread, nullptr);
  pthread_join(policy_thread, nullptr);

  close_perf_events();

  if (pagemap_fd >= 0) {
    close(pagemap_fd);
  }

  // Clean up page tracking
  {
    std::lock_guard<std::mutex> lock(pages_map_lock);
    for (auto& kv : pages_map) {
      delete kv.second;
    }
    pages_map.clear();
  }

  std::cout << "[ARMS] Shutdown complete." << std::endl;
}