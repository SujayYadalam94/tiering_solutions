#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <numa.h>
#include <immintrin.h>

/* ---- configuration ---- */
#define LATENCY_BYTES   (16ULL * 1024 * 1024)   /* 16 MB > 14 MB L3 */
#define BW_BYTES        (64ULL * 1024 * 1024)   /* 64 MB BW buffer */
#define MEASURE_ITERS   4000000LL               /* pointer-chain accesses per point */
#define WARMUP_MS       300
#define MAX_BW_THREADS  18
#define N_BW_LEVELS     9

static const int BW_LEVELS[N_BW_LEVELS] = {0, 1, 2, 4, 6, 8, 10, 14, 18};

/* Node 0 CPUs excluding CPU 0 (reserved for main/latency thread) */
static const int BW_CPUS[MAX_BW_THREADS] = {
    1,2,3,4,5,6,7,8,9,20,21,22,23,24,25,26,27,28
};

/* ---- 64-byte pointer-chain element ---- */
typedef struct {
    uint64_t next;
    char     pad[56];
} __attribute__((aligned(64))) CL;

/* ---- per-BW-thread state ---- */
typedef struct {
    pthread_t        tid;
    int              id;
    char            *buf;
    size_t           buf_size;
    volatile long long bytes_read;
} BWThread;

/* ---- global synchronisation ---- */
static pthread_mutex_t g_mu   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv   = PTHREAD_COND_INITIALIZER;
static volatile int    g_nact = 0;
static volatile int    g_done = 0;

/* ---- timing ---- */
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static void msleep(int ms) {
    struct timespec t = {ms/1000, (long)(ms%1000)*1000000L};
    nanosleep(&t, NULL);
}

static void pin_cpu(int cpu) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
}

