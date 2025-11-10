#ifndef ARMS_MMGR_H
#define ARMS_MMGR_H

#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

#include "arms.h"
#include "paging.h"

#define ARMS_INTERVAL 10000ULL // in us

#define ARMS_FASTFREE    (dramsize / 10)
#define ARMS_COOL_RATE   (10ULL * 1024ULL * 1024ULL * 1024ULL)
#define ARMS_THAW_RATE   (nvmsize + dramsize)

#define FASTMEM_HUGE_PAGES  ((dramsize) / (HUGEPAGE_SIZE))
#define FASTMEM_BASE_PAGES  ((dramsize) / (BASEPAGE_SIZE))

#define SLOWMEM_HUGE_PAGES  ((nvmsize) / (HUGEPAGE_SIZE))
#define SLOWMEM_BASE_PAGES  ((nvmsize) / (BASEPAGE_SIZE))

struct mmgr_node {
  struct arms_page *page;
  uint64_t accesses, tot_accesses;
  uint64_t offset;
  struct mmgr_node *next, *prev;
  struct mmgr_list *list;
};

struct mmgr_list {
  struct mmgr_node *first;
  struct mmgr_node *last;
  size_t numentries;
  pthread_mutex_t list_lock;
};

void *mmgr_kswapd(void);
struct arms_page* arms_mmgr_pagefault();
struct arms_page* arms_mmgr_pagefault_unlocked();
void arms_mmgr_init(void);
void arms_mmgr_remove_page(struct arms_page *page);
void arms_mmgr_stats();

#endif
