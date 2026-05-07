#define _GNU_SOURCE
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <float.h>
#include <fcntl.h>
#include <math.h>

#include "arms.h"
#include "pebs.h"
#include "timer.h"
#include "spsc-ring.h"

#include "khash.h"
#include "kdq.h"
#include "kbtree.h"

// Hash table for ARMS-handled pages
KHASH_MAP_INIT_INT64(kPagesMap, struct arms_page*)
khash_t(kPagesMap) *pages;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

khash_t(kPagesMap) *pages_map;

struct score_entry *scores;

static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;

// Pages to be freed/added in the next interval
typedef struct mod_page {
  struct arms_page* page;
  bool free;
} mod_page_t;
static_assert(sizeof(mod_page_t) == 16);

KDQ_INIT(mod_page_t);

static kdq_t(mod_page_t) *mod_page_dq;
static pthread_mutex_t mod_page_dq_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t policy_thread_period = PEBS_KSWAPD_INTERVAL_BIG;

volatile uint64_t global_version = 0;
volatile uint8_t curr_access_version = 0;
volatile uint8_t prev_access_version; // = 1 - curr_access_version

volatile uint8_t curr_window_index = 0;
volatile uint8_t prev_window_version;

static float local_mlp = MLP_MIN;  // per-tier MLP, updated by measure_tor_mlp()
static float remote_mlp = MLP_MIN; // per-tier MLP, updated by measure_tor_mlp()

void pebs_print_config();

// PAC CSV logging
static FILE    *pac_log_fp    = NULL;
static uint64_t pac_log_epoch = 0;

static void pac_log_init(void)
{
  const char *path = getenv("PAC_LOG_FILE");
  if (!path) path = "pac_log.csv";
  pac_log_fp = fopen(path, "w");
  if (!pac_log_fp) {
    LOG_ERROR("WARNING: Cannot open PAC log '%s': %s\n", path, strerror(errno));
    return;
  }
  // Long-format CSV: pivot in pandas with df.pivot(index='va', columns='epoch', values='pac')
  fprintf(pac_log_fp, "epoch,va,pac,delta_pac,in_dram,mlp\n");
  fflush(pac_log_fp);
  LOG_REPORT("PAC logging to %s\n", path);
}

static void pac_log_write(struct score_entry *scores, size_t cnt)
{
  if (!pac_log_fp) return;
  for (size_t i = 0; i < cnt; i++) {
    struct arms_page *p = scores[i].page;
    if (p->score - p->prev_score == 0) continue; // Skip zero PAC pages for log size (optional)
    fprintf(pac_log_fp, "%lu,0x%lx,%.6f,%d,%.4f\n",
            pac_log_epoch, p->va,
            p->score - p->prev_score,
            (int)p->in_dram, p->in_dram?local_mlp:remote_mlp);
  }
  fflush(pac_log_fp);
  pac_log_epoch++;
}

// Double-buffered total NVM sample count per window (mirrors page->accesses versioning)
static volatile uint64_t window_nvmread_cnt[2] = {0, 0};
static volatile uint64_t window_dramread_cnt[2] = {0, 0};

uint64_t arms_pages_cnt = 0;
uint64_t other_pages_cnt = 0;
uint64_t total_pages_cnt = 0;
uint64_t zero_pages_cnt = 0;
uint64_t throttle_cnt = 0;
uint64_t unthrottle_cnt = 0;
uint64_t cools = 0;

uint32_t sampling_mode = DEFAULT_SAMPLING;

static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];
int pfd[PEBS_NPROCS][NPBUFTYPES];

volatile bool need_cool_dram = false;
volatile bool need_cool_nvm = false;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
  int cpu, int group_fd, unsigned long flags)
{
int ret;

ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
  group_fd, flags);
return ret;
}

// ---- TOR_OCCUPANCY counters for per-tier MLP (PAC scoring) ----
// MLP = Σ1/Σ2 per the PACT paper:
//   Σ1 = TOR_OCCUPANCY (sum of occupancy each cycle, thresh=0)
//   Σ2 = TOR active cycles (cycles where occupancy ≥ 1, thresh=1 at config:24-31)

