#define _GNU_SOURCE

/* arms.h must be included first: it defines C220G5 and then includes
 * pebs.h internally.  pebs.h must see arms.h's guard already set when
 * it tries to re-include arms.h, so this ordering is mandatory. */
#include "arms.h"
#include "khash.h"
KHASH_MAP_INIT_INT64(kPagesMap, struct arms_page*)
#include "counterfactual.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/* ================================================================
 * File-scope state
 * ================================================================ */

/* Bandwidth-latency curves — loaded once at init, read-only thereafter.
 * Each array is indexed 0..n-1 with bw_arr strictly non-decreasing. */
static float cf_dram_bw [CF_MAX_CURVE_POINTS];
static float cf_dram_lat[CF_MAX_CURVE_POINTS];   /* µs */
static int   cf_dram_n = 0;

static float cf_cxl_bw [CF_MAX_CURVE_POINTS];
static float cf_cxl_lat[CF_MAX_CURVE_POINTS];    /* µs */
static int   cf_cxl_n  = 0;

/* Pre-allocated snapshot buffer; filled by the CF thread each timer wakeup. */
static cf_page_snapshot_t *cf_snap_buf = NULL;
static size_t               cf_snap_cap = 0;   /* capacity (max pages) */

/* Cross-window prediction record: filled when a recommendation is made,
 * consumed on the next window to compute prediction accuracy. */
typedef struct {
    int      valid;
    uint64_t predicted_dram_bytes;
    double   predicted_stall_rate;   /* exp_stalls / cycles at prediction time */
    double   baseline_stall_rate;    /* measured_stalls / cycles at prediction time */
    time_t   ts;
} cf_pred_record_t;

static cf_pred_record_t cf_last_pred = {0};

/* Shared state registered by the policy thread via counterfactual_register_state(). */
static khash_t(kPagesMap) *s_pages_map    = NULL;
static int                 *s_stall_fd    = NULL;
static uint64_t            *s_stall_prev  = NULL;
static int                 *s_cycles_fd   = NULL;
static uint64_t            *s_cycles_prev = NULL;
static uint64_t            *s_dram_acc    = NULL;
static uint64_t            *s_cxl_acc     = NULL;
static double              *s_dram_bw_sum = NULL;
static double              *s_cxl_bw_sum  = NULL;
static int                 *s_bw_samples  = NULL;
static uint64_t            *s_dramsize    = NULL;
static uint64_t             s_original_dramsize = 0;  /* DRAM bytes at first registration */
static pthread_mutex_t     *s_snap_mutex  = NULL;

/* TOR counter state registered from pebs.c. */
#define MLP_MIN 0.001f
static int      *s_tor_occ_fd   = NULL;
static int      *s_tor_act_fd   = NULL;
static uint64_t *s_prev_tor_occ = NULL;
static uint64_t *s_prev_tor_act = NULL;
static int       s_tor_cha_count = 0;
static float     total_mlp       = MLP_MIN;

/* LP model parameters read from CF_MODEL_PARAMS_PATH each analysis window. */
static cf_lp_params_t s_lp_params;  /* zeroed = invalid at startup */

/* CSV metrics log — one row per second. */
static FILE   *cf_csv_fp          = NULL;
static double  s_prev_dram_bw_sum = 0.0;
static double  s_prev_cxl_bw_sum  = 0.0;
static int     s_prev_bw_samples  = 0;

static void cf_open_csv(void)
{
    const char *path = getenv("ARMS_CSV_LOG") ?: CF_CSV_DEFAULT_PATH;
    cf_csv_fp = fopen(path, "w");
    if (!cf_csv_fp) {
        LOG_REPORT("cf: cannot open CSV log '%s' (%s)\n", path, strerror(errno));
        return;
    }
    fprintf(cf_csv_fp,
            "Timestamp,cur_dram_acc,cur_cxl_acc,dram_bw_gbps,"
            "obs_dram_lat_cyc,cxl_bw_gbps,obs_cxl_lat_cyc,"
            "MLP,measured_stalls,measured_cycles,current_dram_bytes\n");
    fflush(cf_csv_fp);
    LOG_REPORT("cf: CSV metrics log opened at '%s'\n", path);
}

static void cf_write_csv_row(time_t ts,
                              uint64_t cur_dram_acc, uint64_t cur_cxl_acc,
                              double dram_bw_gbps, double obs_dram_lat_cyc,
                              double cxl_bw_gbps,  double obs_cxl_lat_cyc,
                              float mlp,
                              uint64_t measured_stalls, uint64_t measured_cycles,
                              uint64_t current_dram_bytes)
{
    if (!cf_csv_fp) return;
    fprintf(cf_csv_fp, "%ld,%lu,%lu,%.3f,%.1f,%.3f,%.1f,%.4f,%lu,%lu,%lu\n",
            (long)ts,
            cur_dram_acc, cur_cxl_acc,
            dram_bw_gbps, obs_dram_lat_cyc,
            cxl_bw_gbps,  obs_cxl_lat_cyc,
            (double)mlp,
            measured_stalls, measured_cycles,
            current_dram_bytes);
    fflush(cf_csv_fp);
}

