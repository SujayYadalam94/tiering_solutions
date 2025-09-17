#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem.h"

#define LOG_STREAM (stdout)

// Uncomment to enable spatial smoothing
// #define SPATIAL_SMOOTHING
#ifdef SPATIAL_SMOOTHING
#define NUM_NEIGHBOURS (2)
#endif

/// Hardware-related parameters (should be set once for each system)
// ==============================================================================
#ifdef SCAILP
#define NUM_MIGRATION_THREADS (4)
#define PEBS_NPROCS           (24)

// Max expected NVM bandwidth for single thread (for cost calculations)
#define MAX_NVM_RD_BW         (6.6)     // GB/s
#define MAX_NVM_WR_BW         (2.3)     // GB/s
#define NVM_WRITES_WEIGHT     (3)       // Peak NVM read bw ~ 40GB/s and peak NVM write bw ~ 13GB/s (3x)
#define LATENCY_DIFF          (0.1)     // latency diff bw DRAM and NVM = 0.1us or 100ms

#define NVM_HPAGE_MIGRATION_COST_KNEEPOINT (20000) // microseconds (20ms)

#elif defined C220G5
#define NUM_MIGRATION_THREADS (8)
#define PEBS_NPROCS           (30)        // C220g5 has 20 cores on NUMA node 0 (0-9,20-29)

// Max expected NVM bandwidth for single thread (for cost calculations)
#define MAX_NVM_RD_BW         (20)      // GB/s
#define MAX_NVM_WR_BW         (20)      // GB/s
#define NVM_WRITES_WEIGHT     (1)
#define LATENCY_DIFF          (0.1)     // latency diff bw DRAM and NVM = 0.1us or 100ms

#define NVM_HPAGE_MIGRATION_COST_KNEEPOINT (20000)  // microseconds (20ms)

#else
#error "Unknown system - valid options are SCAILP and C220G5"
#endif

#define MIN_PROMOTION_DATACOPY_TIME  (PAGE_SIZE / (MAX_NVM_RD_BW * 1024)) // ~ 700us
#define MIN_DEMOTION_DATACOPY_TIME   (PAGE_SIZE / (MAX_NVM_WR_BW * 1024)) // ~ 1200us
#define MIGRATION_METADATA_COST      (500) // us

#define MIN_PROMOTION_COST           (MIN_PROMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
#define MIN_DEMOTION_COST            (MIN_DEMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
// ==============================================================================


/// Page scoring parameters
// ==============================================================================
// Number of EWMA windows
#define WINDOW_SIZE   (2)

// Bias values for history and recency
#define HIST_BIAS     {0.4, 0.6}        // Harmonic progression (1/3, 1/2)
#define RECN_BIAS     {0.731, 0.269}    // Exponential (e-1, e-2)

// 2/(2^i + 1)
#define W_EWMA_ALPHA  {0.6667, 0.0952}  // 1s, 10s
// #define W_EWMA_ALPHA {.6667, .3333, .0952, 0.0199} // 200ms, 500ms, 2s, 10s
// #define W_EWMA_ALPHA {0.1818, .0952, 0.0392, 0.0199} // 1s, 2s, 5s, 10s
// ==============================================================================


/// Hot-change Detector
// ==============================================================================
#define HCD_EWMA_ALPHA          (0.3) // EWMA alpha
#define HCD_STD_ALPHA           (0.1) // std alpha

#define HCD_RECN_MAX_PERIODS    (20)  // Max periods to stay in RECN bias
#define HCD_RECN_MIN_NVM_BW     (0.3) // Min NVM bw to switch to RECN bias

#define HCD_PH_DRIFT            (0.1) // Page-Hinkley drift
#define HCD_PH_THRESHOLD        (3)   // Page-Hinkley threshold
#define HCD_PH_RESET_THRESHOLD  (-2)  // Page-Hinkley reset threshold
// ==============================================================================


/// Page migration
// ==============================================================================
#define CB_MULTIPLIER             (1.5)     // Cost-benefit multiplier
#define MIGRATION_COST_DECAY_RATE (1.5)     // Decay rate for migration cost (1.5 means that the cost decreases by 1.5 every 1 interval)

#define MIGRATION_WINDOW_SIZE     (20)      // Window size for migration cost averaging
#define MIGRATION_COST_ALPHA      (2./(double)(MIGRATION_WINDOW_SIZE + 1))  // EWMA alpha for migration cost (20 periods -> 0.0952)
// ==============================================================================


/// PEBS kswapd thread wakeup interval
// ==============================================================================
#define PEBS_KSWAPD_INTERVAL_BIG      (500000) // in us (500ms)
#define PEBS_KSWAPD_INTERVAL_SMALL    (100000) // in us (100ms)
// ==============================================================================


#define PERF_PAGES	(1 + (1 << 8))	// Has to be == 1+2^n, here 1MB
#define DEFAULT_SAMPLE_PERIOD	(10007)
#define HF_SAMPLE_PERIOD	(5003)

enum sampling_modes {
  DEFAULT_SAMPLING = 0,
  HIGH_FIDELITY = 1,
  NUM_SAMPLING_MODES
};

#define SCANNING_THREAD_CPU (FAULT_THREAD_CPU + 1)
#define MIGRATION_THREAD_CPU (SCANNING_THREAD_CPU + 1)

#define NUM_IMC 4                 // IceLake has 4 iMCs
#define IMC_BASE_ADDR 0xFB900000  // TODO: Need to find this dynamically, currently obtained from PCM

// There are 4 counters: DRAM Reads, DRAM Writes, PMM Reads, PMM Writes
// We are only interested in PMem bandwidth counters
#define PCM_SERVER_IMC_PMM_READS   (0x22a0)
#define PCM_SERVER_IMC_PMM_WRITES  (0x22a8)

#define PCM_SERVER_IMC_MMAP_SIZE   (0x4000)

enum imc_bw_counters {
  NVM_READS = 0,
  NVM_WRITES = 1,
  NUM_BW_COUNTERS
};

struct perf_sample {
  struct perf_event_header header;
  __u64	ip;
  __u32 pid, tid;    /* if PERF_SAMPLE_TID */
  __u64 addr;        /* if PERF_SAMPLE_ADDR */
  // __u64 weight;      /* if PERF_SAMPLE_WEIGHT */
  /* __u64 data_src;    /\* if PERF_SAMPLE_DATA_SRC *\/ */
};

enum pbuftype {
  DRAMREAD = 0,
  NVMREAD = 1,
  WRITE = 2,
  NPBUFTYPES
};

struct score_entry {
  struct hemem_page* page;
  float score;
};

void *pebs_kswapd();
struct hemem_page* pebs_pagefault(void);
struct hemem_page* pebs_pagefault_unlocked(void);
void pebs_init(void);

void pebs_add_page(struct hemem_page *page);
struct hemem_page* pebs_find_page(uint64_t va);
void pebs_remove_page(struct hemem_page *page);

void pebs_stats();
void pebs_shutdown();

#endif /*  HEMEM_LRU_MODIFIED_H  */
