#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem.h"

#define LOG_STREAM (stdout)

#define WINDOW_SIZE (4)
#define NUM_NEIGHBOURS (4)

// #define HIST_BIAS {.25, .25, .25, .25}
// With HIST_BIAS, we will prioritize longer histories
#define HIST_BIAS {.155, .194, .259, .390}
#define RECN_BIAS {.643, .236, .086, .032}

// 2/(2^i + 1)
// #define W_EWMA_ALPHA {.6667, .3333, .0952, 0.0199} // 200ms, 500ms, 2s, 10s
#define W_EWMA_ALPHA {0.1818, .0952, 0.0392, 0.0199} // 1s, 2s, 5s, 10s
// #define W_EWMA_ALPHA {2, 5, 20, 100}

#define PEBS_KSWAPD_INTERVAL      (100000) // in us (10ms)

//#define PEBS_KSWAPD_MIGRATE_RATE  (10UL * 1024UL * 1024UL * 1024UL) // 10GB
//#define HOT_READ_THRESHOLD        (8)
//#define HOT_WRITE_THRESHOLD       (4)
//#define PEBS_COOLING_THRESHOLD    (10)

//#define HOT_RING_REQS_THRESHOLD   (1024*1024)
//#define COLD_RING_REQS_THRESHOLD  (128)
#define CAPACITY                  (128*1024)
//#define COOLING_PAGES             (8192)

//#define PAGE_ACCESS_SMOOTHING

#ifndef C220G5
#define PEBS_NPROCS 24
#else
#define PEBS_NPROCS 30 // C220g5 has 20 cores on NUMA node 0 (0-9,20-29)
#endif
#define PERF_PAGES	(1 + (1 << 8))	// Has to be == 1+2^n, here 1MB
#define SAMPLE_PERIOD	10007
#define WRITE_SAMPLE_PERIOD	10007
//#define SAMPLE_PERIOD 5003
//#define SAMPLE_FREQ	100


#define SCANNING_THREAD_CPU (FAULT_THREAD_CPU + 1)
#define MIGRATION_THREAD_CPU (SCANNING_THREAD_CPU + 1)

//#define COOL_IN_PLACE
//#define SAMPLE_BASED_COOLING
//#define SAMPLE_COOLING_THRESHOLD 10000

#define NUM_IMC 4                 // IceLake has 4 iMCs
#define IMC_BASE_ADDR 0xFB900000  // TODO: Need to find this dynamically, currently obtained from PCM

// There are 4 counters: DRAM Reads, DRAM Writes, PMM Reads, PMM Writes
// We are only interested in PMem bandwidth counters
#define PCM_SERVER_IMC_PMM_READS   (0x22a0)
#define PCM_SERVER_IMC_PMM_WRITES  (0x22a8)

#define PCM_SERVER_IMC_MMAP_SIZE   (0x4000)

enum imc_bw_counters {
  PMM_READS = 0,
  PMM_WRITES = 1,
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
