/*
 * ARMS Kernel-based Memory Tiering System - Header
 */

#ifndef ARMS_KERNEL_H
#define ARMS_KERNEL_H

#include <linux/perf_event.h>

#include <mutex>

#define C220G5

#define FAST_TIER 0
#define SLOW_TIER 1

// System configuration
#ifdef SCAILP
#define PEBS_NPROCS 24

// DRAM bandwidth and latency curve parameters (used for latency diff calculations)
#define UNLOADED_DRAM_LAT     (0.1)    // us
#define DRAM_RD_BW_KNEE       (20)     // GB/s // TODO: Need to measure these
#define DRAM_BW_SLOPE         (0.006)  // us per GB/s // TODO: Need to measure these

// Max expected NVM bandwidth for single thread (for cost and latency calculations)
#define UNLOADED_NVM_LAT       (0.2)     // NVM latency until knee point (us)
#define NVM_RD_BW_KNEE         (6.6)     // GB/s
#define NVM_WR_BW_KNEE         (2.3)     // GB/s
#define NVM_BW_SLOPE           (0.09)    // us per GB/s // TODO: Need to measure these
#define NVM_WRITES_WEIGHT      (3)       // Peak NVM read bw ~ 40GB/s and peak NVM write bw ~ 13GB/s (3x)

#elif defined C220G5
#define PEBS_NPROCS 30

// DRAM bandwidth and latency curve parameters (used for latency diff calculations)
#define UNLOADED_DRAM_LAT     (0.1)    // us
#define DRAM_BW_KNEE          (25)     // GB/s
#define DRAM_BW_SLOPE         (0.006)  // us per GB/s

// Max expected NVM bandwidth for single thread (for cost calculations)
#define UNLOADED_NVM_LAT      (0.25)    // us
#define NVM_RD_BW_KNEE        (15)      // GB/s
#define NVM_WR_BW_KNEE        (15)      // GB/s
#define NVM_BW_SLOPE          (0.09)    // us per GB/s
#define NVM_WRITES_WEIGHT     (1)

#else
#error "Please define your hardware platform (e.g., SCAILP or C220G5)"
#endif

#define MIN_PROMOTION_DATACOPY_TIME  (PAGE_SIZE / (NVM_RD_BW_KNEE * 1024)) // ~ 700us
#define MIN_DEMOTION_DATACOPY_TIME   (PAGE_SIZE / (NVM_WR_BW_KNEE * 1024)) // ~ 1200us
#define MIGRATION_METADATA_COST      (500) // us

#define MIN_PROMOTION_COST           (MIN_PROMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
#define MIN_DEMOTION_COST            (MIN_DEMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)

/// PEBS kswapd thread wakeup interval
// ==============================================================================
#define PEBS_KSWAPD_INTERVAL_BIG      (500000) // in us (500ms)
#define PEBS_KSWAPD_INTERVAL_SMALL    (100000) // in us (100ms)
// ==============================================================================

/// Page scoring parameters
// ==============================================================================
// Number of EWMA windows
#define WINDOW_SIZE   (2)

// Bias values for history and recency
#define HIST_BIAS_RECN  (0.4)
#define HIST_BIAS       {HIST_BIAS_RECN, 1.-HIST_BIAS_RECN}
//#define HIST_BIAS       {0.4, 0.6}  // Harmonic progression (1/3, 1/2)

#define RECN_BIAS_RECN  (0.731)
#define RECN_BIAS       {RECN_BIAS_RECN, 1.-RECN_BIAS_RECN}
//#define RECN_BIAS     {0.731, 0.269}    // Exponential (e-1, e-2)

#define SHORT_TERM_WND_PERIOD_MS (1000)  // 1s
#define LONG_TERM_WND_PERIOD_MS  (10000) // 10s

#define SHORT_TERM_WND_ALPHA (2./(double)(SHORT_TERM_WND_PERIOD_MS/(PEBS_KSWAPD_INTERVAL_BIG/1000) + 1))  // 1s -> 0.6667
#define LONG_TERM_WND_ALPHA  (2./(double)(LONG_TERM_WND_PERIOD_MS/(PEBS_KSWAPD_INTERVAL_BIG/1000) + 1))   // 10s -> 0.0952

#define W_EWMA_ALPHA  {(SHORT_TERM_WND_ALPHA), (LONG_TERM_WND_ALPHA)}  // 1s, 10s
// ==============================================================================

