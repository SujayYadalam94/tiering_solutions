/*
 * bwlat — loaded-latency profiler for local (DRAM) and remote (CXL-emulating)
 * NUMA memory.  Produces the bw-lat.dat JSON consumed by ARMS
 * (see arms/src/counterfactual.h, CF_BWLAT_DEFAULT).
 *
 * Methodology (same idea as Intel MLC --loaded_latency):
 *   - All threads run on the "application" node (--cpu-node, default 0).
 *   - One latency thread walks a randomly permuted pointer chain that lives
 *     on the target node; every hop is a dependent 64 B load, so
 *     elapsed_time / hops is the average load-to-use latency.
 *   - The remaining CPUs of the node run bandwidth threads that stream
 *     read-only over their own buffers on the target node.  After every
 *     512 B chunk each thread spins for `delay` TSC ticks; sweeping delay
 *     from "paused" down to 0 sweeps the load from idle to saturation.
 *   - For every load level we report total read bandwidth (bandwidth
 *     threads + latency thread) and the latency seen by the chase thread.
 *
 * Only reads are generated, matching ARMS, which feeds read bandwidth into
 * the latency curve.
 *
 * Output schema (counterfactual.h):
 *   { "dram": { "raw": [ {"bw_gbps": F, "latency_ns": F}, ... ] },
 *     "cxl":  { "raw": [ ... ] },
 *     "meta": { ... } }
 * ARMS locates tiers with strstr(), so "dram" and "cxl" are emitted first
 * and nothing before them contains those keys.  "meta" is ignored by ARMS.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <immintrin.h>
#include <math.h>
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#define CL_SIZE        64
#define CHUNK_BYTES    512                 /* bytes read between delay spins */
#define CHUNKS_PER_CHECK 128               /* 64 KB between delay/stop checks */
#define HUGE_SZ        (2UL << 20)
#define MAX_CPUS       1024
#define MAX_POINTS     64                  /* CF_MAX_CURVE_POINTS in ARMS */
#define DELAY_PAUSED   UINT64_MAX          /* bandwidth threads idle */
#define CHASE_BATCH    256                 /* hops between progress publishes */
#define MAX_DELAY_TICKS 40000.0
#define MIN_DELAY_TICKS 20.0
#define COARSE_POINTS   20                 /* delay->bw map for planning */
#define COARSE_MEASURE_S 0.15
#define SUB_WINDOWS     5                  /* median-of slices per point */

/* ------------------------------------------------------------------ */
/* Options                                                            */
/* ------------------------------------------------------------------ */
static int    opt_cpu_node   = 0;
static int    opt_dram_node  = 0;
static int    opt_cxl_node   = 1;
static int    opt_threads    = -1;         /* -1: all usable CPUs on cpu node */
static size_t opt_chain_mb   = 1024;
static size_t opt_buf_mb     = 128;        /* per bandwidth thread */
static int    opt_points     = 24;         /* load levels per tier */
static double opt_warmup_s   = 0.3;
static double opt_measure_s  = 1.0;
static int    opt_keep_all   = 0;
static double opt_min_step   = 0.01;       /* min relative bw gain per point */
static const char *opt_out   = "bw-lat.dat";
static const char *opt_csv   = "bw-lat.csv";

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void sleep_s(double s)
{
    struct timespec t = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
    while (nanosleep(&t, &t) == -1 && errno == EINTR)
        ;
}

static void pin_self(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
    if (rc) {
        fprintf(stderr, "pin to cpu %d failed: %s\n", cpu, strerror(rc));
        exit(1);
    }
}

/* Return the other hyperthread of `cpu`, or -1 if none. */
static int smt_sibling(int cpu)
{
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int a = -1, b = -1;
    char sep;
    int n = fscanf(f, "%d%c%d", &a, &sep, &b);
    fclose(f);
    if (n < 3) return -1;
    return a == cpu ? b : a;
}

/*
 * Allocate `size` bytes bound to `node`, 2 MB aligned, THP-advised and
 * pre-faulted.  Returns the aligned pointer; base and len receive what to
 * munmap.
 */
