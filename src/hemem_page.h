#pragma once

#include <math.h>
#include <pthread.h>

#include "pebs.h"
#include "defs.h"

enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};
enum pbuftype { DRAMREAD = 0, NVMREAD = 1, WRITE = 2, NPBUFTYPES };
// defaults to Model since this would be all 0
enum prediction_type { ARMS = 0, MODEL = 1, NPREDICTIONTYPES };

struct hemem_page {
    uint64_t va;
    size_t malloc_size;
    int prot;
    int flags;
    uint64_t devdax_offset;
    bool in_dram;
    enum pagetypes pt;
    volatile bool migrating;
    bool present;
    uint32_t reads;
    uint32_t writes;
    uint32_t count;
    uint16_t accesses[NPBUFTYPES][2];
    pthread_mutex_t page_lock;

    uint32_t _read_syscalls;
    uint32_t _write_syscalls;
    uint32_t _read_bytes;
    uint32_t _write_bytes;
    uint32_t _malloc_bytes;
    uint32_t _malloc_call;

    uint32_t read_syscalls;
    uint32_t write_syscalls;
    uint32_t read_bytes;
    uint32_t write_bytes;
    uint32_t malloc_bytes;
    uint32_t malloc_call;

    uint32_t prev_count;
    float w[WINDOW_SIZE];
    float w_r[WINDOW_SIZE];
    float w_w[WINDOW_SIZE];

    double cumsum_reads;
    double cumsum_writes;

    float score;
    float prev_score;
    uint16_t hot_age;
    bool can_promote;

    uint32_t age;

    struct hemem_page *next, *prev;
    struct fifo_list *list;

    float accuracy;
    float model_score;
    float arms_score;
    enum prediction_type model_selection;

    int64_t global_count_since_top1_percent_ewma5;
    int64_t global_count_since_top50_percent_ewma5;
    uint32_t rank;
    uint64_t global_count_similar;
    int32_t diff;

    uint64_t _padding[7];
};
static_assert(sizeof(struct hemem_page) == 64 * 6);

static void reset_page_access_fields(struct hemem_page *page) {
    for (int i = 0; i < NPBUFTYPES; i++) {
        page->accesses[i][0] = 0;
        page->accesses[i][1] = 0;
    }
    for (int i = 0; i < WINDOW_SIZE; i++) {
        page->w[i] = 0;
        page->w_r[i] = 0;
        page->w_w[i] = 0;
    }

    page->cumsum_reads = 0;
    page->cumsum_writes = 0;

    page->age = 0;
    page->hot_age = 0;
    page->prev_score = 0;
    page->accuracy = 0;

    page->prev_count = 0;

    page->_read_bytes = 0;
    page->_write_bytes = 0;
    page->_read_syscalls = 0;
    page->_write_syscalls = 0;
    page->_malloc_bytes = 0;
    page->_malloc_call = 0;
    page->read_bytes = 0;
    page->write_bytes = 0;
    page->read_syscalls = 0;
    page->write_syscalls = 0;
    page->malloc_bytes = 0;
    page->malloc_call = 0;
}

static inline float ewma(const float yp, const float x, const float alpha) {
    return (1. - alpha) * yp + (alpha * x);
}

static inline float adjusted_ewma(const float yp, const float x,
                                  const float denom) {
    return yp * (1 - (1 / denom)) + (x / denom);
}

static inline float calculate_reads(const struct hemem_page *page,
                                    volatile uint8_t prev_access_version) {
    return (page->accesses[DRAMREAD][prev_access_version] +
            page->accesses[NVMREAD][prev_access_version]);
}

static inline float calculate_writes(const struct hemem_page *page,
                                     volatile uint8_t prev_access_version) {
    return page->accesses[WRITE][prev_access_version];
}

static inline float calculate_accesses(const struct hemem_page *page,
                                       volatile uint8_t prev_access_version) {
    return calculate_reads(page, prev_access_version) +
           NVM_WRITES_WEIGHT * calculate_writes(page, prev_access_version);
}