/// Hot-change Detector
// ==============================================================================
#define HCD_EWMA_ALPHA          (0.1) // EWMA alpha
#define HCD_STD_ALPHA           (0.2) // std alpha

#define HCD_RECN_MAX_PERIODS    (20)  // Max periods to stay in RECN bias
#define HCD_RECN_MIN_NVM_BW     (0.3) // Min NVM bw to switch to RECN bias

#define HCD_PH_DRIFT            (0.5)   // CUSUM drift
#define HCD_PH_THRESHOLD        (6.0)   // CUSUM threshold
// ==============================================================================

/// Page migration
// ==============================================================================
#define CB_MULTIPLIER             (1.5)     // Cost-benefit multiplier
#define MIGRATION_COST_DECAY_RATE (1.5)     // Decay rate for migration cost (1.5 means that the cost decreases by 1.5 every 1 interval)

#define MIGRATION_WINDOW_SIZE     (20)      // Window size for migration cost averaging
#define MIGRATION_COST_ALPHA      (2./(double)(MIGRATION_WINDOW_SIZE + 1))  // EWMA alpha for migration cost (20 periods -> 0.0952)
// ==============================================================================

#define PERF_PAGES	(1 + (1 << 8))	// Has to be == 1+2^n, here 1MB
#define DEFAULT_SAMPLE_PERIOD	(10007)
#define HF_SAMPLE_PERIOD	(5003)

enum sampling_modes {
  DEFAULT_SAMPLING = 0,
  HF_SAMPLING = 1,
  NUM_SAMPLING_MODES
};

#define SCANNING_THREAD_CPU (0 + 1)
#define POLICY_THREAD_CPU (SCANNING_THREAD_CPU + 1)

/// Bandwidth monitoring
// ==============================================================================
#define NUM_TIERS (2) // DRAM and NVM

#ifdef SCAILP
#define NUM_IMC 4                 // IceLake has 4 iMCs
#define IMC_BASE_ADDR 0xFB900000  // TODO: Need to find this dynamically, currently obtained from PCM
#define PCM_SERVER_IMC_MMAP_SIZE   (0x4000)
// There are 4 counters: DRAM Reads, DRAM Writes, PMM Reads, PMM Writes
#define PCM_SERVER_IMC_DRAM_READS   (0x2290)
#define PCM_SERVER_IMC_DRAM_WRITES  (0x2298)
#define PCM_SERVER_IMC_PMM_READS    (0x22a0)
#define PCM_SERVER_IMC_PMM_WRITES   (0x22a8)

enum imc_bw_counters {
  DRAM_READS  = 0,
  DRAM_WRITES = 1,
  NVM_READS   = 2,
  NVM_WRITES  = 3,
  NUM_BW_COUNTERS
};

#elif defined C220G5

#define NUM_IMC        (6)
#define NUM_EVENTS     (2) // There are 2 events per IMC: Reads and Writes
#endif

/// Generic macros
// ==============================================================================
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define PAGE_SIZE   	(HUGEPAGE_SIZE)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)


enum pbuftype {
  DRAMREAD = 0,
  NVMREAD = 1,
  WRITE = 2,
  NPBUFTYPES
};

// PEBS sample structure
struct perf_sample {
  struct perf_event_header header;
  __u64 ip;
  __u32 pid, tid;
  __u64 addr;  // Virtual address
};

struct arms_page_info {
  uint64_t va;  // Virtual address
  float w[WINDOW_SIZE];  // EWMA windows
  float score;
  float prev_score;
  uint16_t accesses[NPBUFTYPES][2];  // Access counts per version
  uint16_t hot_age;
  bool in_dram;
  bool can_promote;
  std::mutex page_lock;

  arms_page_info() : va(0), score(0), prev_score(0), hot_age(0),
                     in_dram(false), can_promote(true) {
    for (int i = 0; i < WINDOW_SIZE; i++) w[i] = 0;
    for (int i = 0; i < NPBUFTYPES; i++) {
      accesses[i][0] = accesses[i][1] = 0;
    }
  }
};

// Score entry for sorting
struct score_entry {
  arms_page_info* page;
  float score;
};

void arms_start_tiering();
void arms_kernel_shutdown();
void arms_kernel_print_stats();
#endif /* ARMS_KERNEL_H */