static void *alloc_on_node(size_t size, int node, void **base, size_t *len)
{
    size = (size + HUGE_SZ - 1) & ~(HUGE_SZ - 1);
    *len = size + HUGE_SZ;
    *base = mmap(NULL, *len, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (*base == MAP_FAILED) { perror("mmap"); exit(1); }
    char *p = (char *)(((uintptr_t)*base + HUGE_SZ - 1) & ~(HUGE_SZ - 1));

    unsigned long mask[MAX_CPUS / 64] = {0};
    mask[node / 64] |= 1UL << (node % 64);
    if (mbind(p, size, MPOL_BIND, mask, MAX_CPUS, MPOL_MF_STRICT) != 0) {
        perror("mbind");
        exit(1);
    }
    madvise(p, size, MADV_HUGEPAGE);
    memset(p, 0, size);  /* fault in now so the sweep never page-faults */
    return p;
}

/* Sample pages and confirm they really live on `node`. */
static void verify_node(void *p, size_t size, int node, const char *what)
{
    int bad = 0, total = 0;
    for (size_t off = 0; off < size; off += size / 64 + 4096, total++) {
        int actual = -1;
        if (get_mempolicy(&actual, NULL, 0, (char *)p + off,
                          MPOL_F_NODE | MPOL_F_ADDR) == 0 && actual != node)
            bad++;
    }
    if (bad) {
        fprintf(stderr, "ERROR: %d/%d sampled pages of %s not on node %d\n",
                bad, total, what, node);
        exit(1);
    }
}

/* AnonHugePages (kB) backing [p, p+size), from /proc/self/smaps. */
static size_t thp_backed_kb(void *p, size_t size)
{
    FILE *f = fopen("/proc/self/smaps", "r");
    if (!f) return 0;
    char line[512];
    int in = 0;
    size_t kb = 0;
    uintptr_t lo = (uintptr_t)p, hi = lo + size;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t a, b;
        size_t v;
        if (sscanf(line, "%lx-%lx ", &a, &b) == 2)
            in = (a < hi && b > lo);
        else if (in && sscanf(line, "AnonHugePages: %zu kB", &v) == 1)
            kb += v;
    }
    fclose(f);
    return kb;
}

static void read_sysfs(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    out[0] = '\0';
    if (!f) { snprintf(out, n, "unknown"); return; }
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = '\0';
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Worker threads                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    _Atomic uint64_t val;
    char pad[CL_SIZE - sizeof(uint64_t)];
} __attribute__((aligned(CL_SIZE))) counter_t;

static _Atomic uint64_t g_delay;   /* TSC ticks after each chunk */
static _Atomic int      g_stop;

typedef struct {
    int cpu;
    char *buf;
    size_t size;
    counter_t *bytes;
} bw_arg_t;

typedef struct {
    int cpu;
    const uint64_t *chain;         /* chain[i*8] holds the next line index */
    counter_t *hops;
} lat_arg_t;

static void *bw_thread(void *v)
{
    bw_arg_t *a = v;
    pin_self(a->cpu);
    const char *p = a->buf, *end = a->buf + a->size;
    __m256i acc0 = _mm256_setzero_si256(), acc1 = acc0, acc2 = acc0, acc3 = acc0;
    uint64_t bytes = 0;

    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        uint64_t delay = atomic_load_explicit(&g_delay, memory_order_relaxed);
        if (delay == DELAY_PAUSED) {
            _mm_pause();
            continue;
        }
        for (int c = 0; c < CHUNKS_PER_CHECK; c++) {
            const __m256i *q = (const __m256i *)p;
            for (int i = 0; i < CHUNK_BYTES / 32; i += 4) {
                acc0 = _mm256_xor_si256(acc0, _mm256_load_si256(q + i));
                acc1 = _mm256_xor_si256(acc1, _mm256_load_si256(q + i + 1));
                acc2 = _mm256_xor_si256(acc2, _mm256_load_si256(q + i + 2));
                acc3 = _mm256_xor_si256(acc3, _mm256_load_si256(q + i + 3));
            }
            p += CHUNK_BYTES;
            if (p >= end) p = a->buf;
            if (delay) {
                uint64_t t = __rdtsc() + delay;
                while (__rdtsc() < t)
                    _mm_pause();
            }
        }
        bytes += (uint64_t)CHUNKS_PER_CHECK * CHUNK_BYTES;
        atomic_store_explicit(&a->bytes->val, bytes, memory_order_relaxed);
    }
    /* Keep the loads observable so the compiler cannot drop them. */
    acc0 = _mm256_xor_si256(_mm256_xor_si256(acc0, acc1),
                            _mm256_xor_si256(acc2, acc3));
    volatile int sink = _mm256_extract_epi32(acc0, 0);
    (void)sink;
    return NULL;
}