enum cha_evt {
    CHA_OCC_LOC = 0,  /* TOR occupancy,     local  DRAM (numerator)   */
    CHA_ACT_LOC,      /* TOR active cycles, local  DRAM (denominator) */
    CHA_OCC_REM,      /* TOR occupancy,     remote DRAM (numerator)   */
    CHA_ACT_REM,      /* TOR active cycles, remote DRAM (denominator) */
    CHA_EVT_COUNT
};

static uint64_t tor_occ_fd[NUM_TIERS][TOR_CHA_MAX][CHA_EVT_COUNT];   // Sigma1: occupancy sum
static uint64_t tor_act_fd[NUM_TIERS][TOR_CHA_MAX][CHA_EVT_COUNT];   // Sigma2: active cycles (thresh=1)
static uint64_t prev_tor_occ[NUM_TIERS][TOR_CHA_MAX][CHA_EVT_COUNT];
static uint64_t prev_tor_act[NUM_TIERS][TOR_CHA_MAX][CHA_EVT_COUNT];
static int      tor_cha_count = 0;

static void setup_tor_counters(void)
{
  char path[128];
  char buf[32];
  struct perf_event_attr pe;

  for (int s=0; s < NUM_TIERS; s++) {
    tor_cha_count = 0;
    for (int i = 0; i < TOR_CHA_MAX; i++) {
      int fd, n;
      snprintf(path, sizeof(path),
              "/sys/bus/event_source/devices/uncore_cha_%d/type", i);
      fd = open(path, O_RDONLY);
      if (fd == -1) break;
      n = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if (n <= 0) break;
      buf[n] = '\0';
      uint32_t pmu_type = (uint32_t)strtoul(buf, NULL, 10);

      // Sigma1: occupancy sum (thresh=0, default)
      memset(&pe, 0, sizeof(pe));
      pe.type = pmu_type;
      pe.size = sizeof(pe);
      pe.config = ((uint64_t)TOR_OCC_UMASK << 8) | TOR_OCC_EVENT_CODE;
      pe.config1 = TOR_OCC_FILTER_LOCAL;
      tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC] = perf_event_open(&pe, -1, s==0?0:10, -1, 0);

      pe.config1 = TOR_OCC_FILTER_REMOTE;
      tor_occ_fd[s][tor_cha_count][CHA_OCC_REM] = perf_event_open(&pe, -1, s==0?0:10, -1, 0);

      // Sigma2: Occupany active cycles (thresh=1 occupies config:24-31)
      pe.config = ((uint64_t)TOR_OCC_UMASK << 8) | TOR_OCC_EVENT_CODE | (1ULL << 24); // thresh=1
      pe.config1 = TOR_OCC_FILTER_LOCAL;
      tor_act_fd[s][tor_cha_count][CHA_ACT_LOC] = perf_event_open(&pe, -1, s==0?0:10, -1, 0);

      pe.config1 = TOR_OCC_FILTER_REMOTE;
      tor_act_fd[s][tor_cha_count][CHA_ACT_REM] = perf_event_open(&pe, -1, s==0?0:10, -1, 0);

      if (tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC] == -1 || tor_act_fd[s][tor_cha_count][CHA_ACT_LOC] == -1) {
        LOG_ERROR("WARNING: TOR open failed for CHA %d (errno %d)\n", i, errno);
        if (tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC] > 0) close(tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC]);
        if (tor_act_fd[s][tor_cha_count][CHA_ACT_LOC] > 0) close(tor_act_fd[s][tor_cha_count][CHA_ACT_LOC]);
        continue;
      }

      ioctl(tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC], PERF_EVENT_IOC_RESET, 0);
      ioctl(tor_occ_fd[s][tor_cha_count][CHA_OCC_LOC], PERF_EVENT_IOC_ENABLE, 0);
      ioctl(tor_act_fd[s][tor_cha_count][CHA_ACT_LOC], PERF_EVENT_IOC_RESET, 0); 
      ioctl(tor_act_fd[s][tor_cha_count][CHA_ACT_LOC], PERF_EVENT_IOC_ENABLE, 0);


      ioctl(tor_occ_fd[s][tor_cha_count][CHA_OCC_REM], PERF_EVENT_IOC_RESET, 0);
      ioctl(tor_occ_fd[s][tor_cha_count][CHA_OCC_REM], PERF_EVENT_IOC_ENABLE, 0);
      ioctl(tor_act_fd[s][tor_cha_count][CHA_ACT_REM], PERF_EVENT_IOC_RESET, 0); 
      ioctl(tor_act_fd[s][tor_cha_count][CHA_ACT_REM], PERF_EVENT_IOC_ENABLE, 0);
      tor_cha_count++;
    }

    if (tor_cha_count == 0) {
      LOG_ERROR("WARNING: No TOR counters opened; mlp will stay at %.3f\n", MLP_MIN);
      return;
    }

    for (int i = 0; i < tor_cha_count; i++) {
      ssize_t r __attribute__((unused));
      r = read(tor_occ_fd[s][i][CHA_OCC_LOC], &prev_tor_occ[s][i][CHA_OCC_LOC], sizeof(uint64_t));
      r = read(tor_act_fd[s][i][CHA_ACT_LOC], &prev_tor_act[s][i][CHA_ACT_LOC], sizeof(uint64_t));
      r = read(tor_occ_fd[s][i][CHA_OCC_REM], &prev_tor_occ[s][i][CHA_OCC_REM], sizeof(uint64_t));
      r = read(tor_act_fd[s][i][CHA_ACT_REM], &prev_tor_act[s][i][CHA_ACT_REM], sizeof(uint64_t));
    }
    LOG_REPORT("TOR counters: %d CHA tiles opened for socket %d\n", tor_cha_count, s);
  }
}

