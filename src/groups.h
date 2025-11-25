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

    float sum_ewma5;
    float avg_ewma5;
    float max_ewma5;

    float sum_perc;
    float avg_perc;
    float max_perc;

    float sum_perc_ewma5;
    float avg_perc_ewma5;
    float max_perc_ewma5;
    uint32_t count;
};

void page_group_reset(struct page_group *pg);
void page_group_update(struct page_group *pg, const float count, const float ewma5, const float total);

KHASH_MAP_INIT_INT64(kGroupMap, struct page_group *);

struct group_tracker {
    khash_t(kGroupMap) * groups_map;
    pthread_mutex_t group_lock;
};

struct group_tracker *create_group_tracker();
uint64_t page_to_group_id(const uint64_t va);
void add_group_if_missing(struct group_tracker *gt, const struct hemem_page *page);
void reset_group_hash(struct group_tracker *gt);
void update_group_entry(struct group_tracker *gt, struct hemem_page *page, const float total);
struct page_group *try_get_group(struct group_tracker *, const uint64_t, const int8_t);