static void *lat_thread(void *v)
{
    lat_arg_t *a = v;
    pin_self(a->cpu);
    const uint64_t *c = a->chain;
    uint64_t idx = 0, hops = 0;

    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        for (int i = 0; i < CHASE_BATCH; i += 8) {
            idx = c[idx * 8]; idx = c[idx * 8]; idx = c[idx * 8]; idx = c[idx * 8];
            idx = c[idx * 8]; idx = c[idx * 8]; idx = c[idx * 8]; idx = c[idx * 8];
        }
        hops += CHASE_BATCH;
        atomic_store_explicit(&a->hops->val, hops, memory_order_relaxed);
    }
    volatile uint64_t sink = idx;
    (void)sink;
    return NULL;
}

/* Random Hamiltonian cycle over n cache lines (one 8-byte link per line). */
static void build_chain(uint64_t *chain, size_t n)
{
    uint32_t *perm = malloc(n * sizeof(*perm));
    if (!perm) { perror("malloc"); exit(1); }
    for (size_t i = 0; i < n; i++) perm[i] = (uint32_t)i;
    uint64_t r = 0x9e3779b97f4a7c15ULL;
    for (size_t i = n - 1; i > 0; i--) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17;
        size_t j = r % (i + 1);
        uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (size_t i = 0; i < n; i++)
        chain[(size_t)perm[i] * 8] = perm[(i + 1) % n];
    free(perm);
}

/* ------------------------------------------------------------------ */
/* Sweep                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t delay;     /* DELAY_PAUSED == idle */
    double bw_gbps;
    double lat_ns;
    int kept;
} point_t;

static int g_lat_cpu;
static int g_bw_cpus[MAX_CPUS];
static int g_n_bw;

static uint64_t sum_bytes(counter_t *c, int n)
{
    uint64_t s = 0;
    for (int i = 0; i < n; i++)
        s += atomic_load_explicit(&c[i].val, memory_order_relaxed);
    return s;
}

static int cmp_lat(const void *a, const void *b)
{
    const point_t *x = a, *y = b;
    return (x->lat_ns > y->lat_ns) - (x->lat_ns < y->lat_ns);
}

/*
 * Set the load level, let it settle, and measure one point.  The window is
 * split into SUB_WINDOWS slices and the slice with the median latency is
 * reported, so short bursts of outside interference do not skew the point.
 */
static point_t measure(uint64_t delay, double warmup, double dur,
                       counter_t *bctr, counter_t *hops)
{
    atomic_store(&g_delay, delay);
    sleep_s(warmup);

    point_t sub[SUB_WINDOWS];
    uint64_t b0 = sum_bytes(bctr, g_n_bw);
    uint64_t h0 = atomic_load(&hops->val);
    double t0 = now_s();
    for (int i = 0; i < SUB_WINDOWS; i++) {
        sleep_s(dur / SUB_WINDOWS);
        uint64_t b1 = sum_bytes(bctr, g_n_bw);
        uint64_t h1 = atomic_load(&hops->val);
        double t1 = now_s(), dt = t1 - t0;
        double dh = (double)(h1 - h0);
        sub[i] = (point_t){ delay, 0.0, 0.0, 1 };
        sub[i].lat_ns = dh > 0 ? dt * 1e9 / dh : 0.0;
        sub[i].bw_gbps = ((double)(b1 - b0) + dh * CL_SIZE) / dt / 1e9;
        b0 = b1; h0 = h1; t0 = t1;
    }
    qsort(sub, SUB_WINDOWS, sizeof(point_t), cmp_lat);
    return sub[SUB_WINDOWS / 2];
}

