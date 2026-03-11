#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "arms_kernel_threads.h"

struct migration_task
{
    struct page_ref
    {
        uint64_t va;
        std::shared_ptr<page_info> page;
        int expected_hugepages;
    };

    std::vector<page_ref> promote_pages;
    std::vector<page_ref> demote_pages;
};

static std::deque<migration_task> migration_queue;
static std::mutex migration_queue_lock;
std::condition_variable migration_cv;

static bool contains_aligned_va(const std::vector<uint64_t> &vas, uint64_t aligned_va)
{
    return std::find(vas.begin(), vas.end(), aligned_va) != vas.end();
}

static std::vector<uint64_t> deduplicate_migration_vas(const std::vector<uint64_t> &vas,
                                                       const std::vector<uint64_t> *skip_vas = nullptr)
{
    std::vector<uint64_t> unique_vas;
    unique_vas.reserve(vas.size());

    for (uint64_t va : vas)
    {
        const uint64_t aligned_va = va & ~(PAGE_SIZE - 1);
        if ((skip_vas && contains_aligned_va(*skip_vas, aligned_va)) || contains_aligned_va(unique_vas, aligned_va))
        {
            continue;
        }
        unique_vas.push_back(aligned_va);
    }

    return unique_vas;
}

static migration_task::page_ref make_page_ref(uint64_t va)
{
    migration_task::page_ref ref{va & ~(PAGE_SIZE - 1), nullptr, 1};

    {
        std::shared_lock<std::shared_mutex> lock(pages_map_lock);
        auto it = pages_map.find(ref.va);
        if (it != pages_map.end())
        {
            ref.page = it->second;
        }
    }

    if (ref.page)
    {
        ref.expected_hugepages = 1;
    }

    return ref;
}

static void apply_hugepage_state_update(const migration_task::page_ref &ref, int target_node, int status)
{
    if (!ref.page || status != target_node)
    {
        return;
    }

    std::lock_guard<std::mutex> plock(ref.page->page_lock);

    ref.page->in_dram = (target_node == FAST_TIER);

#if USE_MODEL == (true) || LOGGING_RUN == (true)
    ref.page->promote_backoff = BACKOFF_PERIOD;
#endif
}

struct migration_result
{
    int requested_pages = 0;
    int promoted_pages = 0;
    int demoted_pages = 0;
    int numa_ret = 0;
    bool saw_nonbusy_error = false;
    std::vector<migration_task::page_ref> busy_promotions;
    std::vector<migration_task::page_ref> busy_demotions;
};

struct base_page_migration_result
{
    bool migrated = false;
    bool busy = false;
    bool saw_nonbusy_error = false;
};

static base_page_migration_result migrate_page_as_base_pages(const migration_task::page_ref &ref, int target_node)
{
    base_page_migration_result fallback_result;

    std::vector<void *> base_pages;
    base_pages.reserve(BASE_PAGE_PER_HUGEPAGE);
    std::vector<int> target_nodes(BASE_PAGE_PER_HUGEPAGE, target_node);
    std::vector<int> status(BASE_PAGE_PER_HUGEPAGE, -1);

    for (size_t page_idx = 0; page_idx < BASE_PAGE_PER_HUGEPAGE; ++page_idx)
    {
        const uint64_t base_page_va = ref.va + (page_idx * BASE_PAGE);
        base_pages.push_back(reinterpret_cast<void *>(base_page_va));
    }

    int numa_ret;
    {
        numa_ret = numa_move_pages(0, base_pages.size(), base_pages.data(), target_nodes.data(), status.data(),
                                   MPOL_MF_MOVE_ALL);
    }

    if (numa_ret < 0)
    {
        fallback_result.saw_nonbusy_error = true;
        return fallback_result;
    }

    int considered_base_pages = 0;
    int moved_base_pages = 0;
    for (size_t i = 0; i < status.size(); ++i)
    {
        const int page_status = status[i];
        if (page_status == target_node)
        {
            considered_base_pages++;
            moved_base_pages++;
        }
        else if (page_status == -EFAULT || page_status == -ENOENT)
        {
            continue;
        }
        else if (page_status == -EBUSY)
        {
            considered_base_pages++;
            fallback_result.busy = true;
        }
        else
        {
            considered_base_pages++;
            fallback_result.saw_nonbusy_error = true;
            std::cout << "[ARMS] Warning: 4K fallback migration failed for VA " << base_pages[i]
                      << ", target node: " << target_node << ", status: " << page_status << std::endl;
        }
    }

    fallback_result.migrated = (considered_base_pages > 0) && (moved_base_pages == considered_base_pages) &&
                               !fallback_result.busy && !fallback_result.saw_nonbusy_error;

    return fallback_result;
}

