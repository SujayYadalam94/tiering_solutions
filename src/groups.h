#pragma once

#include "defs.h"
#include "hemem_page.h"

#include <array>
#include <boost/unordered_map.hpp>

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
    boost::unordered_map<uint64_t, page_group *> page_groups;
    group_tracker() {}
    ~group_tracker() {}

    inline uint64_t page_to_group_id(const uint64_t &va);
    inline void add_group_if_missing(const uint64_t &va);
    inline void reset_group_hash();
    inline void update_group_entry(const uint64_t &va, const float &ewma2);
    inline struct page_group *try_get_group(const uint64_t &va,
                                            const int8_t &offset);
};

inline uint64_t group_tracker::page_to_group_id(const uint64_t &va) {
    return (va / HUGEPAGE_SIZE) / 128; // 1GB
}

inline void group_tracker::add_group_if_missing(const uint64_t &va) {
    thread_local static int ret;
    thread_local static int key;
    const uint64_t group_id = page_to_group_id(va);
    if (page_groups.find(group_id) != page_groups.end()) {
        return; // group already exists
    }
    page_groups[group_id] = new struct page_group(group_id);
}

inline void group_tracker::reset_group_hash() {
    for (auto &entry : page_groups) {
        auto group = reinterpret_cast<page_group *>(entry.second);
        if (group) {
            group->reset();
        }
    }
}

inline void group_tracker::update_group_entry(const uint64_t &va,
                                              const float &ewma) {
    const uint64_t group_id = page_to_group_id(va);
    auto it = page_groups.find(group_id);
    if (it == page_groups.end()) {
        printf("Something very bad happened\n");
        fflush(stdout);
    } else {
        auto group = reinterpret_cast<page_group *>(it->second);
        group->update(ewma);
    }
}

inline struct page_group *
group_tracker::try_get_group(const uint64_t &va, const int8_t &offset = 0) {
    struct page_group *group_entry = NULL;
    auto it = page_groups.find(page_to_group_id(va) + offset);
    if (it != page_groups.end()) {
        group_entry = reinterpret_cast<page_group *>(it->second);
    }
    return group_entry;
}