/* Phase-change detection: per-metric EWMA + exponential variance state. */
typedef struct {
    double ewma;
    double var;
    int    n;
} cf_phase_metric_t;

static cf_phase_metric_t cf_pm_stall_rate = {0};
static cf_phase_metric_t cf_pm_total_bw   = {0};
static cf_phase_metric_t cf_pm_total_acc  = {0};

static void measure_tor_mlp(void)
{
    if (s_tor_cha_count == 0) return;
    uint64_t sigma1 = 0, sigma2 = 0;
    for (int i = 0; i < s_tor_cha_count; i++) {
        uint64_t cur;
        if (read(s_tor_occ_fd[i], &cur, sizeof(cur)) == (ssize_t)sizeof(cur)) {
            sigma1 += cur - s_prev_tor_occ[i];
            s_prev_tor_occ[i] = cur;
        }
        if (read(s_tor_act_fd[i], &cur, sizeof(cur)) == (ssize_t)sizeof(cur)) {
            sigma2 += cur - s_prev_tor_act[i];
            s_prev_tor_act[i] = cur;
        }
    }
    total_mlp = sigma2 ? (float)sigma1 / (float)sigma2 : MLP_MIN;
    total_mlp = fmaxf(total_mlp, MLP_MIN);
    LOG_REPORT("cf: total_mlp=%.4f (sigma1=%lu, sigma2=%lu)\n",
               total_mlp, sigma1, sigma2);
}


/* ================================================================
 * 1. JSON bw-lat loader
 * ================================================================ */

/*
 * Read an entire file into a heap-allocated, NUL-terminated buffer.
 * Caller must free() the buffer.  Returns 0 on success, -1 on failure.
 */
static int cf_slurp_file(const char *path, char **buf_out)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_REPORT("cf: cannot open '%s' (%s)\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long len = ftell(f);
    if (len <= 0)                    { fclose(f); return -1; }
    rewind(f);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nr = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[nr] = '\0';
    *buf_out = buf;
    return 0;
}

/*
 * Extract the "raw" array for one tier from the JSON buffer.
 *
 * Looks for:
 *   "<tier_name>": { ... "raw": [ {"bw_gbps": F, "latency_ns": F}, ... ] }
 *
 * latency_ns values are divided by 1000 to convert to µs.
 * Points must appear with non-decreasing bw_gbps (as measured).
 *
 * Returns 0 on success, -1 if the tier or raw array is not found.
 *
 * FLAG-5: out-of-range bandwidth queries extrapolate linearly at lookup
 *         time (cf_lookup_latency_us) with a run-time warning.
 */
static int cf_parse_tier(const char *json, const char *tier,
                          float *bw_arr, float *lat_arr, int *n_out)
{
    /* 1. Locate the tier key, e.g. "dram" or "cxl". */
    char key[32];
    snprintf(key, sizeof(key), "\"%s\"", tier);
    const char *tier_pos = strstr(json, key);
    if (!tier_pos) {
        LOG_ERROR("cf: tier '%s' not found in bw-lat JSON\n", tier);
        return -1;
    }

    /* 2. Find "raw" inside that tier's section. */
    const char *raw_pos = strstr(tier_pos, "\"raw\"");
    if (!raw_pos) {
        LOG_ERROR("cf: 'raw' array not found for tier '%s'\n", tier);
        return -1;
    }

    /* 3. Find the opening '[' of the raw array. */
    const char *arr_open = strchr(raw_pos, '[');
    if (!arr_open) return -1;

    /* 4. Find the closing ']' of the raw array.
     *    The array elements use '{}' not '[]', so the first ']' after
     *    arr_open is the one that closes the array. */
    const char *arr_close = strchr(arr_open + 1, ']');
    if (!arr_close) return -1;

    /* 5. Walk the array, extracting bw_gbps and latency_ns from each point. */
    const char *p = arr_open + 1;
    *n_out = 0;

    while (p < arr_close) {
        /* Find the next "bw_gbps" key within the array bounds. */
        const char *bw_key = strstr(p, "\"bw_gbps\"");
        if (!bw_key || bw_key >= arr_close) break;

        const char *bw_colon = strchr(bw_key, ':');
        if (!bw_colon || bw_colon >= arr_close) break;

        float bw_gbps;
        if (sscanf(bw_colon + 1, " %f", &bw_gbps) != 1) break;

        /* "latency_ns" must appear after "bw_gbps" in the same object. */
        const char *lat_key = strstr(bw_colon, "\"latency_ns\"");
        if (!lat_key || lat_key >= arr_close) break;

        const char *lat_colon = strchr(lat_key, ':');
        if (!lat_colon || lat_colon >= arr_close) break;

        float lat_ns;
        if (sscanf(lat_colon + 1, " %f", &lat_ns) != 1) break;

        if (*n_out >= CF_MAX_CURVE_POINTS) {
            LOG_ERROR("cf: tier '%s' exceeds CF_MAX_CURVE_POINTS=%d\n",
                      tier, CF_MAX_CURVE_POINTS);
            break;
        }

        bw_arr [*n_out] = bw_gbps;
        lat_arr[*n_out] = lat_ns / 1000.0f;   /* ns → µs */
        (*n_out)++;

        p = lat_colon + 1;   /* advance past the value we just consumed */
    }

    if (*n_out < 2) {
        LOG_ERROR("cf: tier '%s' has < 2 raw points (%d found)\n",
                  tier, *n_out);
        return -1;
    }
    return 0;
}

