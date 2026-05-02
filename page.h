#pragma once

#include "defs.h"
#include <memory>

enum pbuftype
{
    READ = 0,
    WRITE = 1,
    NPBUFTYPES
};

enum prediction_type
{
    ARMS = 0,
    MODEL = 1,
    NPREDICTIONTYPES
};

float ewma(const float yp, const float x, const float alpha);
float adjusted_ewma(const float yp, const float x, const float denom);

struct page_info
{
    uint64_t va; // Virtual address
    int prot;
    int flags;
    bool in_dram;

    // if it is in pebs we need to check if it is fragmented or not before removing it
    bool found_in_pebs;
    // if it is fragmented, we can't remove it
    bool fragmented;
    bool would_migrate_fragmented;

    uint32_t reads;
    uint32_t writes;
    uint32_t count;
    uint32_t accesses[NPBUFTYPES][2]; // Access counts per version
    std::mutex page_lock;

    uint32_t read_bytes;
    uint32_t write_bytes;

    uint32_t prev_count;
    float w[WINDOW_SIZE];
    float w_r[WINDOW_SIZE];
    float w_w[WINDOW_SIZE];

    float w_perc_second_moment[WINDOW_SIZE];
    float w_perc_var[WINDOW_SIZE];

    float w_perc[WINDOW_SIZE];
    float w_r_perc[WINDOW_SIZE];
    float w_w_perc[WINDOW_SIZE];

    double cumsum_reads;
    double cumsum_writes;
    double global_avg_accesses;
    double global_avg_accesses_perc;
    float gap4;
    float read_write_gap3;

    float score;
    float prev_score;
    uint16_t hot_age;
    bool can_promote;

    uint64_t last_seen_scan;
    uint64_t last_access_generation;

    uint32_t age;
    uint64_t age_count_total;

    uint32_t non_resetting_ewma100;
    uint32_t non_resetting_age;

    float accuracy;
    float model_score_history[HISTORY_LENGTH];
    uint8_t model_score_history_count;
    uint8_t model_score_history_index;
    float adjusted_model_score_moving_average;
    // Tracks the last smoothing step used to update model_score_history.
    // In model/logging mode this is a virtual step id.
    uint64_t last_model_score_step;
    float arms_score;
    enum prediction_type model_selection;

    // Virtual windows can accumulate up to the configured VIRTUAL_STEP_SAMPLES
    // accesses; use 32-bit counters so hot pages do not wrap within a step.
    uint32_t virtual_accesses[NPBUFTYPES];
    uint32_t virtual_missed_accesses;
    uint64_t virtual_step;
    uint32_t virtual_age;
    uint32_t virtual_reads;
    uint32_t virtual_writes;
    uint32_t virtual_count;
    uint32_t virtual_missed_count;
    float virtual_missed_ewma100;
    double virtual_cumsum_reads;
    double virtual_cumsum_writes;
    double virtual_global_avg_accesses;
    double virtual_global_avg_accesses_perc;
    float virtual_w[WINDOW_SIZE];
    float virtual_w_r[WINDOW_SIZE];
    float virtual_w_w[WINDOW_SIZE];
    float virtual_w_perc_second_moment[WINDOW_SIZE];
    float virtual_w_perc_var[WINDOW_SIZE];
    float virtual_w_perc[WINDOW_SIZE];
    float virtual_w_r_perc[WINDOW_SIZE];
    float virtual_w_w_perc[WINDOW_SIZE];
    float virtual_gap4;
    float virtual_read_write_gap3;
    float virtual_groups_perc[15];
    float virtual_group_ewma5_perc[15];
    float virtual_group_ewma5_var;
    uint64_t virtual_age_count_total;

    int64_t global_count_since_top1_percent_ewma5;
    int64_t global_count_since_top50_percent_ewma5;
    uint32_t rank;
    float rank_perc;
    float rank_perc_ewma[WINDOW_SIZE];

    uint64_t global_count_similar;
    int32_t diff;

    int promote_backoff;

    int num_demotions;
    int num_promotions;

    struct data_row *last_logged_row;

    page_info() : in_dram(false), can_promote(true), last_seen_scan(0)
    {
        this->reset_page_access_fields();
    }

    void reset_page_access_fields();
    float calculate_reads(volatile uint8_t prev_access_version);
    float calculate_writes(volatile uint8_t prev_access_version);
    float calculate_accesses(volatile uint8_t prev_access_version);
    void update_window(volatile uint8_t prev_access_version, const enum sampling_modes sampling_mode);
    void update_virtual_window(size_t count_total, uint64_t step_id);
    void update_derivative_features(size_t rank, size_t num_sorted_pages, size_t count_total);
    void update_can_promote(size_t num_dram_pages, size_t rank);
    float compute_score(const float *bias);
    void reset_model_score_history();
    void push_model_score(float score);
    float max_model_score_history() const;
    float min_model_score_history() const;
    float average_model_score_history() const;
    float adjusted_moving_average_model_score() const;
};

typedef std::shared_ptr<page_info> page_ptr;

// Score entry for sorting
struct score_entry
{
    page_ptr page;
    float score;
};
