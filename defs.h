#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/perf_event.h>

#include <mutex>

extern bool initialized;

#ifndef USE_MODEL
#define USE_MODEL (true)
#endif

#ifndef PRINT_TRAINING_DATA
#define PRINT_TRAINING_DATA (false)
#endif

#define FULL_LOGS (false)

#ifndef MAX_LOGGED_SAMPLES
#define MAX_LOGGED_SAMPLES (10000000)
#endif

#define MALLOC_SAMPLE_RATE (10) // Log 1 out of every N malloc calls
#define BACKOFF_PERIOD (4)      // Number of scanning intervals to backoff after promotion/demotion
#define HISTORY_LENGTH (10)

#define MIN_FREE_MEMORY (1024 * 1024) // in KB (ie 1 GB)

#define C220G5

// NUMA node assignment: node0 = fast (near), node1 = slow (far)
#define FAST_TIER 0
#define SLOW_TIER 1

// System configuration
#ifdef SCAILP
#define PEBS_NPROCS 24

// DRAM bandwidth and latency curve parameters (used for latency diff calculations)
#define UNLOADED_DRAM_LAT (0.1) // us
#define DRAM_RD_BW_KNEE (20)    // GB/s // TODO: Need to measure these
#define DRAM_BW_SLOPE (0.006)   // us per GB/s // TODO: Need to measure these

// Max expected NVM bandwidth for single thread (for cost and latency calculations)
#define UNLOADED_NVM_LAT (0.2) // NVM latency until knee point (us)
#define NVM_RD_BW_KNEE (6.6)   // GB/s
#define NVM_WR_BW_KNEE (2.3)   // GB/s
#define NVM_BW_SLOPE (0.09)    // us per GB/s // TODO: Need to measure these
#define NVM_WRITES_WEIGHT (3)  // Peak NVM read bw ~ 40GB/s and peak NVM write bw ~ 13GB/s (3x)

#elif defined C220G5
#define PEBS_NPROCS 30

// DRAM bandwidth and latency curve parameters (used for latency diff calculations)
#define UNLOADED_DRAM_LAT (0.1) // us
#define DRAM_BW_KNEE (25)       // GB/s
#define DRAM_BW_SLOPE (0.006)   // us per GB/s

// Max expected NVM bandwidth for single thread (for cost calculations)
#define UNLOADED_NVM_LAT (0.25) // us
#define NVM_RD_BW_KNEE (15)     // GB/s
#define NVM_WR_BW_KNEE (15)     // GB/s
#define NVM_BW_SLOPE (0.09)     // us per GB/s
#define NVM_WRITES_WEIGHT (1)

#else
#error "Please define your hardware platform (e.g., SCAILP or C220G5)"
#endif

#define MIN_PROMOTION_DATACOPY_TIME (PAGE_SIZE / (NVM_RD_BW_KNEE * 1024)) // ~ 700us
#define MIN_DEMOTION_DATACOPY_TIME (PAGE_SIZE / (NVM_WR_BW_KNEE * 1024))  // ~ 1200us
#define MIGRATION_METADATA_COST (500)                                     // us

#define MIN_PROMOTION_COST (MIN_PROMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
#define MIN_DEMOTION_COST (MIN_DEMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)

/// PEBS kswapd thread wakeup interval
// ==============================================================================
#if USE_MODEL == false && PRINT_TRAINING_DATA == false
#define PEBS_KSWAPD_INTERVAL_BIG (500000)   // in us (500ms)
#define PEBS_KSWAPD_INTERVAL_SMALL (100000) // in us (100ms)
#else
#define PEBS_KSWAPD_INTERVAL_BIG (250000)   // in us (250ms)
#define PEBS_KSWAPD_INTERVAL_SMALL (250000) // in us (250ms)
#endif

// ==============================================================================

/// Page scoring parameters
// ==============================================================================
// Number of EWMA windows
#define WINDOW_SIZE (4)

// Bias values for history and recency
#define HIST_BIAS_RECN (0.4)
#define HIST_BIAS {HIST_BIAS_RECN, 0, 1. - HIST_BIAS_RECN, 0}
// #define HIST_BIAS       {0.4, 0.6}  // Harmonic progression (1/3, 1/2)

#define RECN_BIAS_RECN (0.731)
#define RECN_BIAS {RECN_BIAS_RECN, 0, 1. - RECN_BIAS_RECN, 0}
// #define RECN_BIAS     {0.731, 0.269}    // Exponential (e-1, e-2)

#define EWMA_2_ALPHA (2. / (2. + 1.))     // 1s -> 0.6667
#define EWMA_5_ALPHA (2. / (5. + 1.))     // 5s -> 0.3333
#define EWMA_20_ALPHA (2. / (20. + 1.))   // 20s -> 0.0952
#define EWMA_100_ALPHA (2. / (100. + 1.)) // 100s -> 0.0952