static bool migration_had_error(const migration_result &result)
{
    return result.numa_ret < 0 || result.saw_nonbusy_error;
}

static int expected_hugepages_count(const std::vector<migration_task::page_ref> &pages)
{
    int expected_pages = 0;
    for (const auto &page : pages)
    {
        expected_pages += page.expected_hugepages;
    }
    return expected_pages;
}

struct retry_migration_result
{
    int migrated_pages = 0;
    bool saw_error = false;
    std::vector<migration_task::page_ref> busy_pages;
};

template <typename CounterType>
static void account_successful_migrations(std::atomic<CounterType> &total_counter,
                                          std::atomic<CounterType> &period_counter, int migrated_pages)
{
    if (migrated_pages > 0)
    {
        total_counter.fetch_add(static_cast<CounterType>(migrated_pages), std::memory_order_relaxed);
        period_counter.fetch_add(static_cast<CounterType>(migrated_pages), std::memory_order_relaxed);
    }
}

static migration_result migrate_pages_to_node(const std::vector<migration_task::page_ref> &promote_pages,
                                              const std::vector<migration_task::page_ref> &demote_pages)
{
    migration_result result;

    if (promote_pages.empty() && demote_pages.empty())
        return result;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void *> pages;
    pages.reserve(promote_pages.size() + demote_pages.size());
    std::vector<int> nodes;
    nodes.reserve(promote_pages.size() + demote_pages.size());

    for (const auto &ref : promote_pages)
    {
        pages.push_back(reinterpret_cast<void *>(ref.va));
        nodes.push_back(FAST_TIER);
    }

    for (const auto &ref : demote_pages)
    {
        pages.push_back(reinterpret_cast<void *>(ref.va));
        nodes.push_back(SLOW_TIER);
    }

    std::vector<int> status(pages.size(), -1);

    result.requested_pages = static_cast<int>(pages.size());
    {
        result.numa_ret = numa_move_pages(0, pages.size(), pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);
    }

    int successful_promotions = 0;
    int successful_demotions = 0;
    for (size_t i = 0; i < status.size(); i++)
    {
        if (status[i] == nodes[i])
        {
            if (nodes[i] == FAST_TIER)
            {
                successful_promotions++;
            }
            else if (nodes[i] == SLOW_TIER)
            {
                successful_demotions++;
            }
        }
        else if (status[i] == -EBUSY)
        {
            if (nodes[i] == FAST_TIER)
            {
                result.busy_promotions.push_back(promote_pages[i]);
            }
            else if (nodes[i] == SLOW_TIER)
            {
                result.busy_demotions.push_back(demote_pages[i - promote_pages.size()]);
            }
        }
        /*else if (status[i] == -EFAULT || status[i] == -ENOENT)
        {
            const bool is_promotion = nodes[i] == FAST_TIER;
            const migration_task::page_ref &ref =
                is_promotion ? promote_pages[i] : demote_pages[i - promote_pages.size()];
            base_page_migration_result fallback_result = migrate_page_as_base_pages(ref, nodes[i]);

            if (fallback_result.migrated)
            {
                status[i] = nodes[i];
                if (is_promotion)
                {
                    successful_promotions++;
                }
                else
                {
                    successful_demotions++;
                }
            }
            else if (fallback_result.busy)
            {
                if (is_promotion)
                {
                    result.busy_promotions.push_back(ref);
                }
                else
                {
                    result.busy_demotions.push_back(ref);
                }
            }

            if (fallback_result.saw_nonbusy_error)
            {
                result.saw_nonbusy_error = true;
            }
        }*/
        else if (status[i] != nodes[i] && status[i] != -EFAULT && status[i] != -ENOENT)
        {
            result.saw_nonbusy_error = true;
            std::cout << "[ARMS] Warning: Page migration failed for VA " << pages[i] << ", target node: " << std::dec
                      << nodes[i] << ", status: " << status[i] << std::endl;
        }
    }

    result.promoted_pages = successful_promotions;
    result.demoted_pages = successful_demotions;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    float time_per_page_us = pages.empty() ? 0.0f : static_cast<float>(duration_us) / static_cast<float>(pages.size());

    {
        size_t status_offset = 0;
        for (const auto &ref : promote_pages)
        {
            apply_hugepage_state_update(ref, FAST_TIER, status[status_offset++]);
        }
        for (const auto &ref : demote_pages)
        {
            apply_hugepage_state_update(ref, SLOW_TIER, status[status_offset++]);
        }
    }

    // Update migration cost estimates for both directions if present
    if (result.promoted_pages > 0)
    {
        promotion_cost_avg = std::max((double)MIN_PROMOTION_COST, MIGRATION_COST_ALPHA * time_per_page_us +
                                                                      (1 - MIGRATION_COST_ALPHA) * promotion_cost_avg);
    }
    if (result.demoted_pages > 0)
    {
        demotion_cost_avg = std::max((double)MIN_DEMOTION_COST, MIGRATION_COST_ALPHA * time_per_page_us +
                                                                    (1 - MIGRATION_COST_ALPHA) * demotion_cost_avg);
    }

    return result;
}

