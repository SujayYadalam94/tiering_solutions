#include "groups.h"

void page_group_reset(struct page_group *pg) {
    pg->sum = 0;
    pg->avg = 0;
    pg->max = 0;
    pg->count = 0;
}

void page_group_update(struct page_group *pg, const float count) {
    pg->sum += count;
    pg->count++;
    pg->avg = pg->sum / pg->count;
    if (count > pg->max) {
        pg->max = count;
    }
}
struct group_tracker *create_group_tracker() {
    struct group_tracker *gt = (struct group_tracker *)malloc(sizeof(struct group_tracker));
    gt->groups_map = kh_init(kGroupMap);
    pthread_mutex_init(&gt->group_lock, NULL);
    return gt;
}

uint64_t page_to_group_id(const uint64_t va) {
    return (va / HUGEPAGE_SIZE) / 32;  // 64MB
    /// 128; // 256MB
}

void add_group_if_missing(struct group_tracker *gt,
                                        const struct hemem_page *page) {
    const uint64_t group_id = page_to_group_id(page->va);
    struct page_group *pg = malloc(sizeof(struct page_group));
    if (!pg) {
        perror("Failed to allocate memory for page_group");
        exit(EXIT_FAILURE);
    }
    pg->id = group_id;

    pthread_mutex_lock(&gt->group_lock);
    khiter_t k = kh_get(kGroupMap, gt->groups_map, group_id);
    if (k != kh_end(gt->groups_map)) {
        pthread_mutex_unlock(&gt->group_lock);
        return;
    }

    int absent;
    k = kh_put(kGroupMap, gt->groups_map, group_id, &absent);
    assert(absent);
    kh_value(gt->groups_map, k) = pg;
    pthread_mutex_unlock(&gt->group_lock);
}

void reset_group_hash(struct group_tracker *gt) {
    pthread_mutex_lock(&gt->group_lock);
    for (khiter_t key = kh_begin(gt->groups_map); key != kh_end(gt->groups_map);
         ++key) {
        if (!kh_exist(gt->groups_map, key)) {
            continue;
        }
        struct page_group *group = kh_val(gt->groups_map, key);
        page_group_reset(group);
    }
    pthread_mutex_unlock(&gt->group_lock);
}

void update_group_entry(struct group_tracker *gt,
                                              struct hemem_page *page) {
    const uint64_t group_id = page_to_group_id(page->va);

    pthread_mutex_lock(&gt->group_lock);
    khiter_t k = kh_get(kGroupMap, gt->groups_map, group_id);
    if (k == kh_end(gt->groups_map)) {
        printf("Something very bad happened\n");
        fflush(stdout);
        pthread_mutex_unlock(&gt->group_lock);
        return;
    }
    struct page_group *group = kh_val(gt->groups_map, k);
    page_group_update(group, page->count);
    pthread_mutex_unlock(&gt->group_lock);
}

struct page_group * try_get_group(struct group_tracker *gt,
                                              const uint64_t va,
                                              const int8_t offset){
    pthread_mutex_lock(&gt->group_lock);
    struct page_group *group_entry = NULL;
    khiter_t k = kh_get(kGroupMap, gt->groups_map, page_to_group_id(va) + offset);
    if (k != kh_end(gt->groups_map)) {
        group_entry = kh_val(gt->groups_map, k);
    }
    pthread_mutex_unlock(&gt->group_lock);
    return group_entry;
}