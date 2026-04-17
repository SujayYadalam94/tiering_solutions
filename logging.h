#pragma once

#include <cstring>
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "defs.h"
#include "groups.h"
#include "page.h"

struct data_row
{
    size_t step;
    size_t page;
    size_t read;
    size_t write;
    size_t count;
    float global_avg_accesses;
    float global_avg_accesses_model;
    float ewma_2;
    float ewma_2_r;
    float ewma_2_w;
    float ewma_5;
    float ewma_5_r;
    float ewma_5_w;
    float ewma_20;
    float ewma_20_r;
    float ewma_20_w;
    float ewma_100;
    float ewma_100_r;
    float ewma_100_w;
    float virtual_missed_ewma_100;
    float virtual_missed_accesses;
    float gap4;
    float read_write_gap3;
    float ewma_var_2;
    float ewma_var_5;
    float ewma_var_20;
    float ewma_var_100;

#if FULL_LOGS
    float global_count_since_top1_percent_ewma5;
    float global_count_since_top50_percent_ewma5;
#endif
    float groups[15];
    float group_ewma5[15];
    float group_ewma5_var;
    uint64_t age_count_total;

#if FULL_LOGS
    size_t prot;
    size_t flags;
    float ewma_2_abs;
    float ewma_2_r_abs;
    float ewma_2_w_abs;
    float ewma_5_abs;
    float ewma_5_r_abs;
    float ewma_5_w_abs;
    float ewma_20_abs;
    float ewma_20_r_abs;
    float ewma_20_w_abs;
    float ewma_100_abs;
    float ewma_100_r_abs;
    float ewma_100_w_abs;
    size_t rank;
    float rank_perc;
    float rank_ewma_2;
    float rank_ewma_5;
    float rank_ewma_20;
    float rank_ewma_100;
    size_t count_total;
    size_t global_count_similar;
    float diff;
    float cpu_usage;
    long long disk_read_bytes;
    long long disk_write_bytes;
    long long syscr;
    long long syscw;
    float groups_abs[15];
    float group_ewma5_abs[15];
    size_t model_selection;
    uint32_t read_bytes;
    uint32_t write_bytes;
#endif

    float model_score;
    float arms_score;
    float score;
    bool in_dram;
    uint32_t age;
    int num_demotions;
    int num_promotions;

    // Reward Componants computed at the end
    struct data_row *prev;
    float discounted_reward_90;
    float discounted_reward_95;
    float discounted_reward_99;
};

static inline void zero_cold_start_virtual_row_fields(struct data_row &row)
{
    row.read = 0;
    row.write = 0;
    row.count = 0;

    row.global_avg_accesses = 0.0f;
    row.global_avg_accesses_model = 0.0f;

    row.ewma_2 = 0.0f;
    row.ewma_2_r = 0.0f;
    row.ewma_2_w = 0.0f;
    row.ewma_5 = 0.0f;
    row.ewma_5_r = 0.0f;
    row.ewma_5_w = 0.0f;
    row.ewma_20 = 0.0f;
    row.ewma_20_r = 0.0f;
    row.ewma_20_w = 0.0f;
    row.ewma_100 = 0.0f;
    row.ewma_100_r = 0.0f;
    row.ewma_100_w = 0.0f;

    row.virtual_missed_ewma_100 = 0.0f;
    row.gap4 = 0.0f;
    row.read_write_gap3 = 0.0f;
    row.ewma_var_2 = 0.0f;
    row.ewma_var_5 = 0.0f;
    row.ewma_var_20 = 0.0f;
    row.ewma_var_100 = 0.0f;

    row.age_count_total = 0;
    row.age = 0;
}

static inline void fill_virtual_neighbor_group_features(struct group_tracker *tracker, const page_ptr &page,
                                                        struct data_row &row)
{
    if (tracker == nullptr)
    {
        for (size_t i = 0; i < 15; ++i)
        {
            row.groups[i] = 0.0f;
            row.group_ewma5[i] = 0.0f;
        }
        row.group_ewma5_var = 0.0f;
        return;
    }

    float group_avg[15] = {0.0f};
    float group_avg_ewma5[15] = {0.0f};
    float group_avg_perc[15] = {0.0f};
    float group_avg_perc_ewma5[15] = {0.0f};
    get_group_window_values(tracker, page->va, group_avg, group_avg_ewma5, group_avg_perc, group_avg_perc_ewma5);

    float sum = 0.0f;
    float sum_sq = 0.0f;
    for (size_t i = 0; i < 15; ++i)
    {
        row.groups[i] = group_avg[i];
        row.group_ewma5[i] = group_avg_ewma5[i];
        sum += row.group_ewma5[i];
        sum_sq += row.group_ewma5[i] * row.group_ewma5[i];
    }

    const float mean = sum / 15.0f;
    const float var = (sum_sq / 15.0f) - (mean * mean);
    row.group_ewma5_var = var > 0.0f ? var : 0.0f;
}

struct cpu_stat
{
    long unsigned int utime_ticks;
    long int cutime_ticks;
    long unsigned int stime_ticks;
    long int cstime_ticks;
    long unsigned int vsize; // virtual memory size in bytes
    long unsigned int rss;   // Resident  Set  Size in bytes
};

struct disk_stat
{
    long long rchar;
    long long wchar;
    long long syscr;
    long long syscw;
    long long read_bytes;
    long long write_bytes;
};

class access_log
{
  protected:
    struct cpu_stat prev_cpu_stat;
    struct cpu_stat curr_cpu_stat;
    struct disk_stat prev_disk_stat;
    struct disk_stat curr_disk_stat;
    int64_t logged_samples;
    struct data_row *scores_log;

    int get_disk_usage(const pid_t pid);
    int get_cpu_usage(const pid_t pid);
    void print_row(std::ostream &os, struct data_row *row, bool header);
    void finalize_log();

  public:
    float get_gb_allocated();
    access_log();
    void update_proc_stats();
    float calc_cpu_usage_pct();
    void pebs_write_log();
    struct data_row extract_row(size_t step, const page_ptr &page, struct group_tracker *grp_tracker,
                                size_t count_all_pages);
    void log_row(const page_ptr &page, struct data_row &row);
    bool overlaps_with_logging_region(uint64_t addr, uint64_t length) const;
};

extern struct access_log *access_log;