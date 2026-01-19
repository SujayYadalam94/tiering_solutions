#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "arms_kernel_threads.h"

struct migration_task
{
    std::vector<uint64_t> promote_vas;
    std::vector<uint64_t> demote_vas;
};

static std::deque<migration_task> migration_queue;
static std::mutex migration_queue_lock;
static std::condition_variable migration_cv;

static int migrate_pages_to_node(const std::vector<uint64_t> &promote_vas, const std::vector<uint64_t> &demote_vas,
                                 int &promoted_pages, int &demoted_pages)
{
    if (promote_vas.empty() && demote_vas.empty())
        return 0;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void *> pages;
    pages.reserve((promote_vas.size() + demote_vas.size()) * BASE_PAGE_PER_HUGEPAGE);
    std::vector<int> nodes;
    nodes.reserve((promote_vas.size() + demote_vas.size()) * BASE_PAGE_PER_HUGEPAGE);

    for (uint64_t va : promote_vas)
    {
        for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
        {
            pages.push_back(reinterpret_cast<void *>(va + i * BASE_PAGE));
            nodes.push_back(FAST_TIER);
        }
    }

    for (uint64_t va : demote_vas)
    {
        for (size_t i = 0; i < BASE_PAGE_PER_HUGEPAGE; i++)
        {
            pages.push_back(reinterpret_cast<void *>(va + i * BASE_PAGE));
            nodes.push_back(SLOW_TIER);
        }
    }

    std::vector<int> status(pages.size(), -1);

    int ret = numa_move_pages(0, pages.size(), pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);

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
    float time_per_page_us = (float)duration_us / (float)pages.size() * BASE_PAGE_PER_HUGEPAGE;

    // Update per-page tracking: page_status[], seen_pages, pages_in_dram
    // Group updates by hugepage (`page_info`) to recompute counts once per page.
    {
        // Map each hugepage to the list of (base-page index, status)
        std::unordered_map<page_info *, std::vector<std::pair<int, int>>> page_updates;
        {
            std::lock_guard<std::mutex> lock(pages_map_lock);
            for (size_t i = 0; i < pages.size(); i++)
            {
                uint64_t base_va = ((uint64_t)pages[i]) & ~(PAGE_SIZE - 1);
                auto it = pages_map.find(base_va);
                if (it == pages_map.end())
                {
                    continue; // Not tracked; skip
                }
                page_info *pi = it->second.get();
                size_t idx = (((uint64_t)pages[i]) - base_va) / BASE_PAGE;
                if (idx < BASE_PAGE_PER_HUGEPAGE)
                {
                    page_updates[pi].emplace_back(static_cast<int>(idx), status[i]);
                }
            }
        }

        // Apply updates and recompute counts
        for (auto &kv : page_updates)
        {
            page_info *pi = kv.first;
            {
                std::lock_guard<std::mutex> plock(pi->page_lock);
                for (const auto &entry : kv.second)
                {
                    int idx = entry.first;
                    int st = entry.second;
                    // Record the latest status per base page; negative codes mean unknown/failed
                    pi->page_status[idx] = st;
                }

                bool invalid_status = false;
                int seen = 0;
                int in_dram = 0;
                for (int j = 0; j < BASE_PAGE_PER_HUGEPAGE; j++)
                {
                    int st = pi->page_status[j];
                    if (st < 0 && st != -EFAULT && st != -ENOENT)
                    {
                        invalid_status = true;
                        break;
                    }
                    if (st >= 0)
                    {
                        seen++;
                        if (st == FAST_TIER)
                        {
                            in_dram++;
                        }
                    }
                }
                if (!invalid_status)
                {
                    if (pi->pages_in_dram == in_dram)
                    {
                        pi->promote_backoff = BACKOFF_PERIOD;
                    }
                    pi->seen_pages = seen;
                    pi->pages_in_dram = in_dram;
                }
            }
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

    if (ret != 0)
    {
        perror("numa_move_pages");
        return -1;
    }

    return pages.size();
}

void enqueue_migration_task(const std::vector<uint64_t> &promote_vas, const std::vector<uint64_t> &demote_vas)
{
    if (promote_vas.empty() && demote_vas.empty())
        return;

    constexpr size_t MAX_PAGES_PER_TASK = 50;
    size_t promote_offset = 0;
    size_t demote_offset = 0;

    // Pair up promotions and demotions so workers handle both directions together.
    while (promote_offset < promote_vas.size() || demote_offset < demote_vas.size())
    {
        size_t promote_end = std::min(promote_offset + MAX_PAGES_PER_TASK, promote_vas.size());
        size_t demote_end = std::min(demote_offset + MAX_PAGES_PER_TASK, demote_vas.size());

        std::unique_lock<std::mutex> lock(migration_queue_lock);
        migration_queue.push_back(
            {std::vector<uint64_t>(promote_vas.begin() + promote_offset, promote_vas.begin() + promote_end),
             std::vector<uint64_t>(demote_vas.begin() + demote_offset, demote_vas.begin() + demote_end)});

        promote_offset = promote_end;
        demote_offset = demote_end;
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
    while (true)
    {
        migration_task task;
        {
            std::unique_lock<std::mutex> lock(migration_queue_lock);
            // migration_cv.wait(lock, [] { return terminated || !migration_queue.empty(); });
            if (migration_queue.empty())
            {
                if (terminated)
                {
                    return nullptr;
                }
                else
                {
                    continue;
                }
            }
            task = std::move(migration_queue.front());
            migration_queue.pop_front();
        }

        // Try promotions first without freeing space via demotions.
        if (!task.promote_vas.empty())
        {
            int promoted_pages = 0;
            int demoted_pages_unused = 0;
            int ret = migrate_pages_to_node(task.promote_vas, {}, promoted_pages, demoted_pages_unused);

            const int expected_promotions = static_cast<int>(task.promote_vas.size() * BASE_PAGE_PER_HUGEPAGE);
            bool promotions_failed = (ret < 0) || (promoted_pages < expected_promotions);

            if (ret > 0 && promoted_pages > 0)
            {
                migrations_up.fetch_add(promoted_pages, std::memory_order_relaxed);
                migrations_up_period.fetch_add(promoted_pages, std::memory_order_relaxed);
            }

            // If promotions failed, try demotions to free space, then retry promotions.
            if (promotions_failed && !task.demote_vas.empty())
            {
                int promoted_pages_unused = 0;
                int demoted_pages = 0;
                int demote_ret = migrate_pages_to_node({}, task.demote_vas, promoted_pages_unused, demoted_pages);
                if (demote_ret > 0 && demoted_pages > 0)
                {
                    migrations_down.fetch_add(demoted_pages, std::memory_order_relaxed);
                    migrations_down_period.fetch_add(demoted_pages, std::memory_order_relaxed);
                }

                // Retry promotions after freeing space.
                promoted_pages = 0;
                demoted_pages_unused = 0;
                ret = migrate_pages_to_node(task.promote_vas, {}, promoted_pages, demoted_pages_unused);
                if (ret > 0 && promoted_pages > 0)
                {
                    migrations_up.fetch_add(promoted_pages, std::memory_order_relaxed);
                    migrations_up_period.fetch_add(promoted_pages, std::memory_order_relaxed);
                }
            }
        }
        else if (!task.demote_vas.empty())
        {
            // No promotions requested; still honor demotions if provided.
            int promoted_pages_unused = 0;
            int demoted_pages = 0;
            int ret = migrate_pages_to_node({}, task.demote_vas, promoted_pages_unused, demoted_pages);
            if (ret > 0 && demoted_pages > 0)
            {
                migrations_down.fetch_add(demoted_pages, std::memory_order_relaxed);
                migrations_down_period.fetch_add(demoted_pages, std::memory_order_relaxed);
            }
        }
    }

    return nullptr;
}
