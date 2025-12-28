#pragma once

#include "defs.h"

enum pbuftype
{
    DRAMREAD = 0,
    NVMREAD = 1,
    WRITE = 2,
    NPBUFTYPES
};

enum prediction_type
{
    ARMS = 0,
    MODEL = 1,
    NPREDICTIONTYPES
};

struct page_info
{
    uint64_t va; // Virtual address
    int prot;
    int flags;
    bool in_dram;
    uint32_t reads;
    uint32_t writes;
    uint32_t count;
    uint16_t accesses[NPBUFTYPES][2]; // Access counts per version
    std::mutex page_lock;

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

    float w_perc[WINDOW_SIZE];
    float w_r_perc[WINDOW_SIZE];
    float w_w_perc[WINDOW_SIZE];

    float malloc_call_ewma[WINDOW_SIZE];
    float malloc_size_ewma[WINDOW_SIZE];

    double cumsum_reads;
    double cumsum_writes;

    float score;
    float prev_score;
    uint16_t hot_age;
    bool can_promote;

    uint64_t last_seen_scan;

    uint32_t age;
    uint64_t age_count_total;

    uint32_t non_resetting_ewma100;
    uint32_t non_resetting_age;

    float accuracy;
    float model_score;
    float arms_score;
    enum prediction_type model_selection;

    int64_t global_count_since_top1_percent_ewma5;
    int64_t global_count_since_top50_percent_ewma5;
    uint32_t rank;
    float rank_perc;
    float rank_perc_ewma[WINDOW_SIZE];

    uint64_t global_count_similar;
    int32_t diff;

    // numa_move_pages status
    int seen_pages;
    int pages_in_dram;
    int page_status[BASE_PAGE_PER_HUGEPAGE];

    struct data_row *last_logged_row;

    page_info() : page_status{-1}, seen_pages(0), in_dram(false), can_promote(true), last_seen_scan(0)
    {
        this->reset_page_access_fields();
    }

    void reset_page_access_fields();
    float calculate_reads(volatile uint8_t prev_access_version);
    float calculate_writes(volatile uint8_t prev_access_version);
    float calculate_accesses(volatile uint8_t prev_access_version);
    void update_window(volatile uint8_t prev_access_version, const enum sampling_modes sampling_mode);
    void update_derivative_features(size_t rank, size_t num_sorted_pages, size_t count_total, size_t num_dram_pages);
    float compute_score(const float *bias);
};

// Score entry for sorting
struct score_entry
{
    page_info *page;
    float score;
};