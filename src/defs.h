#pragma once

#include <stdint.h>
#include <stdio.h>

#define C220G5

//#define HEMEM_DEBUG
#define STATS_THREAD

#define USE_DMA
#define NUM_CHANNS 2
#define SIZE_PER_DMA_REQUEST (1024 * 1024)

#define MEM_BARRIER() __sync_synchronize()

extern uint64_t min_interpose_mem_size;

#define NVMSIZE_DEFAULT (64L * (1024L * 1024L * 1024L))
#define DRAMSIZE_DEFAULT (32L * (1024L * 1024L * 1024L))

#define DRAMPATH_DEFAULT "/dev/dax0.0"
#define NVMPATH_DEFAULT "/dev/dax1.0"

//#define PAGE_SIZE (1024 * 1024 * 1024)
//#define PAGE_SIZE (2 * (1024 * 1024))
#define BASEPAGE_SIZE (4UL * 1024UL)
#define HUGEPAGE_SIZE (2UL * 1024UL * 1024UL) // 2MB
#define GIGAPAGE_SIZE (1024UL * 1024UL * 1024UL)
#define PAGE_SIZE HUGEPAGE_SIZE

#define MAX_NVME_PAGES (NVMSIZE_DEFAULT / PAGE_SIZE)
#define MAX_DRAM_PAGES (DRAMSIZE_DEFAULT / PAGE_SIZE)

#define BASEPAGE_MASK (BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK (HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK (BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK (HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK (GIGAPAGE_MASK ^ UINT64_MAX)

#define FAULT_THREAD_CPU (10)
#define STATS_THREAD_CPU (13)

extern FILE *hememlogf;
//#define LOG(...) fprintf(stderr, __VA_ARGS__)
//#define LOG(...)	fprintf(hememlogf, __VA_ARGS__)
#define MY_LOG(str, ...)                                                       \
    while (0) {                                                                \
    }

//#define LOG_TIME(str, ...) log_time(str, __VA_ARGS__)
//#define LOG_TIME(str, ...) fprintf(timef, str, __VA_ARGS__)
#define MY_LOG_TIME(str, ...)                                                  \
    while (0) {                                                                \
    }

extern FILE *statsf;
//#define MY_LOG_STATS(str, ...) fprintf(stderr, str, __VA_ARGS__)
//#define LOG_STATS(str, ...) fprintf(statsf, str, __VA_ARGS__)
#define MY_LOG_STATS(str, ...)                                                 \
    while (0) {                                                                \
    }

#define MAX_UFFD_MSGS (1)
#define MAX_COPY_THREADS (4)

#define MIN_INTERPOSE_MEM_SIZE_DEFAULT (1 * 1024UL * 1024UL * 1024UL)

#define MY_LOG_STREAM (stderr)

#define NUM_NEIGHBOURS (2)

#define WINDOW_SIZE (4)

// #define HIST_BIAS {.25, .25, .25, .25}
// With HIST_BIAS, we will prioritize longer histories
// #define HIST_BIAS {.155, .194, .259, .390}
// #define RECN_BIAS {.643, .236, .086, .032}
// TODO remove this bias stuff, we shouldn't need this
// actually leave this, we will need it in the future
#define HIST_BIAS                                                              \
    { 0.4, 0, 0.6, 0 } // Harmonic progression (1/3, 1/2)
#define RECN_BIAS                                                              \
    { 0.731, 0, 0.269, 0 } // Exponential (e-1, e-2)

enum sampling_modes {
    DEFAULT_SAMPLING = 0,
    HIGH_FIDELITY = 1,
    NUM_SAMPLING_MODES
};

enum imc_bw_counters { NVM_READS = 0, NVM_WRITES = 1, NUM_BW_COUNTERS };

// 2/(2^i + 1)
// #define W_EWMA_ALPHA {.6667, .3333, .0952, 0.0199} // 200ms, 500ms, 2s, 10s
// #define W_EWMA_ALPHA {0.1818, .0952, 0.0392, 0.0199} // 1s, 2s, 5s, 10s
#define W_EWMA_ALPHA                                                           \
    { 0.6667, 0.3334, 0.0952, 0.0198 } // windows of size 2, 5, 20, 100
static const float w_ewma_alpha[WINDOW_SIZE] = W_EWMA_ALPHA;
// #define W_EWMA_ALPHA {2, 5, 20, 100}

#define PEBS_KSWAPD_INTERVAL_BIG (1000000)   // in us (1000ms)
#define PEBS_KSWAPD_INTERVAL_SMALL (1000000) // in us (1000ms)

#define WRITES_WEIGHT                                                          \
    (1) // Peak NVM read bw ~ 40GB/s and peak NVM write bw ~ 13GB/s

//#define PEBS_KSWAPD_MIGRATE_RATE  (10UL * 1024UL * 1024UL * 1024UL) // 10GB
//#define HOT_READ_THRESHOLD        (8)
//#define HOT_WRITE_THRESHOLD       (4)
//#define PEBS_COOLING_THRESHOLD    (10)

//#define HOT_RING_REQS_THRESHOLD   (1024*1024)
//#define COLD_RING_REQS_THRESHOLD  (128)
#define CAPACITY (128 * 1024)
//#define COOLING_PAGES             (8192)

#define NUM_MIGRATION_THREADS (8)

#ifndef C220G5
#define PEBS_NPROCS 24
#else
#define PEBS_NPROCS 30 // C220g5 has 20 cores on NUMA node 0 (0-9,20-29)
#endif
#define PERF_PAGES (1 + (1 << 8)) // Has to be == 1+2^n, here 1MB
#define DEFAULT_SAMPLE_PERIOD 1001
#define HF_SAMPLE_PERIOD 1001
//#define SAMPLE_PERIOD 5003
//#define SAMPLE_FREQ	100

#define MAX_NVM_RD_BW (20) // GB/s
#define MAX_NVM_WR_BW (20) // GB/s

#define MIN_PROMOTION_DATACOPY_TIME                                            \
    (PAGE_SIZE / (MAX_NVM_RD_BW * 1024)) // ~ 700us
#define MIN_DEMOTION_DATACOPY_TIME                                             \
    (PAGE_SIZE / (MAX_NVM_WR_BW * 1024)) // ~ 1200us
#define MIGRATION_METADATA_COST (500)    // us

#define MIN_PROMOTION_COST                                                     \
    (MIN_PROMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)
#define MIN_DEMOTION_COST (MIN_DEMOTION_DATACOPY_TIME + MIGRATION_METADATA_COST)

#define MIGRATION_COST_ALPHA (0.0952) // Last 20 migrations
#define LATENCY_DIFF (0.1) // latency diff bw DRAM and NVM = 0.1us or 100ms

#define SCANNING_THREAD_CPU (FAULT_THREAD_CPU + 1)
#define MIGRATION_THREAD_CPU (SCANNING_THREAD_CPU + 1)

//#define COOL_IN_PLACE
//#define SAMPLE_BASED_COOLING
//#define SAMPLE_COOLING_THRESHOLD 10000

#define NUM_IMC 4 // IceLake has 4 iMCs
#define IMC_BASE_ADDR                                                          \
    0xFB900000 // TODO: Need to find this dynamically, currently obtained from
               // PCM

// There are 4 counters: DRAM Reads, DRAM Writes, PMM Reads, PMM Writes
// We are only interested in PMem bandwidth counters
#define PCM_SERVER_IMC_PMM_READS (0x22a0)
#define PCM_SERVER_IMC_PMM_WRITES (0x22a8)

#define PCM_SERVER_IMC_MMAP_SIZE (0x4000)
