#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <vector>

#include "arms_kernel_threads.h"

static std::mutex madvise_queue_lock;
std::condition_variable madvise_cv;
static std::deque<uint64_t> madvise_queue;

// Attempt to populate the page; on failure, move it to the remote tier and retry once.
static inline void populate_with_remote_retry(uint64_t addr)
{
    if (addr == 0)
    {
        return;
    }

    if (madvise((void *)addr, PAGE_SIZE, MADV_POPULATE_WRITE) == 0)
    {
        return;
    }

    void *pages[1] = {(void *)addr};
    int nodes[1] = {SLOW_TIER};
    int status[1] = {-1};
    numa_move_pages(0, 1, pages, nodes, status, MPOL_MF_MOVE_ALL);

    if (madvise((void *)addr, PAGE_SIZE, MADV_POPULATE_WRITE))
    {
        perror("[ARMS] madvise(MADV_POPULATE_WRITE) failed after remote move");
    }
}

static void enqueue_madvise(uint64_t page_base)
{
    if (page_base == 0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(madvise_queue_lock);
        madvise_queue.push_back(page_base);
    }
    madvise_cv.notify_one();
}

// Queue a populate request so scans never block on madvise.
void populate_new_page(uint64_t page_base)
{
    enqueue_madvise(page_base);
}

void *madvise_worker_thread(void *arg)
{
    (void)arg;
    while (!terminated.load(std::memory_order_relaxed))
    {
        std::vector<uint64_t> batch;
        {
            std::unique_lock<std::mutex> lock(madvise_queue_lock);
            madvise_cv.wait(lock, [] { return terminated.load(std::memory_order_relaxed) || !madvise_queue.empty(); });
            if (terminated.load(std::memory_order_relaxed) && madvise_queue.empty())
            {
                break;
            }

            while (!madvise_queue.empty())
            {
                batch.push_back(madvise_queue.front());
                madvise_queue.pop_front();
            }
        }

        for (uint64_t addr : batch)
        {
            populate_with_remote_retry(addr);
        }
    }

    return nullptr;
}
