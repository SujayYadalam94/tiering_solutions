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
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "arms_kernel_threads.h"

struct migration_task
{
    std::vector<page_ptr> promote_pages;
    std::vector<page_ptr> demote_pages;
};

static std::deque<migration_task> migration_queue;
static std::mutex migration_queue_lock;
std::condition_variable migration_cv;
static std::unordered_set<pid_t> migration_worker_tids;
static std::shared_mutex migration_worker_tids_lock;

bool is_migration_worker_tid(pid_t tid)
{
    std::shared_lock<std::shared_mutex> lock(migration_worker_tids_lock);
    return migration_worker_tids.find(tid) != migration_worker_tids.end();
}

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

void enqueue_migration_task(const std::vector<page_ptr> &promote_pages, const std::vector<page_ptr> &demote_pages)
{
    if (promote_pages.empty() && demote_pages.empty())
        return;

    constexpr size_t TASK_COUNT = MIGRATION_WORKER_COUNT * 2;
    std::vector<migration_task> tasks;
    tasks.reserve(TASK_COUNT);

    // Evenly split promotions and demotions across worker threads to keep load balanced.
    for (size_t i = 0; i < TASK_COUNT; i++)
    {
        size_t promote_begin = (promote_pages.size() * i) / TASK_COUNT;
        size_t promote_end = (promote_pages.size() * (i + 1)) / TASK_COUNT;

        size_t demote_begin = (demote_pages.size() * i) / TASK_COUNT;
        size_t demote_end = (demote_pages.size() * (i + 1)) / TASK_COUNT;

        if (promote_begin < promote_end || demote_begin < demote_end)
        {
            migration_task task;
            task.promote_pages.reserve(promote_end - promote_begin);
            task.demote_pages.reserve(demote_end - demote_begin);

            for (size_t idx = promote_begin; idx < promote_end; ++idx)
            {
                task.promote_pages.push_back(promote_pages[idx]);
            }
            for (size_t idx = demote_begin; idx < demote_end; ++idx)
            {
                task.demote_pages.push_back(demote_pages[idx]);
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

static int log_move_base_pages(std::vector<page_ptr> pages, int target_node)
{
    std::vector<void *> vas;
    std::vector<int> nodes;
    std::vector<int> status;
    for (const auto &page : pages)
    {
        for (size_t offset = 0; offset < PAGE_SIZE; offset += BASE_PAGE_SIZE)
        {
            vas.push_back(reinterpret_cast<void *>(page->va + offset));
            status.push_back(-100);
            nodes.push_back(target_node);
        }
    }

    long ret = numa_move_pages(0, vas.size(), vas.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);
    int e = errno;

    // if ENOMEM is returned, it means the migration did not happen at all. We can abort retry until demotions.
    if (ret != 0 && e == ENOMEM)
    {
        return 1;
    }

    int total_failed_migrations = 0;
    for (int p = 0; p < pages.size(); p++)
    {
        int failed_migrations = 0;
        bool is_fragmented = false;
        for (int offset = 0; offset < PAGE_SIZE; offset += BASE_PAGE_SIZE)
        {
            int idx = p * (PAGE_SIZE / BASE_PAGE_SIZE) + (offset / BASE_PAGE_SIZE);
            if (status[idx] != target_node)
            {
                is_fragmented = true;
            }
            if (status[idx] != target_node && status[idx] != -ENOENT && status[idx] != -EFAULT)
            {
                failed_migrations++;
            }
        }

        pages[p]->fragmented = is_fragmented;
        pages[p]->found_in_pebs = false;

        if (failed_migrations > 0)
        {
            total_failed_migrations++;
        }
        else
        {
            pages[p]->in_dram = (target_node == FAST_TIER);
        }
    }

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Migration retry for fragmented page" << std::dec << " to node " << target_node << " had "
                  << total_failed_migrations << " failed base page migrations out of "
                  << (BASE_PAGE_PER_HUGEPAGE * pages.size()) << " total base pages." << std::endl;
    }

    return total_failed_migrations;
}

static int log_move_page(std::vector<page_ptr> &pages, int target_node, int retry)
{
    if (retry < 0)
    {
        return pages.size();
    }

    std::vector<void *> vas;
    std::vector<int> nodes;
    std::vector<int> status;

    std::vector<page_ptr> busy_migrations;
    std::vector<page_ptr> fragmented_migrations;

    for (const auto &ref : pages)
    {
        if (ref->fragmented)
        {
            fragmented_migrations.push_back(ref);
            continue;
        }

        vas.push_back(reinterpret_cast<void *>(ref->va));
        status.push_back(-100);
        nodes.push_back(target_node);
    }

    long ret = 0;
    int e;
    if (!vas.empty())
    {
        ret = numa_move_pages(0, vas.size(), vas.data(), nodes.data(), status.data(), MPOL_MF_MOVE_ALL);
        e = errno;
    }

    // if ENOMEM is returned, it means the migration did not happen at all. We can abort retry until demotions.
    if (ret != 0 && e == ENOMEM)
    {
        return vas.size();
    }

    int other_errors = 0;
    for (int i = 0; i < status.size(); i++)
    {
        const auto &page = pages[i];
        if (status[i] == target_node)
        {
            page->in_dram = (target_node == FAST_TIER);
        }
        else if (status[i] == -EBUSY)
        {
            busy_migrations.push_back(page);
        }
        else if (status[i] == -EFAULT || status[i] == -ENOENT)
        {
            fragmented_migrations.push_back(page);
        }
        else
        {
            other_errors++;
        }
    }
    int total_failed = busy_migrations.size() + fragmented_migrations.size() + other_errors;

    // All the migration worked
    if (total_failed == 0)
    {
        return 0;
    }

    // If there are any busy errors, we should retry as they are likely to be transient.
    int eperm_count = busy_migrations.size();
    if (eperm_count > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        eperm_count = log_move_page(busy_migrations, target_node, retry - 1);
    }

    // for missing migrations, try to migrate fragmented pages
    int fragmented_errors = fragmented_migrations.size();

    int wont_fix_fragmented = 0;
    std::vector<page_ptr> retry_fragmented;
    for (auto &page : fragmented_migrations)
    {
        if (page->found_in_pebs)
        {
            page->fragmented = true;
            page->found_in_pebs = false;
        }

        if (page->fragmented && page->would_migrate_fragmented)
        {
            retry_fragmented.push_back(page);
        }
        else
        {
            wont_fix_fragmented++;
        }
    }
    int retry_fragmented_failed = log_move_base_pages(retry_fragmented, target_node);

    return eperm_count + wont_fix_fragmented + retry_fragmented_failed + other_errors;
}

void *migration_worker(void *arg)
{
    (void)arg;
    {
        std::unique_lock<std::shared_mutex> lock(migration_worker_tids_lock);
        migration_worker_tids.insert(static_cast<pid_t>(syscall(SYS_gettid)));
    }

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
            auto start = std::chrono::high_resolution_clock::now();
            int initial_failed_promotions = log_move_page(task.promote_pages, FAST_TIER, 3);
            auto end = std::chrono::high_resolution_clock::now();
            promotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            int failed_demotions = 0;
            int failed_promotions = 0;

            if (initial_failed_promotions != 0)
            {
                start = std::chrono::high_resolution_clock::now();

                // Erase demotions after the number of remaining pages to promote * 1.5
                size_t demote_limit = static_cast<size_t>(initial_failed_promotions * 1.5);
                if (demote_limit < task.demote_pages.size())
                {
                    task.demote_pages.erase(task.demote_pages.begin() + demote_limit, task.demote_pages.end());
                }

                failed_demotions = log_move_page(task.demote_pages, SLOW_TIER, 3);
                end = std::chrono::high_resolution_clock::now();
                demotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                start = std::chrono::high_resolution_clock::now();
                failed_promotions = log_move_page(task.promote_pages, FAST_TIER, 3);
                end = std::chrono::high_resolution_clock::now();
                promotion_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                if (failed_promotions > 0 && errno == ENOMEM)
                {
                    // If we failed due to ENOMEM, do the migration in smaller batches to avoid OOM in the fast tier.
                    std::vector<page_ptr> prom_pages(task.promote_pages.begin(),
                                                     task.promote_pages.begin() + task.promote_pages.size() / 2);
                    failed_promotions -= (prom_pages.size() - log_move_page(prom_pages, FAST_TIER, 1));
                }
            }

            if ((failed_promotions != 0 || failed_demotions != 0) && ARMS_VERBOSE)
            {
                std::cout << "[ARMS] Migration worker fails - Initial promotions: " << initial_failed_promotions << "/"
                          << task.promote_pages.size() << ", Initial demotions: " << failed_demotions << "/"
                          << task.demote_pages.size() << ", Retried promotions: " << failed_promotions << "/"
                          << task.promote_pages.size() << std::endl;
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