/*
 * Load both DRAM and CXL bw-lat curves from a single combined JSON file.
 * Returns 0 if both tiers loaded successfully, -1 otherwise.
 */
static int cf_load_bwlat_json(const char *path)
{
    char *json = NULL;
    if (cf_slurp_file(path, &json) != 0) return -1;

    int rc = 0;
    if (cf_parse_tier(json, "dram", cf_dram_bw, cf_dram_lat, &cf_dram_n) != 0) rc = -1;
    if (cf_parse_tier(json, "cxl",  cf_cxl_bw,  cf_cxl_lat,  &cf_cxl_n)  != 0) rc = -1;

    free(json);

    if (rc == 0)
        LOG_REPORT("cf: loaded bw-lat from '%s' (DRAM: %d pts, CXL: %d pts)\n",
                   path, cf_dram_n, cf_cxl_n);
    return rc;
}


/* ================================================================
 * 2. Latency lookup — linear interpolation
 * ================================================================ */

/*
 * Return latency in µs for bw_gbps by linear interpolation on a sorted curve.
 *
 * Curve arrays must be sorted with bw_arr[0] ≤ bw_arr[1] ≤ … ≤ bw_arr[n-1].
 *
 * FLAG-5: if bw_gbps is outside [bw_arr[0], bw_arr[n-1]], the function
 *         extrapolates linearly using the nearest endpoint slope and emits a
 *         LOG_REPORT warning.  Extrapolation past the saturation knee will
 *         underestimate the true latency — check these warnings.
 */
static float cf_lookup_latency_us(float bw_gbps,
                                   const float *bw_arr, const float *lat_arr,
                                   int n, const char *label)
{
    if (n < 2) return (n == 1) ? lat_arr[0] : 0.0f;

    /* Below the curve: extrapolate using the first segment's slope. */
    if (bw_gbps < bw_arr[0]) {
        LOG_REPORT("cf WARNING [%s]: bw %.3f GB/s below curve min %.3f GB/s"
                   " — returning unloaded latency %.3f µs\n", label, bw_gbps, bw_arr[0], lat_arr[0]);
        return lat_arr[0];
    }

    /* Within the curve: find the bracketing interval and interpolate. */
    for (int i = 0; i < n - 1; i++) {
        if (bw_gbps <= bw_arr[i + 1]) {
            float denom = bw_arr[i + 1] - bw_arr[i];
            if (denom == 0.0f) return lat_arr[i];
            float t = (bw_gbps - bw_arr[i]) / denom;
            return lat_arr[i] + t * (lat_arr[i + 1] - lat_arr[i]);
        }
    }

    /* Above the curve: extrapolate using the last segment's slope. */
    LOG_REPORT("cf WARNING [%s]: bw %.3f GB/s above curve max %.3f GB/s"
               " — extrapolating linearly\n", label, bw_gbps, bw_arr[n - 1]);
    float denom = bw_arr[n - 1] - bw_arr[n - 2];
    float slope = (denom != 0.0f) ? (lat_arr[n - 1] - lat_arr[n - 2]) / denom : 0.0f;
    return lat_arr[n - 1] + slope * (bw_gbps - bw_arr[n - 1]);
}


/* ================================================================
 * 3. Core stall formula
 * ================================================================ */

/*
 * LP-model variant: stalls = (alpha_dram * Ad * Ld + alpha_cxl * Ac * Lc) / MLP + beta
 * Coefficients come from the LP fitter in exposure_lp.py.
 */
static double cf_compute_expected_stalls_lp(double dram_bw, double cxl_bw,
                                              uint64_t dram_acc, uint64_t cxl_acc,
                                              const cf_lp_params_t *p)
{
    double dram_lat_us = cf_lookup_latency_us((float)dram_bw,
                             cf_dram_bw, cf_dram_lat, cf_dram_n, "DRAM");
    double cxl_lat_us  = cf_lookup_latency_us((float)cxl_bw,
                             cf_cxl_bw,  cf_cxl_lat,  cf_cxl_n,  "CXL");

    double dram_lat_cyc = dram_lat_us * CF_CPU_FREQ_GHZ * 1000.0;
    double cxl_lat_cyc  = cxl_lat_us  * CF_CPU_FREQ_GHZ * 1000.0;

    return (p->alpha_dram * (double)dram_acc * dram_lat_cyc +
            p->alpha_cxl  * (double)cxl_acc  * cxl_lat_cyc) / (double)total_mlp
           + p->beta;
}

