#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "../hemem.h"
#include "paging.h"
#include "lru.h"
#include "../timer.h"
#include "../fifo.h"


#define MAX_VAS                1000000

static struct fifo_list active_list;
static struct fifo_list inactive_list;
static struct fifo_list nvm_active_list;
static struct fifo_list nvm_inactive_list;
static struct fifo_list written_list;
static struct fifo_list nvm_written_list;
static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;
static uint64_t vas[MAX_VAS];
static uint64_t vanum = 0;
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
static bool __thread in_kswapd = false;
uint64_t lru_runs = 0;
static volatile bool lru_shutdown_requested = false;
static pthread_t lru_scan_thread;
static pthread_t lru_kswapd_thread;
static bool lru_scan_thread_active = false;
static bool lru_kswapd_thread_active = false;
static bool lru_lists_initialized = false;

static void init_fifo_list(struct fifo_list *list);
static void cleanup_fifo_list(struct fifo_list *list);

static void lru_migrate_down(struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  page->migrating = true;
  hemem_wp_page(page, true);
  hemem_migrate_down(page, offset);
  page->migrating = false; 

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_down: %f s\n", elapsed(&start, &end));
}

static void lru_migrate_up(struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  page->migrating = true;
  hemem_wp_page(page, true);
  hemem_migrate_up(page, offset);
  page->migrating = false;

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_up: %f s\n", elapsed(&start, &end));
}

static void init_fifo_list(struct fifo_list *list)
{
  memset(list, 0, sizeof(*list));
  pthread_mutex_init(&(list->list_lock), NULL);
}

static void cleanup_fifo_list(struct fifo_list *list)
{
  struct hemem_page *page;
  while ((page = dequeue_fifo(list)) != NULL) {
    pthread_mutex_destroy(&(page->page_lock));
    free(page);
  }
  pthread_mutex_destroy(&(list->list_lock));
  memset(list, 0, sizeof(*list));
}


static void shrink_caches(struct fifo_list *active, struct fifo_list *inactive, struct fifo_list *written)
{
  size_t nr_pages = active->numentries;
  uint64_t bits;
  // find cold pages and move to inactive list
  while (nr_pages > 0 && active->numentries > 0) {
    struct hemem_page *page = dequeue_fifo(active);
    bits = hemem_get_bits(page);
    if ((bits & HEMEM_ACCESSED_FLAG) == HEMEM_ACCESSED_FLAG) {
      if ((bits & HEMEM_DIRTY_FLAG) == HEMEM_DIRTY_FLAG) {
        // page was written, so put it in the highest priority
        // written list for memory type; if in DRAM, will
        // remain in DRAM; if in NVM, has highest priority
        // for migration to DRAM
        page->written = true;
        //hemem_clear_bits(page);
        assert(vanum < MAX_VAS);
        vas[vanum++] = page->va;
        enqueue_fifo(written, page);
      }
      else {
        // page was not written but was already in active list, so
        // keep it in active list since it was accessed
        page->written = false;
        //hemem_clear_bits(page);
        assert(vanum < MAX_VAS);
        vas[vanum++] = page->va;
        enqueue_fifo(active, page);
      }
    }
    else {
      // found a cold page, put it on inactive list
      page->naccesses = 0;
      enqueue_fifo(inactive, page);
    }
    nr_pages--;
  }
}


static void expand_caches(struct fifo_list *active, struct fifo_list *inactive, struct fifo_list *written)
{
  size_t nr_pages = inactive->numentries;
  size_t i;
  struct hemem_page *page;
  uint64_t bits;

  // examine each page in inactive list and move to active list if accessed
  for (i = 0; i < nr_pages; i++) {
    page = dequeue_fifo(inactive);

    if (page == NULL) {
      break;
    }

    bits = hemem_get_bits(page);
    if ((bits & HEMEM_ACCESSED_FLAG) == HEMEM_ACCESSED_FLAG) {
      if ((bits & HEMEM_DIRTY_FLAG) == HEMEM_DIRTY_FLAG) {
        // page was written, so put it in the highest priority
        // written list for memory type; if in DRAM, will
        // remain in DRAM; if in NVM, has highest priority
        // for migration to DRAM
        page->written = true;
        enqueue_fifo(written, page);
      }
      else {
        // page was not written and used to be inactive
        // non write-intensive pages must be accessed twice
        // in a row to be marked active
        page->written = false;
        if (page->naccesses >= 4) {
          page->naccesses = 0;
          enqueue_fifo(active, page);
        }
        else {
          page->naccesses++;
          //hemem_clear_bits(page);
          assert(vanum < MAX_VAS);
          vas[vanum++] = page->va;
          enqueue_fifo(inactive, page);
        }
      }
    }
    else {
      page->naccesses = 0;
      enqueue_fifo(inactive, page);
    }
  }
}