/* ---- Fisher-Yates Hamiltonian cycle through n cache lines ---- */
static void build_chain(CL *chain, size_t n) {
    uint64_t *p = malloc(n * sizeof(uint64_t));
    if (!p) { perror("malloc"); exit(1); }
    for (size_t i = 0; i < n; i++) p[i] = i;
    uint64_t r = 0xdeadbeef12345678ULL;
    for (size_t i = n-1; i > 0; i--) {
        r = r * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t j = r % (i+1);
        uint64_t t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (size_t i = 0; i < n; i++)
        chain[p[i]].next = p[(i+1) % n];
    free(p);
}

/* ---- non-temporal sequential read: generate bandwidth without polluting L3 ---- */
static size_t nt_read(char *buf, size_t sz) {
    __m128i *p   = (__m128i *)buf;
    __m128i *end = (__m128i *)(buf + sz);
    __m128i  acc = _mm_setzero_si128();
    while (p < end) {
        __m128i a = _mm_stream_load_si128(p);
        __m128i b = _mm_stream_load_si128(p+1);
        __m128i c = _mm_stream_load_si128(p+2);
        __m128i d = _mm_stream_load_si128(p+3);
        acc = _mm_xor_si128(acc, _mm_xor_si128(_mm_xor_si128(a,b), _mm_xor_si128(c,d)));
        p += 4;
    }
    asm volatile("" : "+x"(acc));
    return sz;
}

/* ---- BW thread: loop non-temporal reads while active ---- */
static void *bw_thread(void *arg) {
    BWThread *t = arg;
    pin_cpu(BW_CPUS[t->id]);
    while (1) {
        pthread_mutex_lock(&g_mu);
        while (t->id >= g_nact && !g_done)
            pthread_cond_wait(&g_cv, &g_mu);
        pthread_mutex_unlock(&g_mu);
        if (g_done) break;
        while (!g_done && t->id < g_nact) {
            size_t nb = nt_read(t->buf, t->buf_size);
            __atomic_fetch_add(&t->bytes_read, (long long)nb, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

/* ---- measure one (bw_gbps, lat_ns) point ---- */
static void measure_point(CL *chain, BWThread *thr, int n_active,
                           double *bw_out, double *lat_out) {
    pthread_mutex_lock(&g_mu);
    g_nact = n_active;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);

    msleep(WARMUP_MS);

    long long sb[MAX_BW_THREADS];
    for (int i = 0; i < n_active; i++)
        sb[i] = __atomic_load_n(&thr[i].bytes_read, __ATOMIC_RELAXED);

    uint64_t t0 = now_ns();
    uint64_t cur = 0;
    for (long long i = 0; i < MEASURE_ITERS; i++)
        cur = chain[cur].next;
    uint64_t t1 = now_ns();
    asm volatile("" : "+r"(cur));

    double elapsed = (double)(t1 - t0);
    *lat_out = elapsed / (double)MEASURE_ITERS;

    long long total = 0;
    for (int i = 0; i < n_active; i++) {
        long long eb = __atomic_load_n(&thr[i].bytes_read, __ATOMIC_RELAXED);
        total += eb - sb[i];
    }
    *bw_out = (total > 0) ? (double)total / elapsed : 0.0; /* bytes/ns = GB/s */

    pthread_mutex_lock(&g_mu);
    g_nact = 0;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    msleep(50);
}

/* ---- piecewise linear fit: lat = base + slope * max(0, bw - knee) ---- */
static void fit_model(double *bw, double *lat, int n,
                      double *base, double *knee, double *slope) {
    *base = lat[0];
    if (n < 3) { *knee = bw[n-1]; *slope = 0.0; return; }

    double best_sse = 1e300; int best_k = 1;
    for (int k = 1; k < n-1; k++) {
        double num = 0, den = 0;
        for (int i = k; i < n; i++) {
            double dx = bw[i] - bw[k], dy = lat[i] - *base;
            if (dx > 0) { num += dx*dy; den += dx*dx; }
        }
        double s = (den > 0) ? fmax(0.0, num/den) : 0.0;
        double sse = 0;
        for (int i = 0; i < n; i++) {
            double pred = (bw[i] > bw[k]) ? *base + s*(bw[i]-bw[k]) : *base;
            double r = lat[i] - pred; sse += r*r;
        }
        if (sse < best_sse) { best_sse = sse; best_k = k; }
    }
    *knee = bw[best_k];
    double num = 0, den = 0;
    for (int i = best_k; i < n; i++) {
        double dx = bw[i] - *knee, dy = lat[i] - *base;
        if (dx > 0) { num += dx*dy; den += dx*dx; }
    }
    *slope = (den > 0) ? fmax(0.0, num/den) : 0.0;
}

/* ---- print double (0.0 → "0.0", else %.17g) ---- */
static void pd(double v) {
    if (v == 0.0) printf("0.0");
    else          printf("%.17g", v);
}

/* ---- JSON section ---- */
static void print_section(const char *label, int last,
                           double base, double knee, double slope,
                           double *bw, double *lat, int n) {
    printf("  \"%s\": {\n", label);
    printf("    \"model\": {\n");
    printf("      \"base_latency_ns\": %.17g,\n", base);
    printf("      \"bw_knee_gbps\": %.17g,\n",    knee);
    printf("      \"slope_ns_per_gbps\": %.17g\n", slope);
    printf("    },\n");
    printf("    \"raw\": [\n");
    for (int i = 0; i < n; i++) {
        printf("      {\n");
        printf("        \"bw_gbps\": "); pd(bw[i]); printf(",\n");
        printf("        \"latency_ns\": "); pd(lat[i]); printf("\n");
        printf("      }%s\n", (i < n-1) ? "," : "");
    }
    printf("    ]\n");
    printf("  }%s\n", last ? "" : ",");
}

/* ---- sweep one NUMA node, collect (bw, lat) pairs ---- */
static void sweep(int node, double *bw_out, double *lat_out, int *n_out) {
    size_t n_cl  = LATENCY_BYTES / sizeof(CL);
    CL    *chain = numa_alloc_onnode(n_cl * sizeof(CL), node);
    char  *bwbuf = numa_alloc_onnode(BW_BYTES, node);
    if (!chain || !bwbuf) { fprintf(stderr, "numa_alloc_onnode failed\n"); exit(1); }

    /* fault in pages */
    memset(chain, 0, n_cl * sizeof(CL));
    memset(bwbuf, 0, BW_BYTES);

    build_chain(chain, n_cl);

    /* warm up: one full traversal to place chain in DRAM (not just page-mapped) */
    {
        volatile uint64_t cur = 0;
        for (size_t i = 0; i < n_cl; i++) cur = chain[cur].next;
        asm volatile("" : "+r"(cur));
    }

    /* per-thread BW buffer slices (64-byte aligned) */
    size_t per = (BW_BYTES / MAX_BW_THREADS) & ~(size_t)63;

    BWThread thr[MAX_BW_THREADS];
    g_done = 0; g_nact = 0;
    for (int i = 0; i < MAX_BW_THREADS; i++) {
        thr[i].id         = i;
        thr[i].buf        = bwbuf + (size_t)i * per;
        thr[i].buf_size   = per;
        thr[i].bytes_read = 0;
        pthread_create(&thr[i].tid, NULL, bw_thread, &thr[i]);
    }

    int n = 0;
    for (int li = 0; li < N_BW_LEVELS; li++) {
        int nact = BW_LEVELS[li];
        if (nact > MAX_BW_THREADS) nact = MAX_BW_THREADS;
        double bw, lat;
        measure_point(chain, thr, nact, &bw, &lat);
        bw_out[n]  = bw;
        lat_out[n] = lat;
        n++;
        fprintf(stderr, "  node%d n_bw=%2d: bw=%.2f GB/s  lat=%.1f ns\n",
                node, nact, bw, lat);
    }
    /* shut down threads */
    pthread_mutex_lock(&g_mu);
    g_done = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    for (int i = 0; i < MAX_BW_THREADS; i++) pthread_join(thr[i].tid, NULL);
    g_done = 0;

    /* Truncate at peak BW: post-saturation points have decreasing BW but rising
       latency, which scrambles the model fit.  Keep only the pre-saturation region. */
    int peak = 0;
    for (int i = 1; i < n; i++)
        if (bw_out[i] > bw_out[peak]) peak = i;
    n = peak + 1;
    *n_out = n;

    /* Sort pre-peak points by BW to handle minor noise (insertion sort, n<=9) */
    for (int i = 1; i < n; i++) {
        double bi = bw_out[i], li = lat_out[i]; int j = i-1;
        while (j >= 0 && bw_out[j] > bi) {
            bw_out[j+1] = bw_out[j]; lat_out[j+1] = lat_out[j]; j--;
        }
        bw_out[j+1] = bi; lat_out[j+1] = li;
    }

    numa_free(chain, n_cl * sizeof(CL));
    numa_free(bwbuf, BW_BYTES);
}

int main(void) {
    if (numa_available() < 0) { fprintf(stderr, "NUMA not available\n"); return 1; }
    if (numa_max_node() < 1)  { fprintf(stderr, "Need at least 2 NUMA nodes\n"); return 1; }

    pin_cpu(0);

    double cxl_bw[N_BW_LEVELS],  cxl_lat[N_BW_LEVELS];
    double dram_bw[N_BW_LEVELS], dram_lat[N_BW_LEVELS];
    int n_cxl, n_dram;

    fprintf(stderr, "Sweeping node 1 (cxl)...\n");
    sweep(1, cxl_bw, cxl_lat, &n_cxl);

    fprintf(stderr, "Sweeping node 0 (dram)...\n");
    sweep(0, dram_bw, dram_lat, &n_dram);

    double cxl_base,  cxl_knee,  cxl_slope;
    double dram_base, dram_knee, dram_slope;
    fit_model(cxl_bw,  cxl_lat,  n_cxl,  &cxl_base,  &cxl_knee,  &cxl_slope);
    fit_model(dram_bw, dram_lat, n_dram, &dram_base, &dram_knee, &dram_slope);

    printf("{\n");
    print_section("cxl",  0, cxl_base,  cxl_knee,  cxl_slope,  cxl_bw,  cxl_lat,  n_cxl);
    print_section("dram", 1, dram_base, dram_knee, dram_slope, dram_bw, dram_lat, n_dram);
    printf("}\n");
    return 0;
}