/* Update EWMA + variance for one phase-detection metric and return true if
 * the new value is a spike (> CF_PHASE_SPIKE_THRESHOLD std-devs above EWMA).
 * Comparison uses the pre-update EWMA so a genuine outlier isn't absorbed
 * into the mean before the check. */
static bool cf_phase_update(cf_phase_metric_t *m, double value)
{
    double diff  = value - m->ewma;
    bool   spike = false;
    if (m->n >= CF_PHASE_MIN_WINDOWS) {
        double std = sqrt(m->var);
        spike = (std > 0.0 && diff > CF_PHASE_SPIKE_THRESHOLD * std);
    }
    m->ewma += CF_PHASE_EWMA_ALPHA * diff;
    m->var   = (1.0 - CF_PHASE_EWMA_ALPHA) * (m->var + CF_PHASE_EWMA_ALPHA * diff * diff);
    m->n++;
    return spike;
}

static void cf_read_lp_params(void)
{
    const char *path = getenv("CF_MODEL_PARAMS") ?: CF_MODEL_PARAMS_PATH;
    FILE *f = fopen(path, "r");
    if (!f) { s_lp_params.valid = 0; return; }

    cf_lp_params_t p = {0};
    int found = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if      (sscanf(line, "alpha_dram=%lf", &p.alpha_dram) == 1) found++;
        else if (sscanf(line, "alpha_cxl=%lf",  &p.alpha_cxl)  == 1) found++;
        else if (sscanf(line, "beta=%lf",        &p.beta)       == 1) found++;
        else if (sscanf(line, "epsilon=%lf",     &p.epsilon)    == 1) found++;
    }
    fclose(f);
    p.valid = (found == 4);
    s_lp_params = p;
}

static int cf_cmp_hotness_desc(const void *a, const void *b)
{
    uint64_t ra = ((const cf_page_snapshot_t *)a)->read_accesses;
    uint64_t rb = ((const cf_page_snapshot_t *)b)->read_accesses;
    return (ra < rb) - (ra > rb);   /* descending */
}


/* ================================================================
 * 5. Main analysis: CDF sweep + recommendation
 * ================================================================ */

/*
 * Run the counterfactual analysis on a snapshot of n pages.
 *
 * Steps:
 *  1. Sort pages hottest-first to build the access CDF.
 *  2. Compute current_stalls from the actual placement (in_dram flags).
 *  3. Sweep CF_SWEEP_STEPS+1 candidate DRAM sizes in an asymmetric range:
 *       decrease: [current_dram - CF_SWEEP_DELTA_DECREASE, current_dram]
 *       increase: [current_dram, current_dram + CF_SWEEP_DELTA_INCREASE]
 *     clamped to [0, n×PAGE_SIZE].  For each size, the hottest pages go
 *     into DRAM; the rest spill to CXL.  This is the optimal placement
 *     for that size, not a simulation of the current policy.
 *  4. Log a sweep table and recommend the smallest size whose predicted
 *     slowdown is below CF_SLOWDOWN_THRESHOLD.
 *
 * qsort modifies snap[] in place; snap is the module-static cf_snap_buf
 * owned by the CF thread.
 */
