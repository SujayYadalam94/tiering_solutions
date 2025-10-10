/*
 * =====================================================================================
 *
 *       Filename:  simple.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/04/2020 09:58:58 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#include "../hemem.h"
#include "paging.h"
#include "../timer.h"
#include "../fifo.h"

uint64_t fastmem = 0;
uint64_t slowmem = 0;
bool slowmem_switch = false;

static struct fifo_list dram_free, nvm_free;

void simple_remove_page(struct hemem_page *page)
{
  if (page->in_dram) {
    page->present = false;
    enqueue_fifo(&dram_free, page);
    fastmem -= PAGE_SIZE;
  }
  else {
    page->present = false;
    enqueue_fifo(&nvm_free, page);
    slowmem -= PAGE_SIZE;
  }
}

struct hemem_page* simple_pagefault(void)
{
  struct timeval start, end;
  struct hemem_page *page;

  gettimeofday(&start, NULL);

  page = dequeue_fifo(&dram_free);
  if (page != NULL) {
    assert(!page->present);
    page->present = true;
    fastmem += PAGE_SIZE;  
  }
  else {
    assert(slowmem < nvmsize);
    page = dequeue_fifo(&nvm_free);
    
    assert(page != NULL);
    assert(!page->present);

    page->present = true;
    slowmem += PAGE_SIZE;
  }
  gettimeofday(&end, NULL);
  LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));
  
  return page;
}

void simple_init(struct fifo_list *dram_fl, struct fifo_list *nvm_fl)
{
  // Use the provided free lists instead of creating new ones
  dram_free = *dram_fl;
  nvm_free = *nvm_fl;
  
  LOG("Memory management policy is simple with %lu DRAM pages and %lu NVM pages\n",
      dram_fl->numentries, nvm_fl->numentries);
}

void simple_stats()
{
  LOG_STATS("\tfastmem_allocated: [%ld]\tslowmem_allocated: [%ld]\n", fastmem, slowmem);
}
