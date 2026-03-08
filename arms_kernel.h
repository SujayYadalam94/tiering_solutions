/*
 * ARMS Kernel-based Memory Tiering System - Header
 */

#ifndef ARMS_KERNEL_H
#define ARMS_KERNEL_H

#include "defs.h"

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
#endif /* ARMS_KERNEL_H */
