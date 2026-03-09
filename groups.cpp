#include "groups.h"

namespace
{
page_group *allocate_group(uint64_t group_id)
{
    page_group *group = new page_group();
    if (!group)
    {
        perror("Failed to allocate memory for page_group");
        exit(EXIT_FAILURE);
    }

    page_group_reset(group);
    group->id = group_id;
    return group;
}

page_group *get_or_create_group_locked(struct group_tracker *gt, uint64_t group_id)
{
    auto it = gt->groups_map.find(group_id);
    if (it != gt->groups_map.end())
    {
        return it->second;
    }

    page_group *group = allocate_group(group_id);
    gt->groups_map[group_id] = group;
    return group;
}
} // namespace

void page_group_reset(struct page_group *pg)
{
    pg->sum = 0;
    pg->avg = 0;
    pg->max = 0;
    pg->sum_ewma5 = 0;
    pg->avg_ewma5 = 0;
    pg->max_ewma5 = 0;
    pg->sum_perc = 0;
    pg->avg_perc = 0;
    pg->max_perc = 0;
    pg->sum_perc_ewma5 = 0;
    pg->avg_perc_ewma5 = 0;
    pg->max_perc_ewma5 = 0;
    pg->max_age = 0;
    pg->count = 0;
}

void page_group_update(struct page_group *pg, const float count, const float ewma5, const float total_access,
                       const uint32_t page_age)
{
    pg->count++;

    if (page_age > pg->max_age)
    {
        pg->max_age = page_age;
    }

    pg->sum += count;
    pg->sum_ewma5 += ewma5;
    pg->avg = pg->sum / (float)pg->count;
    pg->avg_ewma5 = pg->sum_ewma5 / (float)pg->count;

    if (count > pg->max)
    {
        pg->max = count;
    }
    if (ewma5 > pg->max_ewma5)
    {
        pg->max_ewma5 = ewma5;
    }

    if (total_access > 0)
    {
        float perc = (count / total_access);
        float perc_ewma5 = (ewma5 / total_access);
        pg->sum_perc += perc;
        pg->avg_perc = pg->sum_perc / (float)pg->count;
        pg->sum_perc_ewma5 += perc_ewma5;
        pg->avg_perc_ewma5 = pg->sum_perc_ewma5 / (float)pg->count;
        if (perc > pg->max_perc)
        {
            pg->max_perc = perc;
        }
        if (perc_ewma5 > pg->max_perc_ewma5)
        {
            pg->max_perc_ewma5 = perc_ewma5;
        }
    }

    pg->max_age = std::max(pg->max_age, page_age);
    pg->count = count;
}

struct group_tracker *create_group_tracker()
{
    struct group_tracker *gt = new struct group_tracker;
    return gt;
}

uint64_t page_to_group_id(const uint64_t va)
{
    return (va / HUGEPAGE_SIZE) / 8UL;
}

void add_group_if_missing(struct group_tracker *gt, const std::shared_ptr<page_info> &page)
{
    std::lock_guard<std::mutex> lock(gt->group_lock);
    const uint64_t group_id = page_to_group_id(page->va);
    (void)get_or_create_group_locked(gt, group_id);
}

void reset_group_hash(struct group_tracker *gt)
{
    std::lock_guard<std::mutex> lock(gt->group_lock);
    for (auto &[key, group] : gt->groups_map)
    {
        page_group_reset(group);
    }
}

void update_group_entry(struct group_tracker *gt, const std::shared_ptr<page_info> &page, const float total_access)
{
    const uint64_t group_id = page_to_group_id(page->va);
    std::lock_guard<std::mutex> lock(gt->group_lock);
    struct page_group *group = get_or_create_group_locked(gt, group_id);

    page_group_update(group, page->count, page->w[1], total_access, page->age);
}

struct page_group *try_get_group(struct group_tracker *gt, const uint64_t va, const int8_t offset)
{
    const uint64_t group_id = page_to_group_id(va);

    std::lock_guard<std::mutex> lock(gt->group_lock);
    auto it = gt->groups_map.find(group_id + offset);
    if (it != gt->groups_map.end())
    {
        return it->second;
    }
    return NULL;
}

void get_group_window(struct group_tracker *gt, const uint64_t va, struct page_group *out_groups[15])
{
    const uint64_t group_id = page_to_group_id(va);

    std::lock_guard<std::mutex> lock(gt->group_lock);
    for (int offset = -7; offset <= 7; ++offset)
    {
        auto it = gt->groups_map.find(group_id + offset);
        out_groups[offset + 7] = (it != gt->groups_map.end()) ? it->second : NULL;
    }
}
