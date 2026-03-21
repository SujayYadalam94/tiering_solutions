#pragma once

#include "defs.h"
#include "page.h"
#include <memory>

#include <pthread.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

struct page_group
{
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
    uint32_t max_age;
    uint32_t count;
};

void page_group_reset(struct page_group *pg);
void page_group_update(struct page_group *pg, const float count, const float ewma5, const float total_access,
                       const uint32_t page_age);

struct group_tracker
{
    std::unordered_map<uint64_t, struct page_group *> groups_map;
    std::mutex group_lock;
};

struct group_snapshot
{
    std::unordered_map<uint64_t, struct page_group *> groups_map;
};

struct group_tracker *create_group_tracker();
uint64_t page_to_group_id(const uint64_t va);
void add_group_if_missing(struct group_tracker *gt, const page_ptr &page);
void reset_group_hash(struct group_tracker *gt);
void update_group_entry(struct group_tracker *gt, const page_ptr &page, const float total_access);
void update_group_entries_bulk(struct group_tracker *gt, const std::vector<score_entry> &scores, const float total_access);
struct page_group *try_get_group(struct group_tracker *, const uint64_t, const int8_t);
void get_group_window(struct group_tracker *gt, const uint64_t va, struct page_group *out_groups[15]);
void snapshot_group_tracker(struct group_tracker *gt, struct group_snapshot &snapshot);
void get_group_window_from_snapshot(const struct group_snapshot &snapshot, const uint64_t va,
                                    struct page_group *out_groups[15]);