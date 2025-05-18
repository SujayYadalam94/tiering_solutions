#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#include <inttypes.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <stdint.h>

#include "hemem_page.h"

enum sampling_modes {
    DEFAULT_SAMPLING = 0,
    HIGH_FIDELITY = 1,
    NUM_SAMPLING_MODES
};

enum imc_bw_counters { NVM_READS = 0, NVM_WRITES = 1, NUM_BW_COUNTERS };

struct perf_sample {
    struct perf_event_header header;
    __u64 ip;
    __u32 pid, tid; /* if PERF_SAMPLE_TID */
    __u64 addr;     /* if PERF_SAMPLE_ADDR */
                    // __u64 weight;      /* if PERF_SAMPLE_WEIGHT */
                    /* __u64 data_src;    /\* if PERF_SAMPLE_DATA_SRC *\/ */
};

struct score_entry {
    struct hemem_page *page;
    float score;
};

void *pebs_kswapd();
struct hemem_page *pebs_pagefault(void);
struct hemem_page *pebs_pagefault_unlocked(void);
void pebs_init(void);

void pebs_add_page(struct hemem_page *page);
struct hemem_page *pebs_find_page(uint64_t va);
void pebs_remove_page(struct hemem_page *page);

void pebs_stats();
void pebs_shutdown();

#endif /*  HEMEM_LRU_MODIFIED_H  */