static void check_writes(struct fifo_list *active, struct fifo_list *inactive, struct fifo_list *written)
{
  size_t nr_pages = written->numentries;
  size_t i;
  struct hemem_page *page;
  uint64_t bits;

  for (i = 0; i < nr_pages; i++) {
    page = dequeue_fifo(written);
    
    if (page == NULL) {
      break;
    }

    bits = hemem_get_bits(page);
    if ((bits & HEMEM_ACCESSED_FLAG) == HEMEM_ACCESSED_FLAG) {
      if ((bits & HEMEM_DIRTY_FLAG) == HEMEM_DIRTY_FLAG) {
        // page was written in the recent past and continues to be written
        // keep in written list for high priority migration to DRAM/high
        // priority for remaining in DRAM
        page->written = true;
        //hemem_clear_bits(page);
        assert(vanum < MAX_VAS);
        vas[vanum++] = page->va;
        enqueue_fifo(written, page);
      }
      else {
        // page was written in the recent past, but was not written just
        // now. it still may be written in the near future, so keep it
        // in the regular active list to give it a good chance of
        // remaining in DRAM or lower priority for migration to DRAM
        page->written = false;
        enqueue_fifo(active, page);
      }
    }
    else {
      enqueue_fifo(inactive, page);
    }
  }
}

void *lru_kscand()
{
  struct timeval start, end, clear_start, clear_end;

  while (true) {
    if (lru_shutdown_requested) {
      break;
    }

    usleep(KSCAND_INTERVAL);

    if (lru_shutdown_requested) {
      break;
    }

    pthread_mutex_lock(&global_lock);
    //fprintf(stderr, "[LRU-DEBUG] kscand: starting scan\n");

    gettimeofday(&start, NULL);

    vanum = 0;

    check_writes(&active_list, &inactive_list, &written_list);
    check_writes(&nvm_active_list, &nvm_inactive_list, &nvm_written_list);

    shrink_caches(&active_list, &inactive_list, &written_list);
    shrink_caches(&nvm_active_list, &nvm_inactive_list, &nvm_written_list);

    expand_caches(&active_list, &inactive_list, &written_list);
    expand_caches(&nvm_active_list, &nvm_inactive_list, &nvm_written_list);

    gettimeofday(&clear_start, NULL);
    for(uint64_t i = 0; i < vanum; i++) {
      struct hemem_page mypage = { .va = vas[i] };
      hemem_clear_bits(&mypage);    
    }
    gettimeofday(&clear_end, NULL);
    LOG_TIME("clear_bits: %f s\n", elapsed(&clear_start, &clear_end));
    
    hemem_tlb_shootdown(0);

    gettimeofday(&end, NULL);

    LOG_TIME("scan: %f s\n", elapsed(&start, &end));
    //fprintf(stderr, "[LRU-DEBUG] kscand: completed scan, active=%lu inactive=%lu\n",
    //        active_list.numentries, inactive_list.numentries);
    pthread_mutex_unlock(&global_lock);
  }

  //fprintf(stderr, "[LRU-DEBUG] kscand: exiting\n");
  return NULL;
}

