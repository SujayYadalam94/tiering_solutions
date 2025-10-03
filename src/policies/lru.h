#ifndef HEMEM_LRU_H
#define HEMEM_LRU_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#include "../hemem.h"
#include "paging.h"


#define KSCAND_INTERVAL   (50000) // in us (20ms)
#define KSWAPD_INTERVAL   (1000000) // in us (1s)
#define KSWAPD_MIGRATE_RATE  (50UL * 1024UL * 1024UL * 1024UL) // 50GB

void *lru_kswapd();
struct hemem_page* lru_pagefault(void);
struct hemem_page* lru_pagefault_unlocked(void);
void lru_init(uint64_t dram_offset, uint64_t dram_size, uint64_t nvm_offset, uint64_t nvm_size);
void lru_remove_page(struct hemem_page *page);
void lru_stats();
void lru_shutdown(void);


#endif /*  HEMEM_LRU_MODIFIED_H  */
