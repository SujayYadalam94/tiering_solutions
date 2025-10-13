#ifndef HEMEM_H
#define HEMEM_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic< X >
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ALLOC_LRU
#include "policies/lru.h"
#endif

#ifdef ALLOC_SIMPLE
#include "policies/simple.h"
#endif

// Define target system here
// Options are SCAILP and C220G5
#define C220G5


#include "fifo.h"
#include "pebs.h"
#include "timer.h"
#include "interpose.h"

#ifdef C220G5
#define FAULT_THREAD_CPU  (10)
#define STATS_THREAD_CPU  (13)
#elif defined SCAILP
#define FAULT_THREAD_CPU  (0)
#define STATS_THREAD_CPU  (3)
#else
// Compile error - unknown system
#error "Unknown system - valid options are SCAILP and C220G5"
#endif


//#define HEMEM_DEBUG
#define STATS_THREAD

#define USE_DMA
#define NUM_CHANNS 2
#define SIZE_PER_DMA_REQUEST (1024*1024)

#define MEM_BARRIER() __sync_synchronize()

extern uint64_t min_interpose_mem_size;

extern uint64_t nvmsize;
extern uint64_t dramsize;
extern char* drampath;
extern char* nvmpath;

#define NVMSIZE_DEFAULT   (64L * (1024L * 1024L * 1024L))
#define DRAMSIZE_DEFAULT  (32L * (1024L * 1024L * 1024L))

#define DRAMPATH_DEFAULT  "/dev/dax0.0"
#define NVMPATH_DEFAULT   "/dev/dax1.0"

#define BASEPAGE_SIZE	  (4UL * 1024UL)
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define GIGAPAGE_SIZE   (1024UL * 1024UL * 1024UL)
#define PAGE_SIZE 	    HUGEPAGE_SIZE
#define CACHELINE_SIZE   (64)

#define MAX_NVME_PAGES  (NVMSIZE_DEFAULT / PAGE_SIZE)
#define MAX_DRAM_PAGES  (DRAMSIZE_DEFAULT / PAGE_SIZE)

#define BASEPAGE_MASK	(BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK   (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK	(BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK   (GIGAPAGE_MASK ^ UINT64_MAX)

extern FILE *hememlogf;
//#define LOG(...) fprintf(stderr, __VA_ARGS__)
//#define LOG(...)	fprintf(hememlogf, __VA_ARGS__)
#define LOG(str, ...) while(0) {}

extern FILE *timef;
extern bool timing;

static inline void log_time(const char* fmt, ...)
{
  if (timing) {
    va_list args;
    va_start(args, fmt);
    vfprintf(timef, fmt, args);
    va_end(args);
  }
}


//#define LOG_TIME(str, ...) log_time(str, __VA_ARGS__)
//#define LOG_TIME(str, ...) fprintf(timef, str, __VA_ARGS__)
#define LOG_TIME(str, ...) while(0) {}

extern FILE *statsf;
#define LOG_STATS(str, ...) fprintf(stderr, str,  __VA_ARGS__)
//#define LOG_STATS(str, ...) fprintf(statsf, str, __VA_ARGS__)
//#define LOG_STATS(str, ...) while (0) {}

#if !defined(ALLOC_RUNTIME)
#if defined (ALLOC_HEMEM)
  #define pagefault(...) pebs_pagefault(__VA_ARGS__)
  #define paging_init(...) pebs_init(__VA_ARGS__)
  #define mmgr_add(...) pebs_add_page(__VA_ARGS__)
  #define mmgr_find(...) pebs_find_page(__VA_ARGS__)
  #define mmgr_remove(...) pebs_remove_page(__VA_ARGS__)
  #define mmgr_stats(...) pebs_stats(__VA_ARGS__)
  #define policy_shutdown(...) pebs_shutdown(__VA_ARGS__)
#elif defined (ALLOC_LRU)
  #define pagefault(...) lru_pagefault(__VA_ARGS__)
  #define paging_init(...) lru_init(__VA_ARGS__)
  #define mmgr_remove(...) lru_remove_page(__VA_ARGS__)
  #define mmgr_stats(...) lru_stats(__VA_ARGS__)
  #define policy_shutdown(...) while(0) {}
#elif defined (ALLOC_SIMPLE)
  #define pagefault(...) simple_pagefault(__VA_ARGS__)
  #define paging_init(...) simple_init(__VA_ARGS__)
  #define mmgr_remove(...) simple_remove_page(__VA_ARGS__)
  #define mmgr_stats(...) simple_stats(__VA_ARGS__)
  #define policy_shutdown(...) while(0) {}
#endif
#endif


#define MAX_UFFD_MSGS	    (1)
#define MAX_COPY_THREADS  (4)

extern int devmemfd;
extern uint64_t cr3;
extern int dramfd;
extern int nvmfd;
extern bool is_init;
extern uint64_t dram_small_allocation_bytes;
extern uint64_t brk_intercepted_bytes;
extern uint64_t brk_intercepted_count;
extern uint64_t missing_faults_handled;
extern uint64_t migrations_up;
extern uint64_t migrations_down;
extern __thread bool internal_malloc;
extern __thread bool old_internal_call;
extern __thread bool internal_call;
extern __thread bool internal_munmap;

enum memtypes {
  FASTMEM = 0,
  SLOWMEM = 1,
  NMEMTYPES,
};

enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};

#ifdef ALLOC_RUNTIME
struct hemem_region;
#endif