void *lru_kswapd()
{
  int tries;
  struct hemem_page *p;
  struct hemem_page *cp;
  struct hemem_page *np;
  struct timeval start, end;
  uint64_t migrated_bytes;
  bool from_written_list = true;
  uint64_t old_offset;

  //free(malloc(65536));
  
  in_kswapd = true;

  while (true) {
    if (lru_shutdown_requested) {
      break;
    }

    usleep(KSWAPD_INTERVAL);

    if (lru_shutdown_requested) {
      break;
    }

    pthread_mutex_lock(&global_lock);

    gettimeofday(&start, NULL);
    //fprintf(stderr, "[LRU-DEBUG] kswapd: starting migration, target=%lu bytes\n", KSWAPD_MIGRATE_RATE);
    
    // move each active NVM page to DRAM
    for (migrated_bytes = 0; migrated_bytes < KSWAPD_MIGRATE_RATE;) {
      p = dequeue_fifo(&nvm_written_list);
      if (p == NULL) {
        p = dequeue_fifo(&nvm_active_list);
        from_written_list = false;
        if (p == NULL) {
          break;
        }
      }
      else {
        from_written_list = true;
      }

      //pthread_mutex_lock(&(p->page_lock));

      for (tries = 0; tries < 2; tries++) {
        // find a free DRAM page
        np = dequeue_fifo(&dram_free_list);

        if (np != NULL) {
          //pthread_mutex_lock(&(np->page_lock));

          assert(!(np->present));

          LOG("%lx: cold %lu -> hot %lu\t slowmem.active: %lu, slowmem.inactive: %lu\t hotmem.active: %lu, hotmem.inactive: %lu\n",
                p->va, p->devdax_offset, np->devdax_offset, nvm_active_list.numentries, nvm_inactive_list.numentries, active_list.numentries, inactive_list.numentries);

          old_offset = p->devdax_offset;
          lru_migrate_up(p, np->devdax_offset);
          np->devdax_offset = old_offset;
          np->in_dram = false;
          np->present = false;

          enqueue_fifo(&active_list, p);
          enqueue_fifo(&nvm_free_list, np);

          migrated_bytes += pt_to_pagesize(p->pt);

          //pthread_mutex_unlock(&(np->page_lock));

          break;
        }

        // no free dram page, try to find a cold dram page to move down
        cp = dequeue_fifo(&inactive_list);
        if (cp == NULL) {
          // all dram pages are hot, so put it back in list we got it from
          if (from_written_list) {
            enqueue_fifo(&nvm_written_list, p);
          }
          else {
            enqueue_fifo(&nvm_active_list, p);
          }
          //pthread_mutex_unlock(&(p->page_lock));
          goto out;
        }
        assert(cp != NULL);

        //pthread_mutex_lock(&(cp->page_lock));

        // find a free nvm page to move the cold dram page to
        np = dequeue_fifo(&nvm_free_list);
        if (np != NULL) {
          //pthread_mutex_lock(&(np->page_lock));
          assert(!(np->present));

          LOG("%lx: hot %lu -> cold %lu\t slowmem.active: %lu, slowmem.inactive: %lu\t hotmem.active: %lu, hotmem.inactive: %lu\n",
                cp->va, cp->devdax_offset, np->devdax_offset, nvm_active_list.numentries, nvm_inactive_list.numentries, active_list.numentries, inactive_list.numentries);

          old_offset = cp->devdax_offset;
          lru_migrate_down(cp, np->devdax_offset);
          np->devdax_offset = old_offset;
          np->in_dram = true;
          np->present = false;

          enqueue_fifo(&nvm_inactive_list, cp);
          enqueue_fifo(&dram_free_list, np);

          //pthread_mutex_unlock(&(np->page_lock));
        } else {
          HEMEM_LOG("[LRU-WARNING] NVM free list exhausted during demotion\n");
          enqueue_fifo(&inactive_list, cp);
          break;
        }

        //pthread_mutex_unlock(&(cp->page_lock));
      }

      //pthread_mutex_unlock(&(p->page_lock));
    }

out:
    lru_runs++;
    pthread_mutex_unlock(&global_lock);
    gettimeofday(&end, NULL);
    LOG_TIME("migrate: %f s\n", elapsed(&start, &end));
  }

  in_kswapd = false;
  //fprintf(stderr, "[LRU-DEBUG] kswapd: exiting\n");
  return NULL;
}


/*  called with global lock held via lru_pagefault function */
static struct hemem_page* lru_allocate_page()
{
  struct timeval start, end;
  struct hemem_page *page;
#ifdef LRU_SWAP
  struct hemem_page *cp;
  int tries;
#endif

  gettimeofday(&start, NULL);
#ifdef LRU_SWAP
  for (tries = 0; tries < 2; tries++) {
#endif
    page = dequeue_fifo(&dram_free_list);
    if (page != NULL) {
      //pthread_mutex_lock(&(page->page_lock));
      assert(page->in_dram);
      assert(!page->present);

      page->present = true;
      enqueue_fifo(&active_list, page);

      gettimeofday(&end, NULL);
      LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

      //pthread_mutex_unlock(&(page->page_lock));

      return page;
    }
    
#ifndef LRU_SWAP
    // DRAM is full, fall back to NVM
    page = dequeue_fifo(&nvm_free_list);
    if (page != NULL) {
      //pthread_mutex_lock(&(page->page_lock));

      assert(!page->in_dram);
      assert(!page->present);

      page->present = true;
      enqueue_fifo(&nvm_active_list, page);

      gettimeofday(&end, NULL);
      LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

      //pthread_mutex_unlock(&(page->page_lock));

      return page;
    }
    
#else
    // DRAM was full, try to free some space by moving a cold page down
    if (inactive_list.numentries == 0){
      // force some pages down to slow memory/inactive list
      shrink_caches(&active_list, &inactive_list, &written_list);
    }

    // move a cold page from dram to nvm
    cp = dequeue_fifo(&inactive_list);
    page = dequeue_fifo(&nvm_free_list);
    if (page != NULL) {
      LOG("Out of hot memory -> move hot frame %lu to cold frame %lu\n", cp->devdax_offset, page->devdax_offset);
      LOG("\tmoving va: 0x%lx\n", cp->va);

      uint64_t old_offset = cp->devdax_offset;
      lru_migrate_down(cp, page->devdax_offset);
      page->devdax_offset = old_offset;
      page->in_dram = true;
      page->present = false;


      enqueue_fifo(&nvm_inactive_list, cp);
      enqueue_fifo(&dram_free_list, page);
    }
    
    
#endif
#ifdef LRU_SWAP
  }
#endif

