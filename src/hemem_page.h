#pragma once

#include <math.h>
#include <pthread.h>

#include <stddef.h>
#include <assert.h>

#include "defs.h"

enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};
enum pbuftype { DRAMREAD = 0, NVMREAD = 1, WRITE = 2, NPBUFTYPES };
// defaults to Model since this would be all 0
enum prediction_type { ARMS = 0, MODEL = 1, NPREDICTIONTYPES };

struct score_entry {
  struct hemem_page* page;
  float score;
};
struct hemem_page {
    uint64_t va;
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

    uint32_t read_syscalls;
    uint32_t write_syscalls;
    uint32_t read_bytes;
    uint32_t write_bytes;
    uint64_t sum_malloc_bytes;
    int32_t min_malloc_bytes;
    int32_t max_malloc_bytes;
    uint32_t malloc_call;

    uint32_t prev_count;
    float w[WINDOW_SIZE];
    float w_r[WINDOW_SIZE];
    float w_w[WINDOW_SIZE];

    float malloc_call_ewma[WINDOW_SIZE];
    float malloc_size_ewma[WINDOW_SIZE];

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

    uint64_t _padding[6];
};
static_assert(sizeof(struct hemem_page) == 64 * 6);

void reset_page_access_fields(struct hemem_page *page);
float ewma(const float yp, const float x, const float alpha);
float adjusted_ewma(const float yp, const float x, const float denom);
float calculate_reads(const struct hemem_page *page, volatile uint8_t prev_access_version);
float calculate_writes(const struct hemem_page *page, volatile uint8_t prev_access_version);
float calculate_accesses(const struct hemem_page *page, volatile uint8_t prev_access_version);
void update_window(struct hemem_page *page, volatile uint8_t prev_access_version,
                                 const enum sampling_modes sampling_mode);
void update_derivative_features(struct hemem_page *page, size_t rank, size_t num_sorted_pages, size_t count_total);
float compute_score(const struct hemem_page *page, const float *bias);