static void cf_run_analysis(cf_page_snapshot_t *snap, size_t n,
                             uint64_t current_dram_bytes,
                             uint64_t measured_stalls, uint64_t measured_cycles,
                             double dram_bw_gbps, double cxl_bw_gbps)
{
    if (cf_dram_n < 2 || cf_cxl_n < 2) {
        LOG_REPORT("cf: bw-lat curves not loaded — skipping analysis\n");
        return;
    }
    if (n == 0) return;

    cf_read_lp_params();
    if (!s_lp_params.valid) {
        LOG_REPORT("cf: LP model params not yet available — skipping analysis\n");
        return;
    }

    struct timespec now_ts;
    clock_gettime(CLOCK_REALTIME, &now_ts);
    time_t window_ts = now_ts.tv_sec;

    const double cycles = (measured_cycles > 0) ? (double)measured_cycles : 1.0;
    double actual_stall_rate = (double)measured_stalls / cycles;

    /* Cross-window accuracy: compare last window's prediction to this window's reality. */
    if (cf_last_pred.valid) {
        double pred_error = actual_stall_rate - cf_last_pred.predicted_stall_rate;
        LOG_REPORT("cf: ACCURACY ts=%ld pred_ts=%ld"
                   " predicted_dram_mb=%lu actual_dram_mb=%lu"
                   " predicted_stall_rate=%.6f actual_stall_rate=%.6f"
                   " error=%.6f error_pct=%.2f%%\n",
                   (long)window_ts, (long)cf_last_pred.ts,
                   cf_last_pred.predicted_dram_bytes >> 20,
                   current_dram_bytes >> 20,
                   cf_last_pred.predicted_stall_rate,
                   actual_stall_rate,
                   pred_error,
                   pred_error * 100.0);
    }

    /* Phase-change detection: spike in any metric → restore DRAM to original size. */
    {
        uint64_t total_acc = 0;
        for (size_t i = 0; i < n; i++)
            total_acc += snap[i].read_accesses + snap[i].write_accesses;
        double total_bw = dram_bw_gbps + cxl_bw_gbps;

        bool spike_stall = cf_phase_update(&cf_pm_stall_rate, actual_stall_rate);
        bool spike_bw    = cf_phase_update(&cf_pm_total_bw,   total_bw);
        bool spike_acc   = cf_phase_update(&cf_pm_total_acc,  (double)total_acc);

        if ((spike_stall || spike_bw || spike_acc) && s_original_dramsize > 0) {
            LOG_REPORT("cf: PHASE_CHANGE_DETECTED ts=%ld"
                       " spike_stall=%d(%.6f/ewma=%.6f)"
                       " spike_bw=%d(%.3f/ewma=%.3f)"
                       " spike_acc=%d(%lu/ewma=%.0f)"
                       " — restoring DRAM to %lu MB\n",
                       (long)window_ts,
                       spike_stall, actual_stall_rate, cf_pm_stall_rate.ewma,
                       spike_bw, total_bw, cf_pm_total_bw.ewma,
                       spike_acc, (unsigned long)total_acc, cf_pm_total_acc.ewma,
                       s_original_dramsize >> 20);
            // FILE *f = fopen(dram_config_path, "w");
            // if (f) {
            //     fprintf(f, "%luM\n", s_original_dramsize >> 20);
            //     fclose(f);
            // }
            cf_pm_stall_rate = (cf_phase_metric_t){0};
            cf_pm_total_bw   = (cf_phase_metric_t){0};
            cf_pm_total_acc  = (cf_phase_metric_t){0};
            cf_last_pred.valid = 0;
            return;
        }
    }

    /* 1. Sort hottest-first (FLAG-1: by read_accesses). */
    qsort(snap, n, sizeof(*snap), cf_cmp_hotness_desc);

    /* 2. Sum per-tier access counts from the actual placement (in_dram flags).
     *    Used as the reference point for the BW delta calculation below. */
    uint64_t cur_dram_acc = 0, cur_cxl_acc = 0;
    for (size_t i = 0; i < n; i++) {
        if (snap[i].in_dram) cur_dram_acc += snap[i].read_accesses;
        else                  cur_cxl_acc  += snap[i].read_accesses;
    }

    /* 3. Bytes-per-access ratio: constant across all candidates. */
    double agg_acc = (double)cur_dram_acc + (double)cur_cxl_acc;
    double agg_bw  = dram_bw_gbps + cxl_bw_gbps;
    double bpa     = (agg_acc > 0) ? agg_bw * CF_WINDOW_S * 1e9 / agg_acc
                                   : CF_CACHE_LINE_BYTES;

    /* Compute expected stalls at the original (startup) DRAM size as the
     * fixed baseline for all slowdown comparisons — prevents the ratchet
     * effect that occurs when comparing against an already-shrunk current size. */
    uint64_t ref_dram    = (s_original_dramsize > 0) ? s_original_dramsize : current_dram_bytes;
    size_t   n_dram_orig = (size_t)(ref_dram / PAGE_SIZE);
    if (n_dram_orig > n) return;
    uint64_t dram_acc_orig = 0, cxl_acc_orig = 0;
    for (size_t i = 0;           i < n_dram_orig; i++) dram_acc_orig += snap[i].read_accesses;
    for (size_t i = n_dram_orig; i < n;           i++) cxl_acc_orig  += snap[i].read_accesses;
    double c_dram_bw_orig  = dram_acc_orig * bpa / (CF_WINDOW_S * 1e9);
    double c_cxl_bw_orig   = cxl_acc_orig  * bpa / (CF_WINDOW_S * 1e9);
    double exp_stalls_orig  = cf_compute_expected_stalls_lp(c_dram_bw_orig, c_cxl_bw_orig,
                                                             dram_acc_orig, cxl_acc_orig,
                                                             &s_lp_params);
    LOG_REPORT("cf: original_dram=%lu MB (exp_stalls_orig=%.0f stall_rate_orig=%.6f)\n",
               ref_dram >> 20, exp_stalls_orig, exp_stalls_orig / cycles);

    /* Use the LP model to predict stalls at the current actual placement, then
     * compare against the original-size baseline to determine sweep direction. */
    double c_dram_bw_curr  = cur_dram_acc * bpa / (CF_WINDOW_S * 1e9);
    double c_cxl_bw_curr   = cur_cxl_acc  * bpa / (CF_WINDOW_S * 1e9);
    double exp_stalls_curr = cf_compute_expected_stalls_lp(c_dram_bw_curr, c_cxl_bw_curr,
                                                            cur_dram_acc, cur_cxl_acc,
                                                            &s_lp_params);
    double current_slowdown = (exp_stalls_curr - exp_stalls_orig) / cycles;
    int    needs_increase   = (current_slowdown > CF_INCREASE_THRESHOLD);

    /* 4. Build the candidate sweep range: ±4 GB from current, direction-gated. */
    uint64_t sweep_lo, sweep_hi;
    if (needs_increase) {
        sweep_lo = current_dram_bytes;
        sweep_hi = (current_dram_bytes + CF_SWEEP_DELTA_DECREASE < max_dramsize)
                   ? current_dram_bytes + CF_SWEEP_DELTA_DECREASE : max_dramsize;
    } else {
        sweep_lo = (current_dram_bytes > CF_SWEEP_DELTA_DECREASE)
                   ? current_dram_bytes - CF_SWEEP_DELTA_DECREASE : 0;
        sweep_hi = current_dram_bytes;
    }
    sweep_lo = (sweep_lo / PAGE_SIZE) * PAGE_SIZE;
    sweep_hi = (sweep_hi / PAGE_SIZE) * PAGE_SIZE;

    LOG_REPORT("cf: === counterfactual ts=%ld (window=%ds, %zu pages,"
               " current=%lu MB, sweep=[%lu, %lu] MB,"
               " current_slowdown=%.4f%%, direction=%s) ===\n",
               (long)window_ts, CF_WINDOW_S, n,
               current_dram_bytes >> 20,
               sweep_lo >> 20, sweep_hi >> 20,
               current_slowdown * 100.0,
               needs_increase ? "increase" : "decrease");
    LOG_REPORT("cf: measured — stalls=%lu cycles=%lu stall_rate=%.6f"
               " dram_acc=%lu cxl_acc=%lu"
               " dram_bw=%.3f GB/s cxl_bw=%.3f GB/s mlp=%.3f\n",
               measured_stalls, measured_cycles, actual_stall_rate,
               cur_dram_acc, cur_cxl_acc, dram_bw_gbps, cxl_bw_gbps, total_mlp);
    LOG_REPORT("cf: LP params: alpha_dram=%.6f alpha_cxl=%.6f beta=%.2f epsilon=%.2f\n",
               s_lp_params.alpha_dram, s_lp_params.alpha_cxl,
               s_lp_params.beta, s_lp_params.epsilon);
    LOG_REPORT("cf: %10s  %13s  %10s  %14s %10s  %13s  %10s  %9s\n",
               "DRAM (MB)", "Slowdown (%)", "DRAM acc", "DRAM BW(GB/s)",
               "CXL acc", "CXL BW(GB/s)",
               "DRAM pages", "CXL pages");

    /* Epsilon from the LP fit is the worst-case model error in stall cycles.
     * Subtract it from the threshold so we only recommend when we're confident
     * the true slowdown is within CF_SLOWDOWN_THRESHOLD. */
    double epsilon_margin      = s_lp_params.epsilon / cycles;
    double effective_threshold = CF_SLOWDOWN_THRESHOLD - epsilon_margin;

    size_t recommended_pages    = 0;
    int    found_recommendation = 0;

    uint64_t span      = sweep_hi - sweep_lo;
    int      num_steps = CF_SWEEP_STEPS;

    for (int step = 0; step <= num_steps; step++) {
        uint64_t candidate_bytes = sweep_lo + (span * (uint64_t)step) / (uint64_t)num_steps;
        candidate_bytes = (candidate_bytes / PAGE_SIZE) * PAGE_SIZE;

        size_t n_dram = (size_t)(candidate_bytes / PAGE_SIZE);
        if (n_dram > n) continue;
        size_t n_cxl = n - n_dram;

        uint64_t dram_acc = 0, cxl_acc = 0;
        for (size_t i = 0;      i < n_dram; i++) dram_acc += snap[i].read_accesses;
        for (size_t i = n_dram; i < n;      i++) cxl_acc  += snap[i].read_accesses;

        double c_dram_bw = dram_acc * bpa / (CF_WINDOW_S * 1e9);
        double c_cxl_bw  = cxl_acc  * bpa / (CF_WINDOW_S * 1e9);

        double exp_stalls = cf_compute_expected_stalls_lp(c_dram_bw, c_cxl_bw,
                                                           dram_acc, cxl_acc,
                                                           &s_lp_params);
        /* Slowdown vs. expected stalls at original DRAM size: positive = slower, negative = faster. */
        double slowdown = (exp_stalls - exp_stalls_orig) / cycles;

        uint64_t    dram_mb = candidate_bytes >> 20;
        const char *mark    = (slowdown < effective_threshold) ? " <--" : "";
        LOG_REPORT("cf: %10lu  %12.2f%% %10lu %14.3f %10lu %13.3f  %10zu  %9zu%s\n",
                   dram_mb, slowdown * 100.0,
                   dram_acc, c_dram_bw, cxl_acc, c_cxl_bw, n_dram, n_cxl, mark);

        if (!found_recommendation && slowdown < effective_threshold) {
            recommended_pages    = n_dram;
            found_recommendation = 1;

            cf_last_pred.valid                = 1;
            cf_last_pred.predicted_dram_bytes = candidate_bytes;
            cf_last_pred.predicted_stall_rate = exp_stalls / cycles;
            cf_last_pred.baseline_stall_rate  = exp_stalls_orig / cycles;
            cf_last_pred.ts                   = window_ts;
        }
    }

    /* 4. Emit recommendation. */
    if (found_recommendation && recommended_pages != 0) {
        uint64_t rec_mb = (uint64_t)recommended_pages * PAGE_SIZE >> 20;
        const char *direction = (rec_mb > (current_dram_bytes >> 20)) ? "grow" : "shrink";
        LOG_REPORT("cf: RECOMMENDATION: %s DRAM to %lu MB"
                   " (predicted slowdown < %.2f%% vs. original DRAM size,"
                   " epsilon_margin=%.2f%%)\n",
                   direction, rec_mb,
                   effective_threshold * 100.0, epsilon_margin * 100.0);
        // Write the recommended size to the specified path as 'X'M
        FILE *f = fopen(dram_config_path, "w");
        if (f) {
            fprintf(f, "%luM\n", rec_mb);
            fclose(f);
        } else {
            LOG_REPORT("cf: WARNING: cannot write recommendation to %s (%s)\n",
                       dram_config_path, strerror(errno));
        }
    } else {
        LOG_REPORT("cf: no candidate in [%lu, %lu] MB meets < %.2f%% slowdown\n",
                   sweep_lo >> 20, sweep_hi >> 20, effective_threshold * 100.0);
    }
}


