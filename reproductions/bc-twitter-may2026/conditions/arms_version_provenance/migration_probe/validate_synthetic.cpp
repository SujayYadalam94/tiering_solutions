// Standalone diagnostic-support validation. No ARMS initializer, real
// move_pages, perf event, workload, or host-setting access is linked or called.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>
#include <unistd.h>

#define FAST_TIER 0
#define MPOL_MF_MOVE_ALL 4
struct SyntheticPage { uint64_t va; };
using page_ptr = std::shared_ptr<SyntheticPage>;
static int mock_return = 0;
static int mock_result_errno = -1;
static int expected_incoming_errno = 0;
static std::vector<int> mock_status;
static int mock_calls = 0;

static int numa_move_pages(int pid, unsigned long count, void **,
                           const int *, int *status, int flags)
{
    assert(pid == 0 && flags == MPOL_MF_MOVE_ALL);
    assert(errno == expected_incoming_errno);
    assert(count == mock_status.size());
    ++mock_calls;
    for (size_t i = 0; i < count; ++i)
        status[i] = mock_status[i];
    if (mock_result_errno >= 0)
        errno = mock_result_errno;
    return mock_return;
}

#include "instrumentation.inc"

static uint64_t value(unsigned lane, ProbeCounter counter)
{
    return probe_lanes[lane].values[counter].load(std::memory_order_relaxed);
}

int main()
{
    std::vector<page_ptr> pages;
    for (uint64_t va : {0x200000ULL, 0x400000ULL, 0x600000ULL})
        pages.push_back(std::make_shared<SyntheticPage>(SyntheticPage{va}));
    std::vector<void *> vas{reinterpret_cast<void *>(pages[1]->va),
                            reinterpret_cast<void *>(pages[2]->va)};
    std::vector<int> nodes(2, 0), status(2, -100);

    errno = ENOMEM;
    probe_note_huge_batch(3, 1, 0);
    assert(errno == ENOMEM && value(0, mixed_filtered_batches) == 1);
    expected_incoming_errno = ENOMEM;
    mock_return = 1;
    mock_status = {0, -EBUSY};
    assert(probe_move_pages(vas, nodes, status, 0, false, &pages) == 1);
    assert(errno == ENOMEM && status == mock_status);
    assert(value(0, positive_errno_enomem_calls) == 1);
    assert(value(0, positive_errno_enomem_success_statuses) == 1);
    assert(value(0, early_enomem_valid_success_statuses) == 1);
    assert(value(0, wrong_index_opportunity_slots) == 2);
    assert(value(0, wrong_index_loop_reached_slots) == 0);

    mock_return = 0;
    mock_status = {0, 0};
    assert(probe_move_pages(vas, nodes, status, 0, false, &pages) == 0);
    assert(errno == ENOMEM && status == mock_status);
    assert(value(0, wrong_index_loop_reached_slots) == 2);
    assert(value(0, wrong_index_valid_success_slots) == 2);

    mock_return = -1;
    mock_result_errno = EPERM;
    mock_status = {-100, -100};
    assert(probe_move_pages(vas, nodes, status, 0, false, &pages) == -1);
    assert(errno == EPERM);
    assert(value(0, errno_eperm_calls) == 1);
    assert(value(0, valid_status_entries) == 4);
    assert(value(0, status_untouched_sentinel) == 0);

    expected_incoming_errno = EPERM;
    mock_result_errno = ENOMEM;
    assert(probe_move_pages(vas, nodes, status, 0, false, &pages) == -1);
    assert(errno == ENOMEM && value(0, errno_enomem_calls) == 1);
    assert(value(0, early_enomem_branch_calls) == 2);

    expected_incoming_errno = ENOMEM;
    mock_result_errno = -1;
    mock_return = 1;
    mock_status = {0, -ENOMEM};
    assert(probe_move_pages(vas, nodes, status, 0, true, nullptr) == 1);
    assert(errno == ENOMEM && value(1, positive_errno_enomem_success_statuses) == 1);
    assert(value(1, wrong_index_opportunity_slots) == 0);

    std::vector<void *> empty_vas;
    std::vector<int> empty_nodes, empty_status;
    mock_return = 0;
    mock_status = {};
    assert(probe_move_pages(empty_vas, empty_nodes, empty_status, 1, true, nullptr) == 0);
    assert(value(3, started_calls) == 1 && value(3, requested_addresses) == 0);
    assert(errno == ENOMEM);

    probe_note_huge_batch(0, 0, 1);
    probe_note_huge_batch(3, 3, 1);
    probe_note_huge_batch(3, 0, 1);
    assert(value(2, empty_batches) == 1 && value(2, all_filtered_batches) == 1);
    assert(value(2, unfiltered_nonempty_batches) == 1);
    assert(errno == ENOMEM);
    assert(mock_calls == 6);
    assert(value(0, started_calls) == 4 && value(0, completed_calls) == 4);
    for (unsigned lane = 0; lane < 4; ++lane)
        assert(value(lane, inflight_calls) == 0);
    migration_probe_report();
    assert(errno == ENOMEM);
    migration_probe_report(); // Must not print a second report.
    assert(errno == ENOMEM);
    std::puts("synthetic validation passed; real move_pages was never called");
}
