# Historical runner cleanup and residual memory

Read-only audit on 2026-09-09. No setup, cleanup, module operation, or workload was executed. Scope: May 4 normal measurements, May 7 cost sweep, May 14 long runs, June 14/22 long runs, and the earlier changes that disabled memory-eater setup. Existing complete source snapshots and hashes are in [call_paths](../hugepage_settings_history/call_paths/README.md).

**No retained runner change establishes a hidden May-only memory restriction.** The audit does identify a stronger reason why successful process migration was never proof that node 0 was empty: the `migratepages` executable silently discards positive unmigrated-page counts. Historical readbacks needed to quantify this in May are absent. Today's historical-setup replay instead leaves only about 1–2 MiB of node-0 anonymous pages.

## Dated cleanup sequence

| Cohort or change | Verified behavior |
|---|---|
| March 9, `6b2f5ddf7` | Comments out `insmod memeater.ko`. The earlier headroom calculation remains active temporarily. |
| March 14, `d87369bce` | Comments out the entire headroom calculation and module-directory block. |
| May 4, Git `8d6a8bc66` helper/outer runner | Unsetup → setup → two defrag passes → ARMS; model prepares again for each configuration; teardown after the workload. |
| May 7, 06:09 saved model runner | Same preparation before **each** cost 0.125, 0.25, 0.5, 1.0, and 1.5. Original result chronology supports ARMS preceding this sweep. |
| May 14 saved long runner | Model with its setup → setup → ARMS → teardown. |
| June 13 and June 22 saved long runners | Identical long-runner source except capacity label. Setup/common/unsetup/ARMS/model bodies also unchanged in their corresponding Git state. |

Sources: [May outer runner](../hugepage_settings_history/call_paths/may04_git/run_all_measurements.sh), [May 7 model save](../hugepage_settings_history/call_paths/editor/may07_cost_model.sh), [three long saves and diffs](../long_variant_history.md). The `may04_git` snapshot identifies shared helper contents; this is not a claim that `8d6a8bc66` was already checked out for May 4—the reflog identifies May 4 HEAD as `6b1bc11dd`.

With module insertion disabled, `SIZE_MIB` is a manually supplied output label. The ordinary script does not enforce that amount of free RAM. In the library, the startup “DRAM size” print reads live node-0 **MemFree**, despite its neighboring MemTotal comment. ARMS then recomputes free capacity during policy steps. The C220G5 compile-time assignments are fast node 0 and slow node 1; the GSL Optane node-2 assignment is in a separate platform branch. [Existing capacity audit](../../capacity_policy_audit.md).

## Real cleanup gaps, with limits

**Unsetup does not restore a clean host.** Its May/June contents are only `pkill -f jupyter-notebook`, `pkill python`, and `rmmod memeater.ko`. The common helper ignores its result. It neither restores sysctls, removes `nd_pmem` or other modules, resets cgroups, nor verifies residual processes or node occupancy. `pkill` sends a signal without waiting for termination; there is no subsequent PID/cgroup emptiness check. These are existing limitations shared by both policies, not changes that occurred between the compared May and June runs. The inferred May 7 preparation gaps are consistently approximately 43–44 seconds, so a missing wait is not evidence that ordinary terminating Python processes survived until measurement.

**The `.ko` suffix is not an established unload bug.** Installed libkmod 31 normalizes both `memeater` and `memeater.ko` to module name `memeater`. This was checked through the library's name-construction/get-name functions only, without loading or unloading anything. Upstream rmmod accepts a path when it exists and otherwise constructs a normalized module name. [kmod module-name handling](https://github.com/kmod-project/kmod/blob/v31/libkmod/libkmod-module.c#L313), [rmmod argument handling](https://github.com/kmod-project/kmod/blob/master/tools/rmmod.c#L141). Actual unload failures would be ignored, but no successful May insertion or failed May removal has been recovered.

**The local memeater source has a normal freeing path.** `/users/zimooo2/colloid/tpp/memeater/memeater.c` allocates pages plus a pointer array; initialization failure frees the pages allocated so far, and module exit frees all recorded pages plus that array. It contains no persistent allocation deliberately left behind on successful removal. Its page allocator requests node 0 without `__GFP_THISNODE`, so even an active module would require real node-residency evidence rather than assuming all requested bytes came from node 0. This is the retained local source, not proof of a historical loaded module's bytes.