#define W_EWMA_ALPHA {(EWMA_2_ALPHA), (EWMA_5_ALPHA), (EWMA_20_ALPHA), (EWMA_100_ALPHA)} // 1s, 5s, 20s, 100s
// ==============================================================================

/// Hot-change Detector
// ==============================================================================
#define HCD_EWMA_ALPHA (0.1) // EWMA alpha
#define HCD_STD_ALPHA (0.2)  // std alpha

#define HCD_RECN_MAX_PERIODS (20) // Max periods to stay in RECN bias
#define HCD_RECN_MIN_NVM_BW (0.3) // Min NVM bw to switch to RECN bias

#define HCD_PH_DRIFT (0.5)     // CUSUM drift
#define HCD_PH_THRESHOLD (6.0) // CUSUM threshold
// ==============================================================================

/// Page migration
// ==============================================================================
#define CB_MULTIPLIER (1.5) // Cost-benefit multiplier
#define MIGRATION_COST_DECAY_RATE                                                                                      \
    (1.5) // Decay rate for migration cost (1.5 means that the cost decreases by 1.5 every 1 interval)

#define MIGRATION_WINDOW_SIZE (20) // Window size for migration cost averaging
#define MIGRATION_COST_ALPHA                                                                                           \
    (2. / (double)(MIGRATION_WINDOW_SIZE + 1)) // EWMA alpha for migration cost (20 periods -> 0.0952)
// ==============================================================================

#define PERF_PAGES (1 + (4 << 8)) // Has to be == 1+2^n, here 4MB
#define DEFAULT_SAMPLE_PERIOD (5003)
#define HF_SAMPLE_PERIOD (5003)

enum sampling_modes
{
    DEFAULT_SAMPLING = 0,
    HIGH_FIDELITY = 1,
    NUM_SAMPLING_MODES
};

#define SCANNING_THREAD_CPU (0 + 1)
#define POLICY_THREAD_CPU (SCANNING_THREAD_CPU + 1)

/// Bandwidth monitoring
// ==============================================================================
#define NUM_TIERS (2) // DRAM and NVM

#ifdef SCAILP
#define NUM_IMC 4                // IceLake has 4 iMCs
#define IMC_BASE_ADDR 0xFB900000 // TODO: Need to find this dynamically, currently obtained from PCM
#define PCM_SERVER_IMC_MMAP_SIZE (0x4000)
// There are 4 counters: DRAM Reads, DRAM Writes, PMM Reads, PMM Writes
#define PCM_SERVER_IMC_DRAM_READS (0x2290)
#define PCM_SERVER_IMC_DRAM_WRITES (0x2298)
#define PCM_SERVER_IMC_PMM_READS (0x22a0)
#define PCM_SERVER_IMC_PMM_WRITES (0x22a8)

enum imc_bw_counters
{
    DRAM_READS = 0,
    DRAM_WRITES = 1,
    NVM_READS = 2,
    NVM_WRITES = 3,
    NUM_BW_COUNTERS
};

#elif defined C220G5

#define NUM_IMC (6)
#define NUM_EVENTS (2) // There are 2 events per IMC: Reads and Writes
#endif

/// Generic macros
// ==============================================================================
#define HUGEPAGE_SIZE (2UL * 1024UL * 1024UL)
#define PAGE_SIZE (HUGEPAGE_SIZE)
#define HUGE_PFN_MASK (HUGEPAGE_MASK ^ UINT64_MAX)
#define HUGEPAGE_MASK (HUGEPAGE_SIZE - 1)
#define BASE_PAGE (4096)
#define BASE_PAGE_PER_HUGEPAGE (HUGEPAGE_SIZE / BASE_PAGE)

static const float w_ewma_alpha[WINDOW_SIZE] = W_EWMA_ALPHA;
static const float hist_bias[WINDOW_SIZE] = HIST_BIAS;
static const float recn_bias[WINDOW_SIZE] = RECN_BIAS;

#define DENOM_SIZE (300)
static bool w_ewma_denom_initialized = 0;
static float w_adjusted_ewma_denom[WINDOW_SIZE][DENOM_SIZE];
static inline float get_adjusted_ewma_denom(uint16_t alpha_idx, uint32_t t)
{
    if (!w_ewma_denom_initialized)
    {
        for (int i = 0; i < WINDOW_SIZE; i++)
        {
            float alpha = w_ewma_alpha[i];
            float sum = 0.0;
            for (int j = 0; j < DENOM_SIZE; j++)
            {
                sum += powf((1 - alpha), j);
                w_adjusted_ewma_denom[i][j] = sum;
            }
        }
        w_ewma_denom_initialized = 1;
    }
    // it's probably close enough
    if (t >= DENOM_SIZE)
    {
        t = DENOM_SIZE - 1;
    }
    return w_adjusted_ewma_denom[alpha_idx][t];
}
