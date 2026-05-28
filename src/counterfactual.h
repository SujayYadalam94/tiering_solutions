#ifndef ARMS_COUNTERFACTUAL_H
#define ARMS_COUNTERFACTUAL_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

/* arms.h includes pebs.h in a specific order required by the header guards;
 * counterfactual.h must not include either directly.  Callers must include
 * arms.h before counterfactual.h so that the constants below are available. */

/* ================================================================
 * Counterfactual DRAM-size prediction — configuration
 *
 * Every CF_WINDOW_S seconds the CF thread wakes on a CLOCK_MONOTONIC
 * timer, snapshots per-page access counts, and sweeps candidate DRAM
 * sizes to report the minimum that keeps predicted slowdown below
 * CF_SLOWDOWN_THRESHOLD.
 *
 * Tune these constants per machine / workload before building.
 * ================================================================ */

/* Length of the accumulation window in seconds. */
#define CF_WINDOW_S       30

/* CPU frequency used for µs → cycles conversion.
 * FLAG-3: must be set manually; it cannot be inferred from PEBS data. */
#define CF_CPU_FREQ_GHZ   2.5

/* Slowdown threshold for the minimum-DRAM recommendation. */
#define CF_SLOWDOWN_THRESHOLD   0.02   /* 2 % */
/* Slowdown threshold above which DRAM is considered too small and needs to grow. */
#define CF_INCREASE_THRESHOLD   0.02   /* 2 % */

/* How far to sweep from the current DRAM size (bytes), depending on direction.
 * Decrease sweeps [current - CF_SWEEP_DELTA_DECREASE, current].
 * Increase sweeps [current, current + CF_SWEEP_DELTA_INCREASE].
 * Both ranges are clamped to [0, max_dramsize]. */
#define CF_SWEEP_DELTA_DECREASE  (4ULL * 1024ULL * 1024ULL * 1024ULL)   /* 4 GB */
#define CF_SWEEP_DELTA_INCREASE  (8ULL * 1024ULL * 1024ULL * 1024ULL)   /* 8 GB */

/* Number of candidate sizes within the sweep range.
 * Must be ≥ 1; odd values naturally include the current size. */
#define CF_SWEEP_STEPS    2

/* Maximum number of (bw_gbps, latency_us) data points per bw-lat curve file. */
#define CF_MAX_CURVE_POINTS  64

/* Each PEBS L3-miss event represents one 64-byte cache-line transfer.
 * Used only for bandwidth calculation, not for counting pages per tier.
 * FLAG-2: bandwidth feed to the latency curve uses read accesses only. */
#define CF_CACHE_LINE_BYTES  64

#define CF_DECAY_FACTOR 0.5   /* per-window decay of the long access counts */

/* Phase-change detection: sudden spike in stall rate, bandwidth, or access
 * count triggers a restore of DRAM to its original size. */
#define CF_PHASE_EWMA_ALPHA      0.2   /* smoothing factor for per-window EWMA */
#define CF_PHASE_SPIKE_THRESHOLD 3.0   /* std-devs above EWMA to trigger restore */
#define CF_PHASE_MIN_WINDOWS     3     /* warm-up windows before detection is armed */

/* CPU core for the CF thread.  MIGRATION_THREAD_CPU is the highest
 * pinned core in the existing system; we use the next one after it.
 * For C220G5: FAULT=10, SCANNING=11, MIGRATION=12, CF=13. */
#ifdef MIGRATION_THREAD_CPU
#define CF_THREAD_CPU     (MIGRATION_THREAD_CPU + 1)
#else
#define CF_THREAD_CPU     (13)   /* default for C220G5 */
#endif

/* Path to the combined JSON bw-lat file containing both DRAM and CXL curves.
 * Override at runtime with the ARMS_BWLAT env var.
 *
 * Expected schema:
 *   {
 *     "dram": {
 *       "raw": [ {"bw_gbps": <float>, "latency_ns": <float>}, ... ]
 *     },
 *     "cxl": {
 *       "raw": [ {"bw_gbps": <float>, "latency_ns": <float>}, ... ]
 *     }
 *   }
 * latency_ns is converted to µs on load.  "raw" points are used for
 * interpolation; the "model" section (if present) is ignored.
 * Points must be ordered with non-decreasing bw_gbps within each tier.
 */
#define CF_BWLAT_DEFAULT  "bw-lat.dat"

/* Default path for the per-second CSV metrics log.
 * Override at runtime with the ARMS_CSV_LOG env var. */
#define CF_CSV_DEFAULT_PATH  "/tmp/arms_metrics.csv"

/* ================================================================
 * Snapshot type
 *
 * Built by the CF thread each time its CF_WINDOW_S timer fires by
 * walking the shared pages_map under cf_snapshot_mutex.
 * ================================================================ */
typedef struct {
    uint64_t va;
    uint8_t  in_dram;         /* 1 = DRAM, 0 = CXL at snapshot time */
    uint64_t read_accesses;   /* arms_page.long_read_accesses  accumulated over CF_WINDOW_S */
    uint64_t write_accesses;  /* arms_page.long_write_accesses accumulated over CF_WINDOW_S */
} cf_page_snapshot_t;

/* ================================================================
 * Public API  (called from pebs.c)
 * ================================================================ */

/* Load bw-lat curve files, allocate the snapshot buffer, and spawn the
 * CF analysis thread.  max_pages should be MAX_NVME_PAGES + MAX_DRAM_PAGES. */
void counterfactual_init(size_t max_pages);

/* Register the shared state the CF thread needs to build its own snapshot.
 * Must be called from pebs_policy_thread() after setup_stall_counters(),
 * before the first CF_WINDOW_S timer fires (i.e. within the first 30 s).
 *
 *   pages_map      — khash_t(kPagesMap) * cast to void *
 *   stall_fd/prev  — per-CPU CYCLE_ACTIVITY.STALLS_L3_MISS fds + last-read values
 *   cycles_fd/prev — per-CPU cpu-cycles fds + last-read values
 *   dram_bw_sum    — IMC DRAM BW accumulator (GB/s · samples)
 *   cxl_bw_sum     — IMC CXL BW accumulator
 *   bw_samples     — number of samples in the current window
 *   dramsize       — pointer to the live dramsize variable
 *   snapshot_mutex — held by the CF thread during the snapshot walk;
 *                    also held by the policy thread around pages_map
 *                    structural changes and BW accumulator updates. */
void counterfactual_register_state(
    void            *pages_map,
    int             *stall_fd,   uint64_t *stall_prev,
    int             *cycles_fd,  uint64_t *cycles_prev,
    double          *dram_bw_sum, double *cxl_bw_sum, int *bw_samples,
    uint64_t        *dramsize,
    pthread_mutex_t *snapshot_mutex,
    int             *tor_occ_fd, int *tor_act_fd,
    uint64_t        *prev_tor_occ, uint64_t *prev_tor_act,
    int              tor_cha_count);

/* CF thread entry point registered with pthread_create in counterfactual_init.
 * Loops on a CF_WINDOW_S CLOCK_MONOTONIC timer; runs cf_run_analysis() each
 * iteration. */
void *counterfactual_thread_fn(void *arg);

#endif /* ARMS_COUNTERFACTUAL_H */
