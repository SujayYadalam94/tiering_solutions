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

void simple_init(uint64_t dram_offset, uint64_t dram_size, uint64_t nvm_offset, uint64_t nvm_size)
{
  pthread_mutex_init(&(dram_free.list_lock), NULL);
  // Only create free pages for the assigned physical memory range
  uint64_t dram_pages = dram_size / PAGE_SIZE;
  for (uint64_t i = 0; i < dram_pages; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = dram_offset + (i * PAGE_SIZE);
    p->present = false;
    p->in_dram = true;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);
    enqueue_fifo(&dram_free, p);
  }

  pthread_mutex_init(&(nvm_free.list_lock), NULL);
  // Only create free pages for the assigned physical memory range
  uint64_t nvm_pages = nvm_size / PAGE_SIZE;
  for (uint64_t i = 0; i < nvm_pages; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = nvm_offset + (i * PAGE_SIZE);
    p->present = false;
    p->in_dram = false;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);
    enqueue_fifo(&nvm_free, p);
  }
  LOG("Memory management policy is simple with DRAM[0x%lx-0x%lx) NVM[0x%lx-0x%lx)\n",
      dram_offset, dram_offset + dram_size, nvm_offset, nvm_offset + nvm_size);
}

void simple_stats()
{
  LOG_STATS("\tfastmem_allocated: [%ld]\tslowmem_allocated: [%ld]\n", fastmem, slowmem);
}