/* ================================================================
 * 6. CF thread
 * ================================================================ */

void *counterfactual_thread_fn(void *arg)
{
    (void)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CF_THREAD_CPU, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
        LOG_REPORT("cf: pthread_setaffinity_np failed (non-fatal)\n");

    cf_open_csv();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    for (;;) {
        uint64_t total_stalls_win = 0, total_cycles_win = 0;
        khiter_t it;

        /* Inner loop: one 1-second tick per iteration, CF_WINDOW_S ticks per window.
         * Collects per-second metrics and writes a CSV row each tick. */
        for (int sec = 0; sec < CF_WINDOW_S; sec++) {
            ts.tv_sec += 1;
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);

            if (!s_pages_map || !cf_snap_buf || !s_snap_mutex) continue;

            struct timespec now_ts;
            clock_gettime(CLOCK_REALTIME, &now_ts);

            pthread_mutex_lock(s_snap_mutex);

            /* Per-second stall/cycle deltas; accumulated into 30s totals. */
            uint64_t stalls_1s = 0, cycles_1s = 0;
            for (int ci = 0; ci < PEBS_NPROCS; ci++) {
                uint64_t val = 0;
                if (s_stall_fd[ci] >= 0 &&
                    read(s_stall_fd[ci], &val, sizeof(val)) == (ssize_t)sizeof(val)) {
                    stalls_1s        += val - s_stall_prev[ci];
                    s_stall_prev[ci]  = val;
                }
                val = 0;
                if (s_cycles_fd[ci] >= 0 &&
                    read(s_cycles_fd[ci], &val, sizeof(val)) == (ssize_t)sizeof(val)) {
                    cycles_1s         += val - s_cycles_prev[ci];
                    s_cycles_prev[ci]  = val;
                }
            }
            total_stalls_win += stalls_1s;
            total_cycles_win += cycles_1s;

            /* Per-second BW: average over samples accumulated since last tick. */
            int    bw_delta   = *s_bw_samples - s_prev_bw_samples;
            double dram_bw_1s = (bw_delta > 0)
                                ? (*s_dram_bw_sum - s_prev_dram_bw_sum) / bw_delta : 0.0;
            double cxl_bw_1s  = (bw_delta > 0)
                                ? (*s_cxl_bw_sum  - s_prev_cxl_bw_sum)  / bw_delta : 0.0;
            if (dram_bw_1s < 0.0) dram_bw_1s = 0.0;
            if (cxl_bw_1s  < 0.0) cxl_bw_1s  = 0.0;
            s_prev_dram_bw_sum = *s_dram_bw_sum;
            s_prev_cxl_bw_sum  = *s_cxl_bw_sum;
            s_prev_bw_samples  = *s_bw_samples;

            uint64_t cur_dram_bytes = *s_dramsize;

            pthread_mutex_unlock(s_snap_mutex);

            /* Latency lookup from per-second BW (read-only on static curve arrays). */
            double obs_dram_lat_cyc = cf_lookup_latency_us((float)dram_bw_1s,
                                          cf_dram_bw, cf_dram_lat, cf_dram_n, "DRAM")
                                      * CF_CPU_FREQ_GHZ * 1000.0;
            double obs_cxl_lat_cyc  = cf_lookup_latency_us((float)cxl_bw_1s,
                                          cf_cxl_bw, cf_cxl_lat, cf_cxl_n, "CXL")
                                      * CF_CPU_FREQ_GHZ * 1000.0;

            measure_tor_mlp();

            cf_write_csv_row(now_ts.tv_sec,
                             *s_dram_acc, *s_cxl_acc,
                             dram_bw_1s, obs_dram_lat_cyc,
                             cxl_bw_1s,  obs_cxl_lat_cyc,
                             total_mlp,
                             stalls_1s, cycles_1s,
                             cur_dram_bytes);

            *s_dram_acc = 0;
            *s_cxl_acc  = 0;
        }

        /* 30-second decision boundary: build full snapshot with decay and run analysis. */
        if (!s_pages_map || !cf_snap_buf || !s_snap_mutex) continue;

        pthread_mutex_lock(s_snap_mutex);

        size_t n = 0;
        for (it = kh_begin(s_pages_map); it != kh_end(s_pages_map); ++it) {
            if (!kh_exist(s_pages_map, it)) continue;
            struct arms_page *p = kh_val(s_pages_map, it);
            if (!p || !p->present) continue;
            if (n < cf_snap_cap) {
                cf_snap_buf[n++] = (cf_page_snapshot_t){
                    .va             = p->va,
                    .in_dram        = (uint8_t)p->in_dram,
                    .read_accesses  = p->long_read_accesses,
                    .write_accesses = p->long_write_accesses,
                };
                p->long_read_accesses  = p->long_read_accesses * CF_DECAY_FACTOR;
                p->long_write_accesses = p->long_write_accesses * CF_DECAY_FACTOR;
            }
        }

        double dram_bw_avg = (*s_bw_samples > 0) ? *s_dram_bw_sum / *s_bw_samples : 0.0;
        double cxl_bw_avg  = (*s_bw_samples > 0) ? *s_cxl_bw_sum  / *s_bw_samples : 0.0;
        uint64_t cur_dram  = *s_dramsize;
        *s_dram_bw_sum     = 0.0;
        *s_cxl_bw_sum      = 0.0;
        *s_bw_samples      = 0;
        s_prev_dram_bw_sum = 0.0;
        s_prev_cxl_bw_sum  = 0.0;
        s_prev_bw_samples  = 0;

        pthread_mutex_unlock(s_snap_mutex);

        cf_run_analysis(cf_snap_buf, n, cur_dram,
                        total_stalls_win, total_cycles_win,
                        dram_bw_avg, cxl_bw_avg);
    }
    return NULL;
}


