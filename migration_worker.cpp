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
        std::lock_guard<std::mutex> page_lock(ref.page->page_lock);
        ref.expected_hugepages = ref.page->seen_pages > 0 ? 1 : 0;
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

    ref.page->seen_pages = BASE_PAGE_PER_HUGEPAGE;
    ref.page->pages_in_dram = (target_node == FAST_TIER) ? BASE_PAGE_PER_HUGEPAGE : 0;
    for (size_t j = 0; j < BASE_PAGE_PER_HUGEPAGE; j++)
    {
        ref.page->page_status[j] = target_node;
    }

#if USE_MODEL == (true) || LOGGING_RUN == (true)
    ref.page->promote_backoff = BACKOFF_PERIOD;
#endif
}

static int migrate_pages_to_node(const std::vector<migration_task::page_ref> &promote_pages,
                                 const std::vector<migration_task::page_ref> &demote_pages, int &promoted_pages,
                                 int &demoted_pages, int &numa_ret)
{
    if (promote_pages.empty() && demote_pages.empty())
        return 0;

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

    numa_ret = numa_move_pages(0, pages.size(), pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);

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
        else if (status[i] != nodes[i] && status[i] != -EFAULT && status[i] != -ENOENT)
        {
            // std::cerr << "[ARMS] Warning: Page migration failed for VA " << pages[i] << ", target node: " << std::dec
            //           << nodes[i] << ", status: " << status[i] << std::endl;
        }
    }

    promoted_pages = successful_promotions;
    demoted_pages = successful_demotions;

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
    if (promoted_pages > 0)
    {
        promotion_cost_avg = std::max((double)MIN_PROMOTION_COST, MIGRATION_COST_ALPHA * time_per_page_us +
                                                                      (1 - MIGRATION_COST_ALPHA) * promotion_cost_avg);
    }
    if (demoted_pages > 0)
    {
        demotion_cost_avg = std::max((double)MIN_DEMOTION_COST, MIGRATION_COST_ALPHA * time_per_page_us +
                                                                    (1 - MIGRATION_COST_ALPHA) * demotion_cost_avg);
    }

    return static_cast<int>(pages.size());
}

void enqueue_migration_task(const std::vector<uint64_t> &promote_vas, const std::vector<uint64_t> &demote_vas)
{
#if USE_MODEL == (true) || LOGGING_RUN == (true)
    numa_set_preferred(FAST_TIER);
#endif
    if (promote_vas.empty() && demote_vas.empty())
        return;

    constexpr size_t TASK_COUNT = MIGRATION_WORKER_COUNT;
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

void *migration_worker(void *arg)
{
    (void)arg;
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
            int promoted_pages = 0;
            int demoted_pages_unused = 0;
            int numa_ret;
            int ret = migrate_pages_to_node(task.promote_pages, {}, promoted_pages, demoted_pages_unused, numa_ret);

            int expected_promotions = 0;
            for (const auto &page : task.promote_pages)
            {
                expected_promotions += page.expected_hugepages;
            }
            bool promotions_failed = (ret < 0) || (promoted_pages < expected_promotions);

            if (ret > 0 && promoted_pages > 0)
            {
                migrations_up.fetch_add(promoted_pages, std::memory_order_relaxed);
                migrations_up_period.fetch_add(promoted_pages, std::memory_order_relaxed);
            }

            if (promotions_failed)
            {
                numa_set_preferred(SLOW_TIER);
                // std::cout << "[ARMS] changing preferred node to SLOW_TIER to assist demotions" << std::endl;
            }

            // If promotions failed, try demotions to free space, then retry promotions.
            if (promotions_failed && !task.demote_pages.empty())
            {
                int promoted_pages_unused = 0;
                int demoted_pages = 0;
                int demote_ret =
                    migrate_pages_to_node({}, task.demote_pages, promoted_pages_unused, demoted_pages, numa_ret);
                if (demote_ret > 0 && demoted_pages > 0)
                {
                    migrations_down.fetch_add(demoted_pages, std::memory_order_relaxed);
                    migrations_down_period.fetch_add(demoted_pages, std::memory_order_relaxed);
                }
                if (numa_ret != 0)
                {
                    perror("numa_move_pages demote");
                }
                else
                {
                    if (ARMS_VERBOSE)
                    {
                        std::cout << "[ARMS] Demoted " << demoted_pages << " hugepages to free up space." << std::endl;
                    }
                }

                // Give allocator/reclaim a short chance to settle after demotions.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                // Retry promotions after freeing space in multiple passes, only for hugepages that still fail.
                int expected_retry_promotions = 0;
                for (const auto &page : task.promote_pages)
                {
                    expected_retry_promotions += page.expected_hugepages;
                }
                int total_retry_promoted = 0;
                bool retry_error = false;
                std::vector<migration_task::page_ref> pending_promotions = task.promote_pages;
                constexpr int MAX_PROMOTION_RETRY_PASSES = 1;

                for (int pass = 0; pass < MAX_PROMOTION_RETRY_PASSES && !pending_promotions.empty(); pass++)
                {
                    std::vector<migration_task::page_ref> failed_this_pass;
                    failed_this_pass.reserve(pending_promotions.size());

                    for (const auto &page : pending_promotions)
                    {
                        const int expected_for_hugepage = page.expected_hugepages;
                        if (expected_for_hugepage == 0)
                        {
                            continue;
                        }

                        promoted_pages = 0;
                        demoted_pages_unused = 0;
                        int retry_ret =
                            migrate_pages_to_node({page}, {}, promoted_pages, demoted_pages_unused, numa_ret);
                        if (retry_ret > 0 && promoted_pages > 0)
                        {
                            migrations_up.fetch_add(promoted_pages, std::memory_order_relaxed);
                            migrations_up_period.fetch_add(promoted_pages, std::memory_order_relaxed);
                            total_retry_promoted += promoted_pages;
                        }

                        const bool hugepage_fully_promoted = (promoted_pages >= expected_for_hugepage);
                        if (!hugepage_fully_promoted)
                        {
                            failed_this_pass.push_back(page);
                        }

                        if (retry_ret < 0 || numa_ret != 0)
                        {
                            // perror("numa_move_pages promote");
                            retry_error = true;
                        }
                    }

                    pending_promotions.swap(failed_this_pass);

                    if (!pending_promotions.empty() && (pass + 1) < MAX_PROMOTION_RETRY_PASSES)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2 * (pass + 1)));
                    }
                }
                int retry_failed_pages = std::max(0, expected_retry_promotions - total_retry_promoted);
                if (ARMS_VERBOSE)
                {
                    std::cout << "[ARMS] Promoted " << total_retry_promoted
                              << " hugepages after demotions (multi-pass per-hugepage retry). " << retry_failed_pages
                              << " hugepages failed to promote." << (retry_error ? " (errors seen)" : "") << std::endl;
                }
            }
        }
    }

    return nullptr;
}