static void measure_tor_mlp(void)
{
  if (tor_cha_count == 0) return;

  uint64_t sigma1 = 0, sigma2 = 0;

  // First, read local counters and calculate local_mlp
  // It does not depend on socket but on filter in CHA_OCC_LOCAL
  for (int s = 0; s < NUM_TIERS; s++) {
    for (int i = 0; i < tor_cha_count; i++) {
      uint64_t cur_occ, cur_act;
      if (read(tor_occ_fd[s][i][CHA_OCC_LOC], &cur_occ, sizeof(cur_occ)) == (ssize_t)sizeof(cur_occ)) {
        sigma1         += cur_occ - prev_tor_occ[s][i][CHA_OCC_LOC];
        prev_tor_occ[s][i][CHA_OCC_LOC] = cur_occ;
      }
      if (read(tor_act_fd[s][i][CHA_ACT_LOC], &cur_act, sizeof(cur_act)) == (ssize_t)sizeof(cur_act)) {
        sigma2         += cur_act - prev_tor_act[s][i][CHA_ACT_LOC];
        prev_tor_act[s][i][CHA_ACT_LOC] = cur_act;
      }
    }

    local_mlp = sigma2 ? (float)sigma1 / (float)sigma2 : MLP_MIN;
    local_mlp = fmaxf(local_mlp, MLP_MIN);
  }
  LOG_REPORT("LOCAL_MLP: %.4f (sigma1=%lu, sigma2=%lu)\n", local_mlp, sigma1, sigma2);

  // Second, read remote counters and calculate remote_mlp
  sigma1 = sigma2 = 0;
  for (int s = 0; s < NUM_TIERS; s++) {
    for (int i = 0; i < tor_cha_count; i++) {
      uint64_t cur_occ, cur_act;
      if (read(tor_occ_fd[s][i][CHA_OCC_REM], &cur_occ, sizeof(cur_occ)) == (ssize_t)sizeof(cur_occ)) {
        sigma1         += cur_occ - prev_tor_occ[s][i][CHA_OCC_REM];
        prev_tor_occ[s][i][CHA_OCC_REM] = cur_occ;
      }
      if (read(tor_act_fd[s][i][CHA_ACT_REM], &cur_act, sizeof(cur_act)) == (ssize_t)sizeof(cur_act)) {
        sigma2         += cur_act - prev_tor_act[s][i][CHA_ACT_REM];
        prev_tor_act[s][i][CHA_ACT_REM] = cur_act;
      }
    }

    remote_mlp = sigma2 ? (float)sigma1 / (float)sigma2 : MLP_MIN;
    remote_mlp = fmaxf(remote_mlp, MLP_MIN);
  }
  LOG_REPORT("REMOTE_MLP: %.4f (sigma1=%lu, sigma2=%lu)\n", remote_mlp, sigma1, sigma2);
}