/* ================================================================
 * 7. Public API
 * ================================================================ */

void counterfactual_init(size_t max_pages)
{
    const char *bwlat_path = getenv("ARMS_BWLAT") ?: CF_BWLAT_DEFAULT;
    cf_load_bwlat_json(bwlat_path);

    cf_snap_buf = calloc(max_pages, sizeof(cf_page_snapshot_t));
    if (!cf_snap_buf) {
        LOG_ERROR("cf: OOM allocating snapshot buffer (%zu pages)\n", max_pages);
        return;
    }
    cf_snap_cap = max_pages;

    pthread_t tid;
    if (pthread_create(&tid, NULL, counterfactual_thread_fn, NULL) != 0) {
        LOG_ERROR("cf: pthread_create failed (%s)\n", strerror(errno));
        return;
    }
    pthread_detach(tid);

    LOG_REPORT("cf: initialized (CF_WINDOW_S=%d, CF_SWEEP_STEPS=%d,"
               " CF_CPU_FREQ_GHZ=%.1f,"
               " CF_SLOWDOWN_THRESHOLD=%.0f%%)\n",
               CF_WINDOW_S, CF_SWEEP_STEPS,
               CF_CPU_FREQ_GHZ,
               CF_SLOWDOWN_THRESHOLD * 100.0);
}