struct hemem_page {
  uint64_t va;
  uint64_t devdax_offset;
  bool in_dram;
  enum pagetypes pt;
  volatile bool migrating;
  bool present;
  uint16_t accesses[NPBUFTYPES][2];
  pthread_mutex_t page_lock;

  // Our system
#ifdef SPATIAL_SMOOTHING
  float s_accesses[NPBUFTYPES];
#endif

  float w[WINDOW_SIZE];
  float score;
  float prev_score;
  uint16_t hot_age;
  bool can_promote;
  // LRU policy fields
  bool written;
  uint32_t naccesses;

  struct hemem_page *next, *prev;
  struct fifo_list *list;
#ifdef ALLOC_RUNTIME
  struct hemem_region *region;
#endif
};
#ifdef ALLOC_RUNTIME
static_assert(sizeof(struct hemem_page) >= 128, "hemem_page size expected to be at least 128 bytes");
#else
static_assert(sizeof(struct hemem_page) == 128);
#endif

struct migration_req {
  struct hemem_page *dram_page;
  struct hemem_page *nvm_page;
  struct hemem_page *free_page;
  bool need_demotion;

  struct migration_req *next, *prev;
  struct migration_req_list *list;
};

static inline uint64_t pt_to_pagesize(enum pagetypes pt)
{
  switch(pt) {
  case HUGEP: return HUGEPAGE_SIZE;
  case BASEP: return BASEPAGE_SIZE;
  default: assert(!"Unknown page type");
  }
}

static inline enum pagetypes pagesize_to_pt(uint64_t pagesize)
{
  switch (pagesize) {
    case BASEPAGE_SIZE: return BASEP;
    case HUGEPAGE_SIZE: return HUGEP;
    default: assert(!"Unknown page ssize");
  }
}

void hemem_init();
void hemem_stop();
void* hemem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int hemem_munmap(void* addr, size_t length);
void *handle_fault();
void hemem_migrate_up(struct hemem_page *page, uint64_t dram_offset);
void hemem_migrate_down(struct hemem_page *page, uint64_t nvm_offset);
void hemem_wp_page(struct hemem_page *page, bool protect);
void hemem_promote_pages(uint64_t addr);
void hemem_demote_pages(uint64_t addr);

#if defined(ALLOC_LRU) || defined(ALLOC_RUNTIME)
void hemem_clear_bits(struct hemem_page *page);
uint64_t hemem_get_bits(struct hemem_page *page);
void hemem_tlb_shootdown(uint64_t va);
#endif

// Commented out because -- identical to find_page(uint64_t va)
//struct hemem_page* get_hemem_page(uint64_t va);

void hemem_print_stats();
void hemem_clear_stats();

void hemem_start_timing(void);
void hemem_stop_timing(void);

#ifdef ALLOC_RUNTIME
enum hemem_policy_kind {
  HEMEM_POLICY_PEBs = 0,
  HEMEM_POLICY_LRU,
  HEMEM_POLICY_SIMPLE,
  HEMEM_POLICY_COUNT
};

struct hemem_policy_ops {
  enum hemem_policy_kind kind;
  const char *name;
  void (*init)(struct fifo_list *dram_free_list, struct fifo_list *nvm_free_list);
  void (*shutdown)(void);
  struct hemem_page* (*pagefault)(uint64_t va);
  void (*page_add)(struct hemem_page *page);
  void (*page_remove)(struct hemem_page *page);
  void (*stats)(void);
};

// Per-policy resource allocation
struct hemem_policy_resources {
  enum hemem_policy_kind kind;
  uint64_t dram_size;  // Total DRAM allocated to this policy
  uint64_t nvm_size;   // Total NVM allocated to this policy
  struct fifo_list dram_free_list;  // Free DRAM pages for this policy
  struct fifo_list nvm_free_list;   // Free NVM pages for this policy
  uint64_t dram_offset_start;  // Physical offset in DAX device
  uint64_t nvm_offset_start;   // Physical offset in DAX device
  // Per-policy migration statistics
  uint64_t migrations_up;      // Promotions to DRAM
  uint64_t migrations_down;    // Demotions to NVM
  uint64_t bytes_migrated;     // Total bytes migrated by this policy
};

// Region = VA range + policy type (metadata only, no physical resources)
struct hemem_region {
  uint64_t start;  // VA range start
  uint64_t end;    // VA range end
  enum hemem_policy_kind policy_kind;  // Which policy manages this VA range
  char label[32];
  bool is_fallback;  // True if this is the catchall fallback region
};

void hemem_regions_bootstrap(void);
void hemem_regions_reset(void);  // For testing: reset region state  
struct hemem_region* hemem_region_lookup(uint64_t va);
int hemem_region_register(uint64_t start, uint64_t end, enum hemem_policy_kind kind, const char *label);
struct hemem_page* hemem_policy_pagefault(struct hemem_region *region, uint64_t va);
void hemem_policy_register_page(struct hemem_page *page);
void hemem_policy_unregister_page(struct hemem_page *page);
void hemem_policies_collect_stats(void);
void hemem_policy_record_migration(enum hemem_policy_kind kind, bool to_dram, uint64_t bytes);
struct hemem_policy_resources* hemem_get_policy_resources(enum hemem_policy_kind kind);  // For testing
void hemem_policies_shutdown(void);
struct hemem_page* hemem_page_lookup(uint64_t va);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HEMEM_H */