static struct perf_event_mmap_page* perf_setup(__u64 config, __u64 config1, __u64 cpu, __u64 type)
{
  struct perf_event_attr attr;

  memset(&attr, 0, sizeof(struct perf_event_attr));

  attr.type = PERF_TYPE_RAW;
  attr.size = sizeof(struct perf_event_attr);

  attr.config = config;
  attr.config1 = config1;
  attr.sample_period = DEFAULT_SAMPLE_PERIOD;

  attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
  attr.pinned = 1;
  attr.disabled = 0;
  //attr.inherit = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.exclude_callchain_kernel = 1;
  attr.exclude_callchain_user = 1;
  attr.precise_ip = 1;

  pfd[cpu][type] = perf_event_open(&attr, -1, cpu, -1, 0);
  if(pfd[cpu][type] == -1) {
    perror("perf_event_open");
  }
  assert(pfd[cpu][type] != -1);

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  struct perf_event_mmap_page *p = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd[cpu][type], 0);
  if(p == MAP_FAILED) {
    perror("mmap");
  }
  assert(p != MAP_FAILED);

  return p;
}


void *pebs_scan_thread()
{
#ifdef SAMPLE_BASED_COOLING
  uint64_t samples_since_cool = 0;
#endif

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(SCANNING_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  for(;;) {
    for (int i = 0; i < PEBS_NPROCS; i++) {
#ifdef JOSEPM
      if (i >= 8 && i < 16) {
        continue;
      }
#elif defined C220G5
	  if (i >= 10 && i < 20) {
		continue;
	  }
#endif
      for(int j = 0; j < NPBUFTYPES; j++) {
        struct perf_event_mmap_page *p = perf_page[i][j];
        char *pbuf = (char *)p + p->data_offset;

        __sync_synchronize();

        if(p->data_head == p->data_tail) {
          continue;
        }

        struct perf_event_header *ph = (void *)(pbuf + (p->data_tail % p->data_size));
        struct perf_sample* ps;
        struct arms_page* page;

        switch(ph->type) {
        case PERF_RECORD_SAMPLE:
            ps = (struct perf_sample*)ph;
            assert(ps != NULL);
            if(ps->addr != 0) {
              __u64 pfn = ps->addr & HUGE_PFN_MASK;
              page = pebs_find_page(pfn);
              if (page != NULL) {
                if (page->va != 0) {
                  page->accesses[j][curr_access_version]++;
                  if (j == DRAMREAD)
                    window_dramread_cnt[curr_access_version]++;
                  else if (j == NVMREAD)
                    window_nvmread_cnt[curr_access_version]++;
                }
                arms_pages_cnt++;
              } else {
                other_pages_cnt++;
              }
              total_pages_cnt++;
            }
            else {
              zero_pages_cnt++;
            }
  	      break;
        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
          LOG_INFO("%s event!\n", ph->type == PERF_RECORD_THROTTLE ? "THROTTLE" : "UNTHROTTLE");
          if (ph->type == PERF_RECORD_THROTTLE) {
              throttle_cnt++;
          }
          else {
              unthrottle_cnt++;
          }
          break;
        default:
          LOG_ERROR("ERROR: Unknown perf_event type %u\n", ph->type);
          assert(0);
          break;
        }

        p->data_tail += ph->size;
      }
    }
  }

  return NULL;
}


static void reset_page_access_fields(struct arms_page *page)
{
  for (int i = 0; i < NPBUFTYPES; i++) {
    page->accesses[i][0] = 0;
    page->accesses[i][1] = 0;
    #ifdef SPATIAL_SMOOTHING
    page->s_accesses[i] = 0;
    #endif
  }
  for (int i = 0; i < WINDOW_SIZE; i++) {
    page->w[i] = 0;
  }
  page->hot_age = 0;
  page->prev_score = 0;
}

static size_t calculate_scores_map(struct score_entry *scores_out)
{
  struct ptimer window_timer;
  ptimer_init(&window_timer, "Scores (window)");

  struct arms_page *page;
  khiter_t key;
  size_t s_idx = 0;

  if (kh_size(pages_map) == 0)
    return 0;

  // PACT Algorithm 1
  // Step 4: ΔC = total NVM samples this window, maintained by scan thread
  uint64_t total_nvm_accesses = window_nvmread_cnt[prev_access_version] * DEFAULT_SAMPLE_PERIOD;
  uint64_t total_dram_accesses = window_dramread_cnt[prev_access_version] * DEFAULT_SAMPLE_PERIOD;
  
  window_nvmread_cnt[prev_access_version] = 0;
  window_dramread_cnt[prev_access_version] = 0;

  // Step 2: S = α × LLC-misses / MLP
  // LLC-misses ≈ total_c × sample_period; α × sample_period cancels in per-page
  // attribution, so S_norm = total_c / MLP is sufficient for relative ranking.
  float S_dram = (total_dram_accesses > 0) ? ((float)total_dram_accesses / local_mlp) : 0.0f;
  float S_nvm = (total_nvm_accesses > 0) ? ((float)total_nvm_accesses / remote_mlp) : 0.0f;

  // Steps 5–8: attribute stalls to pages and accumulate PAC
  for (key = kh_begin(pages_map); key != kh_end(pages_map); ++key) {
    if (!kh_exist(pages_map, key)) continue;
    page = kh_val(pages_map, key);
    if (page == NULL || !page->present) continue;

    // s_p = S × (Δc_p / ΔC)  →  with S_norm = ΔC/MLP: s_p = Δc_p / MLP
    float delta_c = 0;
    float s_p = 0;
    if (page->in_dram) {
      delta_c = (float)page->accesses[DRAMREAD][prev_access_version];
      s_p = (total_dram_accesses > 0) ? (S_dram * delta_c / (float)total_dram_accesses) : 0.0f;
    } else {
      delta_c = (float)page->accesses[NVMREAD][prev_access_version];
      s_p = (total_nvm_accesses > 0) ? (S_nvm * delta_c / (float)total_nvm_accesses) : 0.0f;
    }
    
    // Reset per-window access counts
    page->accesses[DRAMREAD][prev_access_version] = 0;
    page->accesses[NVMREAD][prev_access_version] = 0;
    page->accesses[WRITE][prev_access_version]   = 0;

    page->prev_score = page->score;
    page->score     += s_p;

    scores_out[s_idx++] = (struct score_entry){ page, page->score };
  }
  ptimer_print(&window_timer);

  return s_idx;
}


void *pebs_policy_thread()
{
  struct ptimer loop_timer;
  ptimer_init(&loop_timer, "Loop");

  cpu_set_t cpuset;
  pthread_t thread;

  double migrate_time_us;
  struct arms_page* page = NULL;

  #ifdef SPATIAL_SMOOTHING
  page_tree_entry_t entry;
  #endif

  size_t s_pages_cnt;
  // Use a dedicated CPU core for the policy thread
  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(MIGRATION_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  // Initialize TOR counters for MLP measurement
  // setup_tor_counters();

  // Sleep first to allow the scanning thread to start
  usleep((uint64_t)((1.0 * policy_thread_period)));

  for (;;) {
    ptimer_start(&loop_timer);

    LOG_REPORT("\n========================================\n");
    LOG_REPORT("Starting new interval\n");
    LOG_REPORT("========================================\n");

    // Bump the global version to indicate that we are starting a new interval
    global_version++;
    prev_access_version = curr_access_version;
    curr_access_version = 1 - curr_access_version;
    __sync_synchronize();

    // free pages using free page ring buffer
    while (true) {
      mod_page_t* mp;
      pthread_mutex_lock(&mod_page_dq_lock);
      if (kdq_size(mod_page_dq) == 0) {
        pthread_mutex_unlock(&mod_page_dq_lock);
        break;
      }
      mp = kdq_shift(mod_page_t, mod_page_dq);
      pthread_mutex_unlock(&mod_page_dq_lock);

      page = mp->page;
      LOG_DEBUG("Processing page %p [va: %lu]\n", page, page->va);

      #ifdef SPATIAL_SMOOTHING
      entry.page = page;
      entry.va = page->va;

      if (mp->free) {
        kb_del(kPagesTree, pages_tree, entry);

        // Add page to correct free list
        if (page->in_dram) {
          enqueue_fifo(&dram_free_list, page);
        } else {
          enqueue_fifo(&nvm_free_list, page);
        }
        reset_page_access_fields(page);
      } else {
        kb_put(kPagesTree, pages_tree, entry);
      }
      #else
      khiter_t k = kh_get(kPagesMap, pages_map, page->va);
      if (mp->free) {
        if (k != kh_end(pages_map)) {
          kh_del(kPagesMap, pages_map, k);

          // Add page to correct free list
          if (page->in_dram) {
            enqueue_fifo(&dram_free_list, page);
          } else {
            enqueue_fifo(&nvm_free_list, page);
          }
          reset_page_access_fields(page);
        } else {
          LOG_ERROR("WARNING: Page not found in map\n");
        }
      }
      else {
        int absent;
        k = kh_put(kPagesMap, pages_map, page->va, &absent);
        assert(absent);
        kh_value(pages_map, k) = page;
      }

      #endif
    }

    // measure_tor_mlp();
    s_pages_cnt = calculate_scores_map(scores);

    pac_log_write(scores, s_pages_cnt);

    ptimer_stop_and_print(&loop_timer);

    migrate_time_us = loop_timer.elapsed_us;
    if (migrate_time_us < (1.0 * policy_thread_period)) {
      LOG_INFO("Sleeping for %lu", ((uint64_t)((1.0 * policy_thread_period) - migrate_time_us)));
      usleep((uint64_t)((1.0 * policy_thread_period) - migrate_time_us));
    }
  }

  return NULL;
}

static struct arms_page* pebs_allocate_page()
{
  struct timeval start, end;
  struct arms_page *page;

  gettimeofday(&start, NULL);
  page = dequeue_fifo(&dram_free_list);
  if (page != NULL) {
    assert(page->in_dram);
    assert(!page->present);

    page->present = true;
    //enqueue_fifo(&dram_cold_list, page);

    gettimeofday(&end, NULL);
    LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

    return page;
  }

  // DRAM is full, fall back to NVM
  page = dequeue_fifo(&nvm_free_list);
  if (page != NULL) {
    assert(!page->in_dram);
    assert(!page->present);

    page->present = true;
    //enqueue_fifo(&nvm_cold_list, page);

    gettimeofday(&end, NULL);
    LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

    return page;
  }

  assert(!"Out of memory");
}

struct arms_page* pebs_pagefault(void)
{
  struct arms_page *page;

  // do the heavy lifting of finding the devdax file offset to place the page
  page = pebs_allocate_page();
  assert(page != NULL);

  return page;
}

void pebs_add_page(struct arms_page *page)
{
  int absent;
  khiter_t key;
  assert(page != NULL);
  LOG_INFO("Adding page %lu to the add_pages_ring [va: %lu]\n", (uint64_t)page, page->va);

  // Add to the hash table
  pthread_mutex_lock(&pages_lock);
  key = kh_put(kPagesMap, pages, page->va, &absent);
  assert(absent);
  kh_value(pages, key) = page;
  pthread_mutex_unlock(&pages_lock);

  // Add to the new pages ring
  pthread_mutex_lock(&mod_page_dq_lock);
  mod_page_t mp = (mod_page_t){ .page = page, .free = false };
  kdq_push(mod_page_t, mod_page_dq, mp);
  pthread_mutex_unlock(&mod_page_dq_lock);
}

struct arms_page* pebs_find_page(uint64_t va)
{
  khiter_t key;
  struct arms_page *page;
  pthread_mutex_lock(&pages_lock);
  key = kh_get(kPagesMap, pages, va);
  page = key == kh_end(pages) ? NULL : kh_value(pages, key);
  pthread_mutex_unlock(&pages_lock);
  return page;
}

void pebs_remove_page(struct arms_page *page)
{
  khiter_t key;
  assert(page != NULL);
  LOG_INFO("Removing page %lu from the add_pages_ring [va: %lu]\n", (uint64_t)page, page->va);

  // Remove page from hash table
  pthread_mutex_lock(&pages_lock);
  key = kh_get(kPagesMap, pages, page->va);
  assert(key != kh_end(pages));
  kh_del(kPagesMap, pages, key);
  pthread_mutex_unlock(&pages_lock);

  pthread_mutex_lock(&mod_page_dq_lock);
  mod_page_t mp = (mod_page_t){ .page = page, .free = true};
  kdq_push(mod_page_t, mod_page_dq, mp);
  pthread_mutex_unlock(&mod_page_dq_lock);

  // We set page->present to false so that
  // the migration thread does not migrate this page
  pthread_mutex_lock(&(page->page_lock));
  page->present = false;
  pthread_mutex_unlock(&(page->page_lock));
}

#ifdef SCAILP
#define L3_LOAD_MISS_LOCAL 0x2d3
#define L3_LOAD_MISS_REMOTE 0x10d3
#elif defined JOSEPM
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x80d1
#elif defined C220G5
#define L3_LOAD_MISS_LOCAL 0x1d3
#define L3_LOAD_MISS_REMOTE 0x2d3
#endif

void pebs_init(void)
{
  pebs_print_config();

  pthread_t kswapd_thread;
  pthread_t scan_thread;

  LOG_INFO("pebs_init: started\n");

  for (int i = 0; i < PEBS_NPROCS; i++) {
#ifdef JOSEPM
    if (i >= 8 && i < 16) {
      continue;
    }
#elif defined C220G5
    if (i >= 10 && i < 20) {
      continue;
    }
#endif
    //perf_page[i][READ] = perf_setup(0x1cd, 0x4, i);  // MEM_TRANS_RETIRED.LOAD_LATENCY_GT_4
    //perf_page[i][READ] = perf_setup(0x81d0, 0, i);   // MEM_INST_RETIRED.ALL_LOADS
    // perf_page[i][DRAMREAD] = perf_setup(L3_LOAD_MISS_LOCAL, 0, i, DRAMREAD);      // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
    // perf_page[i][NVMREAD] = perf_setup(L3_LOAD_MISS_REMOTE, 0, i, NVMREAD);     // MEM_LOAD_RETIRED.LOCAL_PMM
    // perf_page[i][WRITE] = perf_setup(0x82d0, 0, i, WRITE);    // MEM_INST_RETIRED.ALL_STORES
    //perf_page[i][WRITE] = perf_setup(0x12d0, 0, i);   // MEM_INST_RETIRED.STLB_MISS_STORES
  }

  pthread_mutex_init(&(dram_free_list.list_lock), NULL);
  for (int i = 0; i < dramsize / PAGE_SIZE; i++) {
    struct arms_page *p = calloc(1, sizeof(struct arms_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = true;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&dram_free_list, p);
  }

  pthread_mutex_init(&(nvm_free_list.list_lock), NULL);
  for (int i = 0; i < nvmsize / PAGE_SIZE; i++) {
    struct arms_page *p = calloc(1, sizeof(struct arms_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = false;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&nvm_free_list, p);
  }

  pages = kh_init(kPagesMap);

  #ifdef SPATIAL_SMOOTHING
  pages_tree = kb_init(kPagesTree, KB_DEFAULT_SIZE);
  #else
  pages_map = kh_init(kPagesMap);
  #endif

  scores = (struct score_entry*)arms_malloc((MAX_NVME_PAGES + MAX_DRAM_PAGES) * sizeof(struct score_entry));

  // Initialize the free/add ring buffers
  mod_page_dq = kdq_init(mod_page_t);

  // Initialize the neighbour ring buffers
#ifdef SPATIAL_SMOOTHING
  buffer = (uint64_t**)arms_malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 2));
  assert(buffer);
  l_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 2);
  buffer = (uint64_t**)arms_malloc(sizeof(uint64_t*) * (NUM_NEIGHBOURS + 2));
  assert(buffer);
  r_neighbours = ring_buf_init(buffer, NUM_NEIGHBOURS + 2);
#endif

  // Start the policy and scan threads
  // int r = pthread_create(&scan_thread, NULL, pebs_scan_thread, NULL);
  // assert(r == 0);

  int r = pthread_create(&kswapd_thread, NULL, pebs_policy_thread, NULL);
  assert(r == 0);

  if ((dramsize+nvmsize) < (32*1024*1024*1024UL)) {
    policy_thread_period = PEBS_KSWAPD_INTERVAL_SMALL;
  } else {
    policy_thread_period = PEBS_KSWAPD_INTERVAL_BIG;
  }

  pac_log_init();

  LOG_INFO("Memory management policy is PEBS\n");
  LOG_INFO("pebs_init: finished\n");
}

void pebs_shutdown()
{
  if (pac_log_fp) { fclose(pac_log_fp); pac_log_fp = NULL; }

  for (int i = 0; i < PEBS_NPROCS; i++) {
    for (int j = 0; j < NPBUFTYPES; j++) {
      ioctl(pfd[i][j], PERF_EVENT_IOC_DISABLE, 0);
      //munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
    }
  }
#ifdef C220G5
  for (int i = 0; i < tor_cha_count; i++) {
    ioctl(tor_occ_fd[i], PERF_EVENT_IOC_DISABLE, 0); close(tor_occ_fd[i]);
    ioctl(tor_act_fd[i], PERF_EVENT_IOC_DISABLE, 0); close(tor_act_fd[i]);
  }
#endif
}

void pebs_stats()
{
  //LOG_STATS("dram_hot_list:[%ld] dram_cold_list:[%ld] nvm_hot_list:[%ld] nvm_cold_list:[%ld] samples:[%ld/%ld] throttle/unthrottle_cnt:[%ld/%ld] cools:[%ld]\n",
  LOG_STATS("samples:[%ld/%ld] throttle/unthrottle_cnt:[%ld/%ld] cools:[%ld]\n",
          //dram_hot_list.numentries,
          //dram_cold_list.numentries,
          //nvm_hot_list.numentries,
          //nvm_cold_list.numentries,
          arms_pages_cnt,
          total_pages_cnt,
          throttle_cnt,
          unthrottle_cnt,
          cools);
  // arms_pages_cnt = total_pages_cnt =  throttle_cnt = unthrottle_cnt = 0;
}

void pebs_print_config()
{
  LOG_REPORT("PEBS configuration:\n");
  LOG_REPORT("  =========================================\n");
  LOG_REPORT("  NUM_MIGRATION_THREADS: %d\n", NUM_MIGRATION_THREADS);
  LOG_REPORT("  PEBS_NPROCS: %d\n", PEBS_NPROCS);
  LOG_REPORT("  NVM_RD_BW_KNEE: %d\n", NVM_RD_BW_KNEE);
  LOG_REPORT("  NVM_WR_BW_KNEE: %d\n", NVM_WR_BW_KNEE);
  LOG_REPORT("  NVM_BW_SLOPE: %f\n", NVM_BW_SLOPE);
  LOG_REPORT("  NVM_WRITES_WEIGHT: %d\n", NVM_WRITES_WEIGHT);
  LOG_REPORT("  MIN_PROMOTION_COST: %ld\n", MIN_PROMOTION_COST);
  LOG_REPORT("  MIN_DEMOTION_COST: %ld\n", MIN_DEMOTION_COST);
  LOG_REPORT("  =========================================\n");
  LOG_REPORT("  PEBS_KSWAPD_INTERVAL_BIG: %d\n", PEBS_KSWAPD_INTERVAL_BIG);
  LOG_REPORT("  PEBS_KSWAPD_INTERVAL_SMALL: %d\n", PEBS_KSWAPD_INTERVAL_SMALL);
  LOG_REPORT("  =========================================\n");
  LOG_REPORT("  HCD_EWMA_ALPHA: %f\n", HCD_EWMA_ALPHA);
  LOG_REPORT("  HCD_STD_ALPHA: %f\n", HCD_STD_ALPHA);
  LOG_REPORT("  =========================================\n");
  LOG_REPORT("  CB_MULTIPLIER: %f\n", CB_MULTIPLIER);
  LOG_REPORT("  MIGRATION_COST_DECAY_RATE: %f\n", MIGRATION_COST_DECAY_RATE);
  LOG_REPORT("  MIGRATION_WINDOW_SIZE: %d\n", MIGRATION_WINDOW_SIZE);
  LOG_REPORT("  MIGRATION_COST_ALPHA: %f\n", MIGRATION_COST_ALPHA);
  LOG_REPORT("  =========================================\n");
  LOG_REPORT("  DEFAULT_SAMPLE_PERIOD: %d\n", DEFAULT_SAMPLE_PERIOD);
  LOG_REPORT("  HF_SAMPLE_PERIOD: %d\n", HF_SAMPLE_PERIOD);
  LOG_REPORT("  =========================================\n");
}
