#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <unordered_map>
#include <vector>

#include "arms_kernel.h"
#include "defs.h"
#include "groups.h"
#include "page.h"

struct perf_event_mmap_page;

// Shared global state accessed across thread translation units.
extern uint64_t dramsize;
extern std::atomic<size_t> syscall_queue_index;
extern std::atomic<size_t> syscall_queue_size;
extern std::atomic<struct syscall_event *> syscall_event_queue;
extern std::atomic<uint64_t> scan_generation;

extern std::unordered_map<uint64_t, std::shared_ptr<page_info>> pages_map;
extern std::mutex pages_map_lock;

extern int pagemap_fd;
extern pid_t target_pid;

extern int perf_fd[PEBS_NPROCS][NPBUFTYPES];
extern struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];

extern pthread_t scan_thread;
extern pthread_t pagemap_scan_thread;
extern pthread_t policy_thread;
extern pthread_t migration_threads[MIGRATION_WORKER_COUNT];
extern pthread_t madvise_thread;

extern int32_t policy_thread_interval;

extern volatile uint64_t global_version;
extern volatile uint8_t curr_access_version;
extern volatile uint8_t prev_access_version;
extern volatile uint8_t curr_window_index;

extern const float *active_bias;
extern enum sampling_modes sampling_mode;

extern std::atomic<uint64_t> migrations_up;
extern std::atomic<uint64_t> migrations_down;
extern std::atomic<uint64_t> migrations_up_period;
extern std::atomic<uint64_t> migrations_down_period;
extern uint64_t total_samples[NPBUFTYPES];
extern std::atomic<uint64_t> max_dram_base_pages_seen;
extern std::atomic<uint64_t> total_dram_base_pages_accum;
extern std::atomic<uint64_t> dram_samples;

extern float dram_bw_ewma;
extern float nvm_bw_ewma;
extern float nvm_bw_std;
extern float cusum;
extern uint32_t time_since_recn;

extern float promotion_cost_avg;
extern float demotion_cost_avg;
extern float latency_diff;

extern bool terminated;
extern std::atomic<bool> madvise_thread_running;
extern struct group_tracker *grp_tracker;

extern std::condition_variable madvise_cv;
extern std::condition_variable migration_cv;

bool is_access_log_page(uint64_t page_base);
void populate_new_page(uint64_t page_base);

void enqueue_migration_task(const std::vector<uint64_t> &promote_vas, const std::vector<uint64_t> &demote_vas);
void clear_migration_queue();

void change_sampling_frequency();
void detect_hot_change();
void update_scores_and_migrate(size_t timestep);

void *madvise_worker_thread(void *arg);
void *pebs_scan_thread(void *arg);
void *pagemap_scan_thread_fn(void *arg);
void *migration_worker(void *arg);
void *arms_policy_thread(void *arg);
