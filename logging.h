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
    float global_avg_accesses_perc;
    float ewma_2_perc;
    float ewma_2_r_perc;
    float ewma_2_w_perc;
    float ewma_5_perc;
    float ewma_5_r_perc;
    float ewma_5_w_perc;
    float ewma_20_perc;
    float ewma_20_r_perc;
    float ewma_20_w_perc;
    float ewma_100_perc;
    float ewma_100_r_perc;
    float ewma_100_w_perc;
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
    float groups_perc[15];
    float group_ewma5_perc[15];
    float group_ewma5_var;
    uint64_t age_count_total;

#if FULL_LOGS
    size_t prot;
    size_t flags;
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
    float groups[15];
    float group_ewma5[15];
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