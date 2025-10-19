#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "hemem_page.h"
#include "groups.h"

#define PRINT_TRAINING_DATA (false)
#define MAX_LOGGED_SAMPLES (50000000)

struct data_row{
    size_t step;
    size_t page;
    size_t read;
    size_t write;
    size_t count;
    size_t malloc_size;
    size_t prot;
    size_t flags;
    double ewma_2;
    double ewma_2_r;
    double ewma_2_w;
    double ewma_2_malloc_size;
    double ewma_2_malloc_calls;
    double ewma_5;
    double ewma_5_r;
    double ewma_5_w;
    double ewma_5_malloc_size;
    double ewma_5_malloc_calls;
    double ewma_20;
    double ewma_20_r;
    double ewma_20_w;
    double ewma_20_malloc_size;
    double ewma_20_malloc_calls;
    double ewma_100;
    double ewma_100_r;
    double ewma_100_w;
    double ewma_100_malloc_size;
    double ewma_100_malloc_calls;
    double global_count_since_top1_percent_ewma5;
    double global_count_since_top50_percent_ewma5;
    size_t rank;
    size_t count_total;
    size_t global_count_similar;
    double diff;
    double cpu_usage;
    long long disk_read_bytes;
    long long disk_write_bytes;
    long long syscr;
    long long syscw;
    double groups[7];
    size_t model_selection;
    double model_score;
    double arms_score;
    bool in_dram;
    uint32_t read_syscalls;
    uint32_t write_syscalls;
    uint32_t read_bytes;
    uint32_t write_bytes;
    uint32_t sum_malloc_bytes;
    int32_t min_malloc_bytes;
    int32_t max_malloc_bytes;
    uint32_t malloc_call;
    uint32_t age;
};

struct cpu_stat {
    long unsigned int utime_ticks;
    long int cutime_ticks;
    long unsigned int stime_ticks;
    long int cstime_ticks;
    long unsigned int vsize; // virtual memory size in bytes
    long unsigned int rss; //Resident  Set  Size in bytes
};

struct disk_stat {
    long long rchar;
    long long wchar;
    long long syscr;
    long long syscw;
    long long read_bytes;
    long long write_bytes;
};

enum migration_type {
  TO_DRAM = 0,
  TO_NVM = 1,
};
struct migration_event
{
    double time;
    uint64_t va_dram;
    uint64_t va_nvm;
    size_t write_dram;
    size_t read_dram;
    size_t write_nvm;
    size_t read_nvm;
    size_t timestep;
    enum migration_type type;
};

/* Global process statistics - declared here, defined in logging.c to avoid
    multiple-definition linker errors when this header is included by many
    translation units. Keep only declarations in headers; provide one
    definition in a single .c file (logging.c). */
extern struct cpu_stat prev_cpu_stat;
extern struct cpu_stat curr_cpu_stat;
extern struct disk_stat prev_disk_stat;
extern struct disk_stat curr_disk_stat;

int get_disk_usage(const pid_t pid);
int get_cpu_usage(const pid_t pid);
double calc_cpu_usage_pct();
void update_proc_stats();

/* Logging buffers/counters - declare as extern here and define once in
    logging.c. */
extern size_t logged_samples;
extern struct data_row *scores_log;

#define MIGRATION_EVENT_QUEUE_CAPACITY (256 * 1024)
extern _Atomic size_t migration_queue_index;
extern _Atomic size_t migration_queue_size;
extern _Atomic(struct migration_event *) migration_event_queue;

void print_row(FILE *f, struct data_row *row, bool header);
void pebs_write_log();
void log_row(size_t step, struct hemem_page *page, struct group_tracker *grp_tracker, size_t count_all_pages);