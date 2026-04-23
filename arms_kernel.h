/*
 * ARMS Kernel-based Memory Tiering System - Header
 */

#ifndef ARMS_KERNEL_H
#define ARMS_KERNEL_H

#include "defs.h"
#include <stddef.h>

struct ip_range
{
    uint64_t start;
    uint64_t end;
};

// PEBS sample structure
struct perf_sample
{
    struct perf_event_header header;
    __u64 ip;
    __u32 pid, tid;
    __u64 addr; // Virtual address
};

struct perf_sample_lost
{
    struct perf_event_header header;
    __u64 id;
    __u64 lost;
};

void arms_start_tiering();
void arms_kernel_shutdown();
void arms_kernel_print_stats();
uint64_t get_virtual_step_samples();
void set_application_thread_near_memory_default();
void set_application_thread_near_memory_preferred();
void set_application_thread_far_memory_default();

void set_preload_ip_ranges(const char *library_path, const struct ip_range *ranges, size_t range_count);
bool is_preload_library_ip(uint64_t ip);
void note_preload_library_sample_filtered();
uint64_t take_preload_library_filtered_samples();
void set_helper_library_ip_ranges(const struct ip_range *ranges, size_t range_count);
bool is_helper_library_ip(uint64_t ip);
void note_helper_library_sample_filtered();
uint64_t take_helper_library_filtered_samples();
void note_tiering_runtime_tid_sample_filtered();
uint64_t take_tiering_runtime_tid_filtered_samples();
void note_other_pid_sample_filtered();
uint64_t take_other_pid_filtered_samples();
#endif /* ARMS_KERNEL_H */
