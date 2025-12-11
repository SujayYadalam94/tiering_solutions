/*
 * ARMS Kernel-based Memory Tiering System - Header
 */

#ifndef ARMS_KERNEL_H
#define ARMS_KERNEL_H

#include "defs.h"

enum syscall_type
{
    READ_SYSCALL = 0,
    WRITE_SYSCALL = 1,
    MALLOC_SYSCALL = 2,
    NUM_SYSCALL_TYPES
};

struct syscall_event
{
    enum syscall_type type;
    void *addr;
    size_t len;
};

// PEBS sample structure
struct perf_sample
{
    struct perf_event_header header;
    __u64 ip;
    __u32 pid, tid;
    __u64 addr; // Virtual address
};

void arms_start_tiering();
void arms_kernel_shutdown();
void arms_kernel_print_stats();

void pebs_log_read(void *addr, size_t len);
void pebs_log_write(void *addr, size_t len);
void pebs_log_malloc(void *addr, size_t len);
#endif /* ARMS_KERNEL_H */