static inline void update_window(struct hemem_page *page,
                                 volatile uint8_t prev_access_version,
                                 const enum sampling_modes sampling_mode) {
    page->read_bytes = page->_read_bytes;
    page->write_bytes = page->_write_bytes;
    page->read_syscalls = page->_read_syscalls;
    page->write_syscalls = page->_write_syscalls;
    page->malloc_bytes = page->_malloc_bytes;
    page->malloc_call = page->_malloc_call;
    page->_read_bytes = 0;
    page->_write_bytes = 0;
    page->_read_syscalls = 0;
    page->_write_syscalls = 0;
    page->_malloc_bytes = 0;
    page->_malloc_call = 0;

    page->age++;
    
    page->count = calculate_accesses(page, prev_access_version);
    page->reads = calculate_reads(page, prev_access_version);
    page->writes = calculate_writes(page, prev_access_version);



    page->cumsum_reads += page->reads;
    page->cumsum_writes += page->writes;

    page->diff = ((int32_t)page->count) - ((int32_t)page->prev_count);
    page->prev_count = page->count;

    float scaler = sampling_mode == DEFAULT_SAMPLING
                       ? DEFAULT_SAMPLE_PERIOD / HF_SAMPLE_PERIOD
                       : 1.0;
    for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
        page->w[i] = adjusted_ewma(page->w[i], page->count * scaler,
                                   get_adjusted_ewma_denom(i, page->age));
        page->w_r[i] = adjusted_ewma(page->w_r[i], page->reads,
                                     get_adjusted_ewma_denom(i, page->age));
        page->w_w[i] = adjusted_ewma(page->w_w[i], page->writes,
                                     get_adjusted_ewma_denom(i, page->age));
    }
}

static inline void
update_derivative_features(struct hemem_page *page,
                           struct hemem_page *sorted_pages,
                           size_t num_sorted_pages,
                           size_t count_total) {
    if ((page->count > 0.8f * page->prev_count &&
         page->count < 1.2f * page->prev_count) ||
        abs((int32_t)page->count - (int32_t)page->prev_count) < 2) {
        page->global_count_similar = page->global_count_similar + count_total;
    } else {
        page->global_count_similar = 0;
    }

    // time since top 1% and 50% in ewma5
    size_t idx = 0;
    for (idx = 0; idx < num_sorted_pages; idx++) {
        if (&sorted_pages[idx] == page) {
            break;
        }
    }
    page->rank = idx + 1;

    size_t top1_percent_index = num_sorted_pages / 100;
    size_t top50_percent_index = num_sorted_pages / 2;

    if (page->age == 0 && page->count == 0) {
        page->global_count_since_top1_percent_ewma5 = 0;
        page->global_count_since_top50_percent_ewma5 = 0;
        return;
    }

    if (idx <= top1_percent_index) {
        page->global_count_since_top1_percent_ewma5 =
            fmin(page->global_count_since_top1_percent_ewma5,
                     0) -
            count_total;
    } else {
        page->global_count_since_top1_percent_ewma5 =
            fmax(page->global_count_since_top1_percent_ewma5,
                     0) +
            count_total;
    }

    if (idx <= top50_percent_index) {
        page->global_count_since_top50_percent_ewma5 =
            fmin(page->global_count_since_top50_percent_ewma5,
                     0) -
            count_total;
    } else {
        page->global_count_since_top50_percent_ewma5 =
            fmax(page->global_count_since_top50_percent_ewma5,
                     0) +
            count_total;
    }
}

static inline float compute_score(const struct hemem_page *page,
                                  const float *bias) {
    // Update the score (average of the window)
    // TODO: The model inference goes here
    // Leave this code here. It will be useful for guardrails
    float score = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        score += page->w[i] * bias[i];
    }
    return score;
}