/*
 * Choose the delays for the real sweep.  A quick coarse pass over geometric
 * delays maps delay -> bandwidth; delays for target bandwidths are then
 * found by inverse interpolation in log(delay).  Targets run from idle to
 * peak bandwidth, packed more densely towards the saturation knee, which is
 * where latency changes fastest.
 *
 * Returns the number of delays: paused, n-2 targets, then 0 (unthrottled),
 * with duplicates removed.
 */
static int plan_delays(uint64_t *d, int n, counter_t *bctr, counter_t *hops)
{
    double lg[COARSE_POINTS], bw[COARSE_POINTS];
    double lo = measure(DELAY_PAUSED, 0.1, COARSE_MEASURE_S, bctr, hops).bw_gbps;
    double hi = 0.0;
    for (int i = 0; i < COARSE_POINTS; i++) {
        double f = (double)i / (COARSE_POINTS - 1);
        uint64_t dl = (uint64_t)(MAX_DELAY_TICKS *
                                 pow(MIN_DELAY_TICKS / MAX_DELAY_TICKS, f));
        if (i == COARSE_POINTS - 1) dl = 0;
        point_t p = measure(dl, 0.1, COARSE_MEASURE_S, bctr, hops);
        lg[i] = log((double)dl + 1.0);
        /* Running max keeps the map monotone through saturation noise. */
        hi = fmax(hi, p.bw_gbps);
        bw[i] = hi;
    }

    int k = 0, targets = n - 2;
    d[k++] = DELAY_PAUSED;
    for (int i = 1; i <= targets; i++) {
        double u = (double)i / targets;
        double t = lo + (hi - lo) * (1.0 - pow(1.0 - u, 1.5));
        int j = 0;
        while (j < COARSE_POINTS - 1 && bw[j] < t) j++;
        double x = lg[j];
        if (j > 0 && bw[j] > bw[j - 1])
            x = lg[j - 1] + (t - bw[j - 1]) / (bw[j] - bw[j - 1]) * (lg[j] - lg[j - 1]);
        uint64_t dl = (uint64_t)llround(exp(x) - 1.0);
        if (dl > 0 && (d[k - 1] == DELAY_PAUSED || dl < d[k - 1]))
            d[k++] = dl;
    }
    d[k++] = 0;
    return k;
}