  assert(!"Out of memory");
}


struct hemem_page* lru_pagefault(void)
{
  struct hemem_page *page;

  pthread_mutex_lock(&global_lock);
  // do the heavy lifting of finding the devdax file offset to place the page
  page = lru_allocate_page();
  pthread_mutex_unlock(&global_lock);
  assert(page != NULL);

  return page;
}

struct hemem_page* lru_pagefault_unlocked(void)
{
  struct hemem_page *page;

  page = lru_allocate_page();
  assert(page != NULL);

  return page;
}

void lru_remove_page(struct hemem_page *page)
{
  struct fifo_list *list;

  // Acquire global lock for proper synchronization across lists
  pthread_mutex_lock(&global_lock);
 
  assert(page != NULL);
  LOCK_TRACE_TRY("page->page_lock");
  pthread_mutex_lock(&(page->page_lock));
  LOCK_TRACE_GOT("page->page_lock");

  LOG("LRU: remove page: va: 0x%lx\n", page->va);
  
  list = page->list;
  assert(list != NULL);

  page_list_remove_page(list, page);
  page->present = false;

  if (page->in_dram) {
    enqueue_fifo(&dram_free_list, page);
  }
  else {
    enqueue_fifo(&nvm_free_list, page);
  }

  LOCK_TRACE_REL("page->page_lock");
  pthread_mutex_unlock(&(page->page_lock));
  pthread_mutex_unlock(&global_lock);
}


void lru_init(struct fifo_list *dram_fl, struct fifo_list *nvm_fl)
{
  LOG("lru_init: started with %lu DRAM pages and %lu NVM pages\n",
      dram_fl->numentries, nvm_fl->numentries);

  init_fifo_list(&active_list);
  init_fifo_list(&inactive_list);
  init_fifo_list(&written_list);
  init_fifo_list(&nvm_active_list);
  init_fifo_list(&nvm_inactive_list);
  init_fifo_list(&nvm_written_list);
  
  // Use the provided free lists instead of creating new ones
  dram_free_list = *dram_fl;
  nvm_free_list = *nvm_fl;

  lru_lists_initialized = true;
  lru_shutdown_requested = false;
  lru_scan_thread_active = false;
  lru_kswapd_thread_active = false;
  lru_runs = 0;
  vanum = 0;

  int r = pthread_create(&lru_scan_thread, NULL, lru_kscand, NULL);
  assert(r == 0);
  lru_scan_thread_active = true;
  
  r = pthread_create(&lru_kswapd_thread, NULL, lru_kswapd, NULL);
  assert(r == 0);
  lru_kswapd_thread_active = true;
  
#ifndef LRU_SWAP
  LOG("Memory management policy is LRU\n");
#else
  LOG("Memory management policy is LRU-swap\n");
#endif

  LOG("lru_init: finished\n");

}

void lru_shutdown(void)
{
  //fprintf(stderr, "[LRU-DEBUG] lru_shutdown: requested\n");
  lru_shutdown_requested = true;

  if (lru_scan_thread_active) {
    pthread_join(lru_scan_thread, NULL);
    lru_scan_thread_active = false;
  }

  if (lru_kswapd_thread_active) {
    pthread_join(lru_kswapd_thread, NULL);
    lru_kswapd_thread_active = false;
  }

  lru_shutdown_requested = false;

  if (lru_lists_initialized) {
    cleanup_fifo_list(&active_list);
    cleanup_fifo_list(&inactive_list);
    cleanup_fifo_list(&written_list);
    cleanup_fifo_list(&nvm_active_list);
    cleanup_fifo_list(&nvm_inactive_list);
    cleanup_fifo_list(&nvm_written_list);
    cleanup_fifo_list(&dram_free_list);
    cleanup_fifo_list(&nvm_free_list);
    lru_lists_initialized = false;
  }

  lru_runs = 0;
  vanum = 0;
  //fprintf(stderr, "[LRU-DEBUG] lru_shutdown: completed\n");
}

void lru_stats()
{
  LOG_STATS("\tactive_list.numentries: [%ld]\twritten_list.numentries: [%ld]\tinactive_list.numentries: [%ld]\tnvm_active_list.numentries: [%ld]\tnvm_written_list.numentries: [%ld]\tnvm_inactive_list.numentries: [%ld]\n",
          active_list.numentries,
          written_list.numentries,
          inactive_list.numentries,
          nvm_active_list.numentries,
          nvm_written_list.numentries,
          nvm_inactive_list.numentries);
}