template <typename CounterType>
static retry_migration_result migrate_with_retries(const std::vector<migration_task::page_ref> &pages, int target_node,
                                                   int max_passes, std::atomic<CounterType> &total_counter,
                                                   std::atomic<CounterType> &period_counter, const char *perror_label)
{
    retry_migration_result aggregate;
    if (pages.empty() || max_passes <= 0)
    {
        return aggregate;
    }

    std::vector<migration_task::page_ref> pending_pages = pages;
    for (int pass = 0; pass < max_passes && !pending_pages.empty(); ++pass)
    {
        if (pass > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(pass));
        }

        migration_result pass_result = (target_node == FAST_TIER) ? migrate_pages_to_node(pending_pages, {})
                                                                  : migrate_pages_to_node({}, pending_pages);

        const int migrated_this_pass =
            (target_node == FAST_TIER) ? pass_result.promoted_pages : pass_result.demoted_pages;
        aggregate.migrated_pages += migrated_this_pass;
        aggregate.saw_error = aggregate.saw_error || migration_had_error(pass_result);
        account_successful_migrations(total_counter, period_counter, migrated_this_pass);

        if (pass_result.numa_ret < 0)
        {
            perror(perror_label);
        }

        pending_pages =
            (target_node == FAST_TIER) ? std::move(pass_result.busy_promotions) : std::move(pass_result.busy_demotions);
    }

    aggregate.busy_pages = std::move(pending_pages);
    return aggregate;
}

void enqueue_migration_task(const std::vector<uint64_t> &promote_vas, const std::vector<uint64_t> &demote_vas)
{
    if (promote_vas.empty() && demote_vas.empty())
        return;

    std::vector<uint64_t> unique_promote_vas = deduplicate_migration_vas(promote_vas);
    std::vector<uint64_t> unique_demote_vas = deduplicate_migration_vas(demote_vas, &unique_promote_vas);

    if (unique_promote_vas.empty() && unique_demote_vas.empty())
    {
        return;
    }

    constexpr size_t TASK_COUNT = MIGRATION_WORKER_COUNT;
    std::vector<migration_task> tasks;
    tasks.reserve(TASK_COUNT);

    // Evenly split promotions and demotions across worker threads to keep load balanced.
    for (size_t i = 0; i < TASK_COUNT; i++)
    {
        size_t promote_begin = (unique_promote_vas.size() * i) / TASK_COUNT;
        size_t promote_end = (unique_promote_vas.size() * (i + 1)) / TASK_COUNT;

        size_t demote_begin = (unique_demote_vas.size() * i) / TASK_COUNT;
        size_t demote_end = (unique_demote_vas.size() * (i + 1)) / TASK_COUNT;

        if (promote_begin < promote_end || demote_begin < demote_end)
        {
            migration_task task;
            task.promote_pages.reserve(promote_end - promote_begin);
            task.demote_pages.reserve(demote_end - demote_begin);

            for (size_t idx = promote_begin; idx < promote_end; ++idx)
            {
                task.promote_pages.push_back(make_page_ref(unique_promote_vas[idx]));
            }
            for (size_t idx = demote_begin; idx < demote_end; ++idx)
            {
                task.demote_pages.push_back(make_page_ref(unique_demote_vas[idx]));
            }

            tasks.push_back(std::move(task));
        }
    }

    if (!tasks.empty())
    {
        std::unique_lock<std::mutex> lock(migration_queue_lock);
        for (auto &task : tasks)
        {
            migration_queue.push_back(std::move(task));
        }
    }

    migration_cv.notify_all();
}