static int sweep_node(int node, const char *label, point_t *pts)
{
    fprintf(stderr, "\n== %s: memory on node %d, threads on node %d "
            "(lat cpu %d, %d bw threads) ==\n",
            label, node, opt_cpu_node, g_lat_cpu, g_n_bw);

    /* Pointer chain. */
    size_t chain_bytes = opt_chain_mb << 20;
    size_t n_lines = chain_bytes / CL_SIZE;
    void *cbase; size_t clen;
    uint64_t *chain = alloc_on_node(chain_bytes, node, &cbase, &clen);
    verify_node(chain, chain_bytes, node, "chain");
    build_chain(chain, n_lines);
    size_t thp = thp_backed_kb(chain, chain_bytes);
    fprintf(stderr, "chain: %zu MB, %zu%% THP-backed\n", opt_chain_mb,
            thp * 100 / (chain_bytes >> 10));
    if (thp * 2 < (chain_bytes >> 10))
        fprintf(stderr, "WARNING: chain mostly on 4K pages; latency will "
                "include TLB misses\n");

    /* Bandwidth buffers. */
    size_t buf_bytes = opt_buf_mb << 20;
    void **bbase = calloc(g_n_bw, sizeof(void *));
    size_t *blen = calloc(g_n_bw, sizeof(size_t));
    bw_arg_t *bargs = calloc(g_n_bw, sizeof(bw_arg_t));
    counter_t *bctr = aligned_alloc(CL_SIZE, (g_n_bw + 1) * sizeof(counter_t));
    memset(bctr, 0, (g_n_bw + 1) * sizeof(counter_t));
    counter_t *hops = &bctr[g_n_bw];
    for (int i = 0; i < g_n_bw; i++) {
        bargs[i] = (bw_arg_t){ g_bw_cpus[i],
                               alloc_on_node(buf_bytes, node, &bbase[i], &blen[i]),
                               buf_bytes, &bctr[i] };
        verify_node(bargs[i].buf, buf_bytes, node, "bw buffer");
    }

    /* Start threads, bandwidth threads paused. */
    atomic_store(&g_stop, 0);
    atomic_store(&g_delay, DELAY_PAUSED);
    pthread_t lt, *bt = calloc(g_n_bw, sizeof(pthread_t));
    lat_arg_t largs = { g_lat_cpu, chain, hops };
    pthread_create(&lt, NULL, lat_thread, &largs);
    for (int i = 0; i < g_n_bw; i++)
        pthread_create(&bt[i], NULL, bw_thread, &bargs[i]);

    uint64_t delays[MAX_POINTS];
    int n = plan_delays(delays, opt_points, bctr, hops);
    fprintf(stderr, "%6s %12s %10s %10s\n", "level", "delay_ticks", "bw_GB/s", "lat_ns");
    for (int k = 0; k < n; k++) {
        pts[k] = measure(delays[k], opt_warmup_s, opt_measure_s, bctr, hops);
        if (delays[k] == DELAY_PAUSED)
            fprintf(stderr, "%6d %12s %10.2f %10.1f\n", k, "idle",
                    pts[k].bw_gbps, pts[k].lat_ns);
        else
            fprintf(stderr, "%6d %12lu %10.2f %10.1f\n", k,
                    (unsigned long)delays[k], pts[k].bw_gbps, pts[k].lat_ns);
    }

    atomic_store(&g_stop, 1);
    pthread_join(lt, NULL);
    for (int i = 0; i < g_n_bw; i++) {
        pthread_join(bt[i], NULL);
        munmap(bbase[i], blen[i]);
    }
    munmap(cbase, clen);
    free(bt); free(bargs); free(bbase); free(blen); free(bctr);
    return n;
}

/*
 * ARMS interpolates latency as a function of bandwidth, so bw must be
 * non-decreasing.  Past saturation, more offered load can lower achieved
 * bandwidth while latency keeps rising (the curve "folds back"); those
 * points are not a function of bw.  Points that add less than
 * --min-step of bandwidth over the previous kept point are dropped too:
 * ARMS extrapolates beyond the last point with the last segment's slope,
 * and two near-identical bw values make that slope explode.
 * Points are visited from lightest to heaviest load.
 */
static void filter_monotone(point_t *pts, int n)
{
    double max_bw = -1.0;
    for (int k = 0; k < n; k++) {
        if (pts[k].bw_gbps > max_bw * (1.0 + opt_min_step)) {
            max_bw = pts[k].bw_gbps;
        } else if (!opt_keep_all) {
            pts[k].kept = 0;
        }
    }
}

static int cmp_bw(const void *a, const void *b)
{
    const point_t *x = a, *y = b;
    return (x->bw_gbps > y->bw_gbps) - (x->bw_gbps < y->bw_gbps);
}

static void emit_tier(FILE *f, const char *label, point_t *pts, int n, int last)
{
    point_t sorted[MAX_POINTS];
    int m = 0;
    for (int k = 0; k < n; k++)
        if (pts[k].kept) sorted[m++] = pts[k];
    qsort(sorted, m, sizeof(point_t), cmp_bw);

    fprintf(f, "  \"%s\": {\n    \"raw\": [\n", label);
    for (int k = 0; k < m; k++)
        fprintf(f, "      {\"bw_gbps\": %.4f, \"latency_ns\": %.2f}%s\n",
                sorted[k].bw_gbps, sorted[k].lat_ns, k < m - 1 ? "," : "");
    fprintf(f, "    ]\n  }%s\n", last ? "" : ",");
}