## Process migration can report success while leaving pages

The setup takes one `ps -e` snapshot, calls `migratepages PID 0 1` for each PID, ignores failure, then applies `taskset -pc` to the main thread. It does not set all existing threads' CPU affinities, bind those processes' future allocations, or constrain their cgroups. New processes and allocations after the snapshot are outside that preparation pass.

More subtly, upstream [`migratepages.c`](https://github.com/numactl/numactl/blob/master/migratepages.c#L90) only treats a **negative** `numa_migrate_pages` result as failure; every nonnegative result becomes shell exit 0. Installed numactl `2.0.18-1ubuntu0.24.04.1` confirms this: `/usr/bin/migratepages` calls the function at offset `0x125c`, branches to error only on a negative result at `0x1263`, otherwise zeros the return register. Linux can return a positive count for folios left unmigrated. [v6.18 migration syscall implementation](https://github.com/torvalds/linux/blob/v6.18/mm/mempolicy.c#L1184). Therefore an apparently successful preparation cannot certify that all other user memory left DRAM. Counts also cannot simply be multiplied by 4 KiB when folio sizes differ.

This is a concrete blind spot, but today's retained measurements constrain its magnitude:

| Historical-setup replay, September 9 | Before ARMS | Before model |
|---|---:|---:|
| Logged `migrate_pages: Invalid argument` lines | 450 | 445 |
| Logged `No such process` lines | 4 | 2 |
| Node-0 MemFree, KiB | 2,534,660 | 2,556,356 |
| Node-0 AnonPages, KiB | 2,316 | 928 |
| Node-0 SUnreclaim, KiB | 185,324 | 184,732 |
| Node-0 Mlocked, KiB | 0 | 0 |

Source: [ARMS preparation log](../../sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_arms/preparation.log), [ARMS before snapshot](../../sessions/20260909T130001Z-142220/results/20260909T130001Z-142220/01-may07_arms/before.json), and corresponding `01-may07_model` files. Kernel threads without a user `mm` can produce EINVAL, and processes can exit between snapshot and migration. These error counts do **not** show gigabytes of stranded user memory. The 1,440 MiB PMEM metadata overhead measured separately is kernel/driver state that this unsetup never releases; no May binding record establishes a dated difference.

## Background jobs and skipped library cleanup

The ordinary runner executes the BC command synchronously. The workload is an executable with OpenMP threads; the library starts pthreads, without a fork/daemon path in the audited code. Normal ARMS/model shutdown takes `_exit(0)` before explicit joins and perf cleanup. This does **not** leave those worker threads running: Linux glibc `_exit` calls `exit_group`, terminating the process's threads. Its address space and descriptor references are released as part of process exit. [glibc implementation](https://github.com/bminor/glibc/blob/master/sysdeps/unix/sysv/linux/_exit.c#L22), [frozen shutdown source](../../../source/may04/arms_kernel.cpp:2037).

There is a separate timeout weakness: `timeout --foreground` signals the monitored command without guaranteeing a kill of all descendants; unsetup only targets Python/Jupyter and has no general descendant cleanup. This could matter for an interrupted shell workload with surviving children. [Coreutils 9.4 source](https://github.com/coreutils/coreutils/blob/v9.4/src/timeout.c#L201). However, the retained original timing dataset contains **147 successful records in the May 3 boot and 144 in the May 7 boot, with no recorded failures/timeouts**. The June 13 and June 22 boots each contain 42 successes and six subsecond `mg.D.x-long` segmentation faults, with no recorded timeout. This weakens an explanation based on a visible failed preceding run leaving a large job behind. It does not prove every unrecorded/manual process was absent. [Original timing dataset](../root_cause_timing/all_original_may_june_times.json).

The current strict-setting failure path and historical relative inner-defrag path are already documented in [call-path audit](../hugepage_settings_history/call_paths/README.md). Neither provides a newly dated May-only restriction. The meaningful remaining evidence gap is historical **effective node/zone memory, module state, and process residency**, not an identified ARMS-versus-model cleanup branch.
