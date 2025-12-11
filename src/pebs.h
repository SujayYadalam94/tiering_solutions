#pragma once

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem.h"

#include "defs.h"

struct perf_sample {
  struct perf_event_header header;
  __u64	ip;
  __u32 pid, tid;    /* if PERF_SAMPLE_TID */
  __u64 addr;        /* if PERF_SAMPLE_ADDR */
  // __u64 weight;      /* if PERF_SAMPLE_WEIGHT */
  /* __u64 data_src;    /\* if PERF_SAMPLE_DATA_SRC *\/ */
};


enum syscall_type {
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

void *
pebs_kswapd();
struct hemem_page* pebs_pagefault(void);
struct hemem_page* pebs_pagefault_unlocked(void);
void pebs_init(void);

void pebs_add_page(struct hemem_page *page);
struct hemem_page* pebs_find_page(uint64_t va);
struct hemem_page* pebs_remove_page(uint64_t va);

void pebs_log_read(void *addr, size_t len);
void pebs_log_write(void *addr, size_t len);
void pebs_log_malloc(void *addr, size_t len);

void pebs_stats();
void pebs_shutdown();

extern bool terminated;