static void emit_csv(FILE *f, const char *label, int node, point_t *pts, int n)
{
    for (int k = 0; k < n; k++)
        fprintf(f, "%s,%d,%d,%s,%.4f,%.2f,%d\n", label, node, k,
                pts[k].delay == DELAY_PAUSED ? "idle" : "",
                pts[k].bw_gbps, pts[k].lat_ns, pts[k].kept);
}

/* ------------------------------------------------------------------ */
/* CPU selection                                                      */
/* ------------------------------------------------------------------ */
static void pick_cpus(void)
{
    struct bitmask *cpus = numa_allocate_cpumask();
    if (numa_node_to_cpus(opt_cpu_node, cpus) != 0) {
        perror("numa_node_to_cpus");
        exit(1);
    }
    cpu_set_t allowed;
    sched_getaffinity(0, sizeof(allowed), &allowed);

    int list[MAX_CPUS], n = 0;
    for (int c = 0; c < MAX_CPUS && c < (int)cpus->size; c++)
        if (numa_bitmask_isbitset(cpus, c) && CPU_ISSET(c, &allowed))
            list[n++] = c;
    numa_free_cpumask(cpus);
    if (n < 2) {
        fprintf(stderr, "need >= 2 usable CPUs on node %d\n", opt_cpu_node);
        exit(1);
    }

    /* Latency thread gets the last CPU's core to itself (its SMT sibling is
     * left idle) so bandwidth threads do not steal its pipeline. */
    g_lat_cpu = list[n - 1];
    int sib = smt_sibling(g_lat_cpu);
    g_n_bw = 0;
    for (int i = 0; i < n; i++)
        if (list[i] != g_lat_cpu && list[i] != sib)
            g_bw_cpus[g_n_bw++] = list[i];
    if (opt_threads >= 0 && opt_threads < g_n_bw)
        g_n_bw = opt_threads;
    if (g_n_bw == 0) {
        fprintf(stderr, "no CPUs left for bandwidth threads\n");
        exit(1);
    }
}

static void usage(const char *p)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  -c, --cpu-node N    node whose CPUs run the workload (default 0)\n"
        "  -d, --dram-node N   local/fast memory node -> \"dram\" (default 0)\n"
        "  -x, --cxl-node N    remote/slow memory node -> \"cxl\" (default 1)\n"
        "  -t, --threads N     bandwidth threads (default: all usable CPUs)\n"
        "  -p, --points N      load levels per tier, 3..%d (default 24)\n"
        "  -m, --measure S     seconds measured per level (default 1.0)\n"
        "  -w, --warmup S      seconds settled per level (default 0.3)\n"
        "      --chain-mb MB   latency chain size (default 1024)\n"
        "      --buf-mb MB     per-thread bandwidth buffer (default 128)\n"
        "  -o, --out FILE      JSON output for ARMS (default bw-lat.dat)\n"
        "      --csv FILE      every measured point (default bw-lat.csv)\n"
        "      --min-step F    min relative bw gain between kept points (default 0.01)\n"
        "      --keep-all      keep every measured point (no filtering)\n",
        p, MAX_POINTS);
    exit(1);
}

