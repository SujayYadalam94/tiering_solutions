#ifndef ARMS_LRU_H
#define ARMS_LRU_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#include "../arms.h"
#include "paging.h"


#define KSCAND_INTERVAL   (50000) // in us (20ms)
#define KSWAPD_INTERVAL   (1000000) // in us (1s)
#define KSWAPD_MIGRATE_RATE  (50UL * 1024UL * 1024UL * 1024UL) // 50GB

void *lru_kswapd();
struct arms_page* lru_pagefault(void);
struct arms_page* lru_pagefault_unlocked(void);
void lru_init(void);
void lru_remove_page(struct arms_page *page);
void lru_stats();


#endif /*  ARMS_LRU_MODIFIED_H  */
