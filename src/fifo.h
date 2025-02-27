#ifndef HEMEM_FIFO_H
#define HEMEM_FIFO_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "hemem.h"

struct fifo_list {
  struct hemem_page *first, *last;
  pthread_mutex_t list_lock;
  size_t numentries;
};

struct migration_req_list {
  struct migration_req *first, *last;
  pthread_mutex_t list_lock;
  size_t numentries;
};

void enqueue_fifo(struct fifo_list *list, struct hemem_page *page);
struct hemem_page* dequeue_fifo(struct fifo_list *list);
void page_list_remove_page(struct fifo_list *list, struct hemem_page *page);
void next_page(struct fifo_list *list, struct hemem_page *page, struct hemem_page **res);
void enqueue_fifo_m(struct migration_req_list *list, struct migration_req *req);
struct migration_req* dequeue_fifo_m(struct migration_req_list *list);

#endif

