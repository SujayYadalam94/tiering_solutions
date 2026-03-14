#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <sstream>
#include <string>
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
    ref.page->promote_backoff = BACKOFF_PERIOD;
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

    fallback_result.migrated = (considered_base_pages > 0) && (moved_base_pages > 0.8 * considered_base_pages);

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
    result.numa_ret = numa_move_pages(0, pages.size(), pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);

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
        else if (status[i] == -EPERM)
        {
            const bool is_promotion = nodes[i] == FAST_TIER;
            const migration_task::page_ref &ref =
                is_promotion ? promote_pages[i] : demote_pages[i - promote_pages.size()];
            ref.page->promote_backoff = BACKOFF_PERIOD * 50;
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

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Migration result - Requested: " << result.requested_pages
                  << ", Promoted: " << result.promoted_pages << ", Demoted: " << result.demoted_pages
                  << ", Time per page (us): " << time_per_page_us << ", Promotion cost avg (us): " << promotion_cost_avg
                  << ", Demotion cost avg (us): " << demotion_cost_avg
                  << ", Saw non-busy error: " << (result.saw_nonbusy_error ? "Yes" : "No") << std::endl;
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

    constexpr size_t TASK_COUNT = MIGRATION_WORKER_COUNT * 2;
    std::vector<migration_task> tasks;
    tasks.reserve(TASK_COUNT);

    // Evenly split promotions and demotions across worker threads to keep load balanced.
    for (size_t i = 0; i < TASK_COUNT; i++)
    {
        size_t promote_begin = (promote_vas.size() * i) / TASK_COUNT;
        size_t promote_end = (promote_vas.size() * (i + 1)) / TASK_COUNT;

        size_t demote_begin = (demote_vas.size() * i) / TASK_COUNT;
        size_t demote_end = (demote_vas.size() * (i + 1)) / TASK_COUNT;

        if (promote_begin < promote_end || demote_begin < demote_end)
        {
            migration_task task;
            task.promote_pages.reserve(promote_end - promote_begin);
            task.demote_pages.reserve(demote_end - demote_begin);

            for (size_t idx = promote_begin; idx < promote_end; ++idx)
            {
                task.promote_pages.push_back(make_page_ref(promote_vas[idx]));
            }
            for (size_t idx = demote_begin; idx < demote_end; ++idx)
            {
                task.demote_pages.push_back(make_page_ref(demote_vas[idx]));
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

static int count_status_migrations(std::vector<int> status, int status_target, bool not_equal = false)
{
    int count = 0;
    for (const auto &s : status)
    {
        if (not_equal)
        {
            if (s != status_target)
            {
                count++;
            }
        }
        else if (s == status_target)
        {
            count++;
        }
    }
    return count;
}

static int log_move_page(std::vector<void *> &vas, std::vector<int> &status, int target_node, int retry)
{
    if (retry < 0)
    {
        return vas.size();
    }

    std::vector<int> promote_nodes;
    for (auto _ : vas)
    {
        promote_nodes.push_back(target_node);
    }

    long ret = numa_move_pages(0, vas.size(), vas.data(), promote_nodes.data(), status.data(), MPOL_MF_MOVE_ALL);
    int e = errno;

    // if ENOMEM is returned, it means the migration did not happen at all. We can abort retry until demotions.
    if (ret != 0 && e == ENOMEM)
    {
        return vas.size();
    }

    int succeeded_migrations = count_status_migrations(status, target_node);
    if (succeeded_migrations == vas.size())
    {
        return 0;
    }

    // If there are any non-busy errors, we should retry as they are likely to be transient.
    int eperm_count = count_status_migrations(status, -EPERM);
    if (eperm_count > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return log_move_page(vas, status, target_node, retry - 1);
    }

    return vas.size() - succeeded_migrations;
}

void *migration_worker(void *arg)
{
    (void)arg;
    constexpr int MAX_DEMOTION_RETRY_PASSES = 2;
    constexpr int MAX_PROMOTION_RETRY_PASSES = 2;

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

        std::chrono::microseconds promotion_time(0);
        std::chrono::microseconds demotion_time(0);

        if (!task.promote_pages.empty())
        {
            auto promote_pages = task.promote_pages;
            std::vector<void *> promote_vas;
            std::vector<int> promote_status;
            for (const auto &ref : promote_pages)
            {
                promote_vas.push_back(reinterpret_cast<void *>(ref.va));
                promote_status.push_back(-100);
            }

            auto demote_pages = task.demote_pages;
            std::vector<void *> demote_vas;
            std::vector<int> demote_nodes;
            std::vector<int> demote_status;
            for (const auto &ref : demote_pages)
            {
                demote_vas.push_back(reinterpret_cast<void *>(ref.va));
                demote_status.push_back(-100);
            }

            auto start = std::chrono::high_resolution_clock::now();
            int initial_failed_promotions = log_move_page(promote_vas, promote_status, FAST_TIER, 3);
            auto end = std::chrono::high_resolution_clock::now();
            promotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            int failed_demotions = 0;
            int failed_promotions = 0;

            if (initial_failed_promotions != 0)
            {
                start = std::chrono::high_resolution_clock::now();
                failed_demotions = log_move_page(demote_vas, demote_status, SLOW_TIER, 3);
                end = std::chrono::high_resolution_clock::now();
                demotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                start = std::chrono::high_resolution_clock::now();
                failed_promotions = log_move_page(promote_vas, promote_status, FAST_TIER, 3);
                end = std::chrono::high_resolution_clock::now();
                promotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                if (failed_promotions > 0 && errno == ENOMEM)
                {
                    // If we failed due to ENOMEM, do the migration in smaller batches to avoid OOM in the fast tier.
                    for (auto prom_va : promote_vas)
                    {
                        std::vector<void *> single_prom_va = {prom_va};
                        std::vector<int> single_prom_status = {-100};
                        if (log_move_page(single_prom_va, single_prom_status, FAST_TIER, 1) == 0)
                        {
                            failed_promotions--;
                        }
                    }
                }
            }

            if (failed_promotions != 0 || failed_demotions != 0)
            {
                std::cout << "[ARMS] Migration worker fails - Initial promotions: " << initial_failed_promotions << "/"
                          << promote_vas.size() << ", Initial demotions: " << failed_demotions << "/"
                          << demote_vas.size() << ", Retried promotions: " << failed_promotions << "/"
                          << promote_vas.size() << std::endl;
            }
        }

        if (task.promote_pages.size() > 0)
        {
            float time_per_page_us =
                static_cast<float>(promotion_time.count()) / static_cast<float>(task.promote_pages.size());

            promotion_cost_avg =
                std::max((double)MIN_PROMOTION_COST,
                         MIGRATION_COST_ALPHA * time_per_page_us + (1 - MIGRATION_COST_ALPHA) * promotion_cost_avg);
        }
        if (task.demote_pages.size() > 0)
        {
            float time_per_page_us =
                static_cast<float>(demotion_time.count()) / static_cast<float>(task.demote_pages.size());
            demotion_cost_avg = std::max((double)MIN_DEMOTION_COST, MIGRATION_COST_ALPHA * time_per_page_us +
                                                                        (1 - MIGRATION_COST_ALPHA) * demotion_cost_avg);
        }
    }

    return nullptr;
}
