#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// Define target system here
// Options are SCAILP and C220G5
#define C220G5


#define MIN_INTERPOSE_MEM_SIZE_DEFAULT (1 * 1024UL * 1024UL * 1024UL)

#define FAULT_THREAD_CPU  (10)
#define STATS_THREAD_CPU  (13)

//#define HEMEM_DEBUG
#define STATS_THREAD

#define USE_DMA
#define NUM_CHANNS 2
#define SIZE_PER_DMA_REQUEST (1024*1024)

#define MEM_BARRIER() __sync_synchronize()


#define NVMSIZE_DEFAULT   (64L * (1024L * 1024L * 1024L))
#define DRAMSIZE_DEFAULT  (32L * (1024L * 1024L * 1024L))

#define DRAMPATH_DEFAULT  "/dev/dax0.0"
#define NVMPATH_DEFAULT   "/dev/dax1.0"

#define BASEPAGE_SIZE	  (4UL * 1024UL)
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define GIGAPAGE_SIZE   (1024UL * 1024UL * 1024UL)
#define PAGE_SIZE 	    HUGEPAGE_SIZE
#define CACHELINE_SIZE   (64)

#define MAX_NVME_PAGES  (NVMSIZE_DEFAULT / PAGE_SIZE)
#define MAX_DRAM_PAGES  (DRAMSIZE_DEFAULT / PAGE_SIZE)

#define BASEPAGE_MASK	(BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK   (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK	(BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK   (GIGAPAGE_MASK ^ UINT64_MAX)

// Logging Options
// ==============================================================================
//// Uncomment to enable debug logging
//#define ARMS_DEBUG

#define LOG_STREAM (stdout)

#ifdef ARMS_DEBUG
#define LOG_DEBUG(...)  fprintf(LOG_STREAM, __VA_ARGS__)
#define LOG_INFO(...)   fprintf(LOG_STREAM, __VA_ARGS__)
#else
#define LOG_DEBUG(...)  while(0) {}
#define LOG_INFO(...)   while(0) {}
#endif

#define LOG_REPORT(...) fprintf(LOG_STREAM, __VA_ARGS__)
#define LOG_ERROR(...)  { fprintf(stderr, __VA_ARGS__); fprintf(LOG_STREAM, __VA_ARGS__); }
// =============================================================================

/// Hardware-related parameters (should be set once for each system)
// ==============================================================================
#ifdef SCAILP
#define NUM_MIGRATION_THREADS (4)
#define PEBS_NPROCS           (24)

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
#define NUM_MIGRATION_THREADS (8)
#define PEBS_NPROCS           (30)        // C220g5 has 20 cores on NUMA node 0 (0-9,20-29)

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
#error "Unknown system - valid options are SCAILP and C220G5"
#endif

#define MIN_PROMOTION_DATACOPY_TIME  (PAGE_SIZE / (NVM_RD_BW_KNEE * 1024)) // ~ 700us
#define MIN_DEMOTION_DATACOPY_TIME   (PAGE_SIZE / (NVM_WR_BW_KNEE * 1024)) // ~ 1200us
#define MIGRATION_METADATA_COST      (500) // us

#define MIN_PROMOTION_COST           (MIN_PROMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
#define MIN_DEMOTION_COST            (MIN_DEMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
// ==============================================================================


/// PEBS kswapd thread wakeup interval
// ==============================================================================
#define PEBS_KSWAPD_INTERVAL_BIG      (500000) // in us (500ms)
#define PEBS_KSWAPD_INTERVAL_SMALL    (100000) // in us (100ms)
// ==============================================================================


/// Page scoring parameters
// ==============================================================================
// Number of EWMA windows
#define WINDOW_SIZE   (4)

// Bias values for history and recency
#define HIST_BIAS_RECN  (0.4)
#define HIST_BIAS       {HIST_BIAS_RECN, 0, 1.-HIST_BIAS_RECN, 0}
//#define HIST_BIAS       {0.4, 0.6}  // Harmonic progression (1/3, 1/2)

#define RECN_BIAS_RECN  (0.731)
#define RECN_BIAS       {RECN_BIAS_RECN, 0, 1.-RECN_BIAS_RECN, 0}
//#define RECN_BIAS     {0.731, 0.269}    // Exponential (e-1, e-2)

#define SHORT_TERM_WND_PERIOD_MS (1000)  // 1s
#define LONG_TERM_WND_PERIOD_MS  (10000) // 10s

#define EWMA_2_ALPHA (2./(2. + 1.))  // 1s -> 0.6667
#define EWMA_5_ALPHA (2./(5. + 1.))  // 5s -> 0.3333
#define EWMA_20_ALPHA  (2./(20. + 1.))   // 20s -> 0.0952
#define EWMA_100_ALPHA  (2./(100. + 1.))   // 100s -> 0.0952

#define W_EWMA_ALPHA  {(EWMA_2_ALPHA), (EWMA_5_ALPHA), (EWMA_20_ALPHA), (EWMA_100_ALPHA)}  // 1s, 5s, 20s, 100s
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
  HIGH_FIDELITY = 1,
  NUM_SAMPLING_MODES
};

#define SCANNING_THREAD_CPU (FAULT_THREAD_CPU + 1)
#define MIGRATION_THREAD_CPU (SCANNING_THREAD_CPU + 1)


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

static const float w_ewma_alpha[WINDOW_SIZE] = W_EWMA_ALPHA;
static const float hist_bias[WINDOW_SIZE] = HIST_BIAS;
static const float recn_bias[WINDOW_SIZE] = RECN_BIAS;

#define DENOM_SIZE (300)
static bool w_ewma_denom_initialized = 0;
static float w_adjusted_ewma_denom[WINDOW_SIZE][DENOM_SIZE];
static inline float get_adjusted_ewma_denom(uint16_t alpha_idx, uint32_t t) {
    if (!w_ewma_denom_initialized) {
        for (int i = 0; i < WINDOW_SIZE; i++) {
            float alpha = w_ewma_alpha[i];
            float sum = 0.0;
            for (int j = 0; j < DENOM_SIZE; j++) {
                sum += powf((1 - alpha), j);
                w_adjusted_ewma_denom[i][j] = sum;
            }
        }
        w_ewma_denom_initialized = 1;
    }
    // it's probably close enough
    if (t >= DENOM_SIZE) {
        t = DENOM_SIZE - 1;
    }
    return w_adjusted_ewma_denom[alpha_idx][t];
}