void counterfactual_register_state(
    void            *pages_map,
    int             *stall_fd,   uint64_t *stall_prev,
    int             *cycles_fd,  uint64_t *cycles_prev,
    uint64_t        *cf_dram_acc, uint64_t *cf_nvm_acc,
    double          *dram_bw_sum, double *cxl_bw_sum, int *bw_samples,
    uint64_t        *dramsize,
    pthread_mutex_t *snapshot_mutex,
    int             *tor_occ_fd, int *tor_act_fd,
    uint64_t        *prev_tor_occ, uint64_t *prev_tor_act,
    int              tor_cha_count)
{
    s_pages_map      = (khash_t(kPagesMap) *)pages_map;
    s_stall_fd       = stall_fd;
    s_stall_prev     = stall_prev;
    s_cycles_fd      = cycles_fd;
    s_cycles_prev    = cycles_prev;
    s_dram_acc       = cf_dram_acc;
    s_cxl_acc        = cf_nvm_acc;
    s_dram_bw_sum    = dram_bw_sum;
    s_cxl_bw_sum     = cxl_bw_sum;
    s_bw_samples     = bw_samples;
    s_dramsize       = dramsize;
    if (s_original_dramsize == 0)
        s_original_dramsize = *dramsize;
    s_snap_mutex     = snapshot_mutex;
    s_tor_occ_fd     = tor_occ_fd;
    s_tor_act_fd     = tor_act_fd;
    s_prev_tor_occ   = prev_tor_occ;
    s_prev_tor_act   = prev_tor_act;
    s_tor_cha_count  = tor_cha_count;
}