void clear_migration_queue()
{
    std::lock_guard<std::mutex> lock(migration_queue_lock);
    migration_queue.clear();
}

void *migration_worker(void *arg)
{
    (void)arg;
    constexpr int MAX_DEMOTION_RETRY_PASSES = 1;
    constexpr int MAX_PROMOTION_RETRY_PASSES = 1;

    while (!terminated.load(std::memory_order_relaxed))
    {
        migration_task task;
        {
            std::unique_lock<std::mutex> lock(migration_queue_lock);
            migration_cv.wait(lock,
                              [] { return terminated.load(std::memory_order_relaxed) || !migration_queue.empty(); });
            if (migration_queue.empty())
            {
                continue;
            }
            task = std::move(migration_queue.front());
            migration_queue.pop_front();
        }

        // Try promotions first without freeing space via demotions.
        if (!task.promote_pages.empty())
        {
            retry_migration_result promotion_result = migrate_with_retries(
                task.promote_pages, FAST_TIER, 1, migrations_up, migrations_up_period, "numa_move_pages promote");

            const int expected_promotions = expected_hugepages_count(task.promote_pages);
            const bool promotions_failed =
                promotion_result.saw_error || (promotion_result.migrated_pages < expected_promotions);

            if (promotions_failed)
            {
                numa_set_preferred(SLOW_TIER);
                // std::cout << "[ARMS] changing preferred node to SLOW_TIER to assist demotions" << std::endl;
            }

            // If promotions failed, try demotions to free space, then retry promotions.
            if (promotions_failed && !task.demote_pages.empty())
            {
                retry_migration_result demotion_result =
                    migrate_with_retries(task.demote_pages, SLOW_TIER, MAX_DEMOTION_RETRY_PASSES, migrations_down,
                                         migrations_down_period, "numa_move_pages demote");

                if (ARMS_VERBOSE)
                {
                    std::cout << "[ARMS] Demoted " << demotion_result.migrated_pages << " hugepages to free up space.";
                    if (!demotion_result.busy_pages.empty())
                    {
                        std::cout << ' ' << demotion_result.busy_pages.size() << " hugepages remained busy after "
                                  << MAX_DEMOTION_RETRY_PASSES << " passes.";
                    }
                    if (demotion_result.saw_error)
                    {
                        std::cout << " (errors seen)";
                    }
                    std::cout << std::endl;
                }

                // Give allocator/reclaim a short chance to settle after demotions.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                // Retry promotions after freeing space in multiple passes, mirroring demotion retry behavior.
                retry_migration_result promote_retry_result =
                    migrate_with_retries(task.promote_pages, FAST_TIER, MAX_PROMOTION_RETRY_PASSES, migrations_up,
                                         migrations_up_period, "numa_move_pages promote");

                if (ARMS_VERBOSE)
                {
                    std::cout << "[ARMS] Promoted " << promote_retry_result.migrated_pages
                              << " hugepages after demotions.";
                    if (!promote_retry_result.busy_pages.empty())
                    {
                        std::cout << ' ' << promote_retry_result.busy_pages.size() << " hugepages remained busy after "
                                  << MAX_PROMOTION_RETRY_PASSES << " passes.";
                    }
                    if (promote_retry_result.saw_error)
                    {
                        std::cout << " (errors seen)";
                    }
                    std::cout << std::endl;
                }
            }
        }
    }

    return nullptr;
}
