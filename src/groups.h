#pragma once

#include "defs.h"
#include "hemem_page.h"

#include "khash.h"

#include <pthread.h>
#include <stdio.h>

struct page_group {
    uint64_t id;
    float sum;
    float avg;
    float max;
    uint32_t count;
};

void page_group_reset(struct page_group *pg);
void page_group_update(struct page_group *pg, const float count);

static_assert(sizeof(struct page_group) == 24);
KHASH_MAP_INIT_INT64(kGroupMap, struct page_group *);

struct group_tracker {
    khash_t(kGroupMap) * groups_map;
    pthread_mutex_t group_lock;
};

struct group_tracker *create_group_tracker();
uint64_t page_to_group_id(const uint64_t va);
void add_group_if_missing(struct group_tracker *gt, const struct hemem_page *page);
void reset_group_hash(struct group_tracker *gt);
void update_group_entry(struct group_tracker *gt, struct hemem_page *page);
struct page_group * try_get_group(struct group_tracker *, const uint64_t, const int8_t);