int main(int argc, char **argv)
{
    static struct option lo[] = {
        {"cpu-node", 1, 0, 'c'}, {"dram-node", 1, 0, 'd'},
        {"cxl-node", 1, 0, 'x'}, {"threads", 1, 0, 't'},
        {"points", 1, 0, 'p'},   {"measure", 1, 0, 'm'},
        {"warmup", 1, 0, 'w'},   {"out", 1, 0, 'o'},
        {"chain-mb", 1, 0, 1},   {"buf-mb", 1, 0, 2},
        {"csv", 1, 0, 3},        {"keep-all", 0, 0, 4},
        {"min-step", 1, 0, 5},
        {"help", 0, 0, 'h'},     {0, 0, 0, 0}
    };
    int o;
    while ((o = getopt_long(argc, argv, "c:d:x:t:p:m:w:o:h", lo, NULL)) != -1) {
        switch (o) {
        case 'c': opt_cpu_node  = atoi(optarg); break;
        case 'd': opt_dram_node = atoi(optarg); break;
        case 'x': opt_cxl_node  = atoi(optarg); break;
        case 't': opt_threads   = atoi(optarg); break;
        case 'p': opt_points    = atoi(optarg); break;
        case 'm': opt_measure_s = atof(optarg); break;
        case 'w': opt_warmup_s  = atof(optarg); break;
        case 'o': opt_out       = optarg; break;
        case 1:   opt_chain_mb  = strtoul(optarg, NULL, 0); break;
        case 2:   opt_buf_mb    = strtoul(optarg, NULL, 0); break;
        case 3:   opt_csv       = optarg; break;
        case 4:   opt_keep_all  = 1; break;
        case 5:   opt_min_step  = atof(optarg); break;
        default:  usage(argv[0]);
        }
    }
    if (opt_points < 3 || opt_points > MAX_POINTS || opt_measure_s <= 0)
        usage(argv[0]);
    if (numa_available() < 0) { fprintf(stderr, "NUMA not available\n"); return 1; }
    int maxn = numa_max_node();
    if (opt_cpu_node > maxn || opt_dram_node > maxn || opt_cxl_node > maxn) {
        fprintf(stderr, "node out of range (max node %d)\n", maxn);
        return 1;
    }

    char gov[64];
    read_sysfs("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", gov, sizeof(gov));
    if (strcmp(gov, "performance") != 0)
        fprintf(stderr, "WARNING: cpufreq governor is '%s'; 'performance' "
                "gives more stable latency\n", gov);

    pick_cpus();

    point_t dram[MAX_POINTS], cxl[MAX_POINTS];
    int nd = sweep_node(opt_dram_node, "dram", dram);
    int nc = sweep_node(opt_cxl_node, "cxl", cxl);
    filter_monotone(dram, nd);
    filter_monotone(cxl, nc);

    FILE *f = fopen(opt_out, "w");
    if (!f) { perror(opt_out); return 1; }
    fprintf(f, "{\n");
    emit_tier(f, "dram", dram, nd, 0);
    emit_tier(f, "cxl", cxl, nc, 0);
    time_t now = time(NULL);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime(&now));
    char host[128] = "";
    gethostname(host, sizeof(host) - 1);
    fprintf(f, "  \"meta\": {\"tool\": \"bwlat\", \"host\": \"%s\", "
            "\"timestamp\": \"%s\", \"cpu_node\": %d, \"dram_node\": %d, "
            "\"cxl_node\": %d, \"latency_cpu\": %d, \"bw_threads\": %d, "
            "\"chain_mb\": %zu, \"buf_mb\": %zu, \"measure_s\": %.2f, "
            "\"governor\": \"%s\", \"access\": \"read-only\"}\n",
            host, ts, opt_cpu_node, opt_dram_node, opt_cxl_node, g_lat_cpu,
            g_n_bw, opt_chain_mb, opt_buf_mb, opt_measure_s, gov);
    fprintf(f, "}\n");
    fclose(f);

    FILE *c = fopen(opt_csv, "w");
    if (c) {
        fprintf(c, "tier,node,level,note,bw_gbps,latency_ns,kept\n");
        emit_csv(c, "dram", opt_dram_node, dram, nd);
        emit_csv(c, "cxl", opt_cxl_node, cxl, nc);
        fclose(c);
    }

    int kd = 0, kc = 0;
    for (int k = 0; k < nd; k++) kd += dram[k].kept;
    for (int k = 0; k < nc; k++) kc += cxl[k].kept;
    fprintf(stderr, "\nwrote %s (dram: %d pts, cxl: %d pts) and %s\n",
            opt_out, kd, kc, opt_csv);
    return 0;
}
