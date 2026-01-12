#pragma once

#include <cstring>
#include <fstream>
#include <iosfwd>
#include <iostream>
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
    double ewma_2_perc;
    double ewma_5_perc;
    double ewma_20_perc;
    double ewma_100_perc;
    double ewma_100_malloc_size;
    double ewma_100_malloc_calls;
    double global_count_since_top1_percent_ewma5;
    double global_count_since_top50_percent_ewma5;
    double groups_perc[15];
    double group_ewma5_perc[15];
    uint64_t age_count_total;

#if FULL_LOGS
    size_t malloc_size;
    size_t prot;
    size_t flags;
    double ewma_2;
    double ewma_2_r;
    double ewma_2_w;
    double ewma_2_r_perc;
    double ewma_2_w_perc;
    double ewma_2_malloc_size;
    double ewma_2_malloc_calls;
    double ewma_5;
    double ewma_5_r;
    double ewma_5_w;
    double ewma_5_r_perc;
    double ewma_5_w_perc;
    double ewma_5_malloc_size;
    double ewma_5_malloc_calls;
    double ewma_20;
    double ewma_20_r;
    double ewma_20_w;
    double ewma_20_r_perc;
    double ewma_20_w_perc;
    double ewma_20_malloc_size;
    double ewma_20_malloc_calls;
    double ewma_100;
    double ewma_100_r;
    double ewma_100_w;
    double ewma_100_r_perc;
    double ewma_100_w_perc;
    size_t rank;
    double rank_perc;
    double rank_ewma_2;
    double rank_ewma_5;
    double rank_ewma_20;
    double rank_ewma_100;
    size_t count_total;
    size_t global_count_similar;
    double diff;
    double cpu_usage;
    long long disk_read_bytes;
    long long disk_write_bytes;
    long long syscr;
    long long syscw;
    double groups[15];
    double group_ewma5[15];
    size_t model_selection;
    uint32_t read_syscalls;
    uint32_t write_syscalls;
    uint32_t read_bytes;
    uint32_t write_bytes;
    uint32_t sum_malloc_bytes;
    int32_t min_malloc_bytes;
    int32_t max_malloc_bytes;
    uint32_t malloc_call;
#endif

    double model_score;
    double arms_score;
    int32_t pages_in_dram;
    int32_t seen_pages;
    uint32_t age;

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
    size_t logged_samples;
    struct data_row *scores_log;

    int get_disk_usage(const pid_t pid);
    int get_cpu_usage(const pid_t pid);
    void print_row(std::ostream &os, struct data_row *row, bool header);
    void finalize_log();

  public:
    float get_gb_allocated();
    access_log();
    void update_proc_stats();
    double calc_cpu_usage_pct();
    void pebs_write_log();
    void log_row(size_t step, struct page_info *page, struct group_tracker *grp_tracker, size_t count_all_pages);
};

extern struct access_log *access_log;