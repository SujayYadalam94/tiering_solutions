#pragma once

#include "defs.h"
#include "hemem_page.h"
#include "khash.h"

#include <array>

struct page_group {
    uint64_t id;
    float sum;
    float avg;
    float max;
    uint32_t count;

    page_group(const uint64_t &id) : id(id), sum(0), avg(0), max(0), count(0) {}

    inline void reset() {
        sum = 0;
        avg = 0;
        max = 0;
        count = 0;
    }

    inline void update(const float &ewma2) {
        sum += ewma2;
        count++;
        avg = sum / count;
        if (ewma2 > max) {
            max = ewma2;
        }
    }
};
static_assert(sizeof(struct page_group) == 24);

class group_tracker {
  public:
    KHASH_MAP_INIT_INT64(kGroupMap, struct page_group *);
    khash_t(kGroupMap) * page_groups;
    group_tracker() { page_groups = kh_init(kGroupMap); }
    ~group_tracker() { kh_destroy(kGroupMap, page_groups); }

    inline uint64_t page_to_group_id(const uint64_t &va);
    inline void add_group_if_missing(const uint64_t &va);
    inline void reset_group_hash();
    inline void update_group_entry(const uint64_t &va, const float &ewma2);
    inline struct page_group *try_get_group(const uint64_t &va,
                                            const int8_t &offset);
};

inline uint64_t group_tracker::page_to_group_id(const uint64_t &va) {
    return (va / HUGEPAGE_SIZE) / 512; // 1GB
}

inline void group_tracker::add_group_if_missing(const uint64_t &va) {
    thread_local static int ret;
    thread_local static int key;
    const uint64_t group_id = page_to_group_id(va);
    key = kh_put(kGroupMap, page_groups, group_id, &ret);
    if (ret == 1) { // bucket was empty
        kh_val(page_groups, key) = new struct page_group(group_id);
    }
}

inline void group_tracker::reset_group_hash() {
    khiter_t key;
    struct page_group *group;
    for (key = kh_begin(page_groups); key != kh_end(page_groups); ++key) {
        if (!kh_exist(page_groups, key)) {
            continue;
        }
        group = kh_val(page_groups, key);
        if (group == NULL) {
            continue;
        }
        group->reset();
    }
}

inline void group_tracker::update_group_entry(const uint64_t &va,
                                              const float &ewma2) {
    const khiter_t group_key =
        kh_get(kGroupMap, page_groups, page_to_group_id(va));
    if (group_key == kh_end(page_groups)) {
        printf("Something very bad happened\n");
        fflush(stdout);
    } else {
        // update the group with the page info
        kh_val(page_groups, group_key)->update(ewma2);
    }
}

inline struct page_group *
group_tracker::try_get_group(const uint64_t &va, const int8_t &offset = 0) {
    struct page_group *group_entry = NULL;
    const khiter_t key =
        kh_get(kGroupMap, page_groups, page_to_group_id(va) + offset);
    if (key != kh_end(page_groups)) {
        group_entry = kh_val(page_groups, key);
    }
    return group_entry;
}