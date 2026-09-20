# MEMTIS measurement settings

MEMTIS uses shared VM/perf settings and all-DRAM memory preparation, with the
MEMTIS artifact's migration controls. Runner settings take effect on the next
launch and do not reconfigure an already running experiment. The kernel
repairs described below are included in fix12. Changing the adaptive split
switch is a runtime setting and does not require rebuilding or rebooting.

New results identify this configuration as
`memtis_settings_profile=memtis-shared-vm-artifact-migration-remote-preferred-nosplit-v22`. Earlier
results, including split-enabled v21, no-split v20, split-enabled v19, no-split v18, v17, near-preferred v16, remote-preferred v15, and the 6072/6073 experiments, retain their original
settings and must not be treated as runs of the current configuration.

## Shared settings

Both runners use `measurement_apply_common_settings`. MEMTIS selects its
artifact migration behavior: `numa_balancing=0`, and no write to the generic
`demotion_enabled` switch. Only all DRAM applies the final
`measurement_apply_baseline_default_migration_settings` override. All other
shared VM/perf settings remain aligned.

The runner loads `measurement_settings.sh` through `measurement_common.sh`
and applies it before each workload. With `MEMTIS_RUN_SETUP=1`, it reapplies
the shared settings after full setup so setup's VM/perf values cannot replace
the current measurement settings.

| Setting | All DRAM and MEMTIS |
| --- | --- |
| `vm.overcommit_memory` | 1 |
| `vm.watermark_scale_factor` | 10 |
| `vm.watermark_boost_factor` | 10000 |
| `vm.compaction_proactiveness` | 80 |
| `vm.user_reserve_kbytes` / `vm.admin_reserve_kbytes` | 16384 / 16384 |
| `vm.min_free_kbytes` | 1048576 |
| `vm.lowmem_reserve_ratio` | 1 1 1 |
| `vm.vfs_cache_pressure` | 2000 |
| THP `enabled` / `defrag` | always / always |
| THP `shmem_enabled` | force |
| khugepaged `defrag` | 1 |
| khugepaged `pages_to_scan` | 8192 |
| khugepaged `scan_sleep_millisecs` | 0 |
| khugepaged `alloc_sleep_millisecs` | 1 |
| `kernel.perf_event_max_sample_rate` | 1000000 |
| `kernel.perf_cpu_time_max_percent` | 0 |
| `vm.zone_reclaim_mode` | 0 |

| Migration control | All DRAM | MEMTIS |
| --- | --- | --- |
| `kernel.numa_balancing` | 1 | 0 |
| kernel NUMA `demotion_enabled` | false | Inherited; never written by MEMTIS setup/runner |

HTMM promotion and demotion remain enabled independently, with `htmm_mode=2`
and both worker periods set to 500 ms. MEMTIS adaptive huge-page splitting is disabled
with `htmm_thres_split=0`. Leaving generic demotion inherited
matches the artifact; it does not guarantee generic demotion is disabled if
another system previously enabled it. These changes reproduce the artifact's
migration controls, not its full VM/perf or placement configuration.

Perf throttling is temporarily enabled before updating the sample-rate limit,
then disabled, matching the DRAM setup. KSM is disabled. MGLRU is disabled
where its interface exists; the MEMTIS kernel does not provide it. The generic
NUMA controls are separate from MEMTIS's own HTMM migration mechanism, which
remains enabled.

Uncore setup still uses the shared helper: on c220g5 it writes MSR
`0x620=0x707` on CPUs `10-19,30-39` (700 MHz). `CPU_SLOWDOWN_CPUS` applies to
both runners. gsl_optane skips those writes. A failed settings write stops
launch.

## Preparation and placement

The batch driver holds the MEMTIS lock across setup, execution, and teardown.
MEMTIS setup uses the shared setup with the MEMTIS system argument, including
one execution of `defrag.sh`: THP configuration, sync/cache drops, explicit
`vm.compact_memory=1`, then sync/cache drops. The old second MEMTIS preparation
pass is removed. All preparation is outside the timed workload.

Standalone launches with default `MEMTIS_RUN_SETUP=0` also perform this memory
preparation before every workload. They no longer reuse warm file caches by
default. `MEMTIS_RUN_SETUP=1` additionally performs full shared setup before
each workload, including global process migration and IRQ affinity
configuration. CPU affinity is reapplied after setup.

Batch launches pass `MEMTIS_RUN_SETUP=0 MEMTIS_CACHE_FLUSH=batch-setup` to
indicate that preparation already occurred under the lock. The runner
consumes that indication for one workload, avoiding duplicate cache flushing
or compaction. Subsequent workloads in the same invocation receive fresh
preparation. Fast-tier capacity is selected after preparation.

The launch command prefers remote memory by default with
`numactl --preferred=${MEMTIS_SLOW_NODE}`: node 1 on c220g5 and node 2 on
gsl_optane. `MEMTIS_PREFERRED_NODE` overrides the initial placement; set it
to `0` to prefer the fast node. The launcher and its child inherit this
policy, while HTMM can promote hot pages into the fast tier. The fast-tier
limit still applies to `MEMTIS_FAST_NODE` (normally 0). Workload CPUs remain on node
0 (`0-9,20-29` on c220g5). The runner does not alter kernel-worker affinity.
`MEMTIS_INITIAL_PLACEMENT` is unused, and `measurement_memtis_near.sh` remains
an alias of the same runner.

Each workload receives a fresh memory cgroup. Cleanup stops sampling, leaves
the cgroup, disables HTMM, terminates/waits for leftover tasks, and removes
the cgroup. Failure to remove it prevents another launch. Only the memory
controller is required; the runner does not configure cpuset masks.

The runner, stats collector, and output logger stay outside the workload
cgroup. Only the timed launch child (including timeout and the MEMTIS
launcher) enters it, using `BASHPID` so the parent shell is not moved.
This keeps monitoring helpers out of HTMM's allocation and migration paths.
On this kernel, failure to allocate an exec argument page can surface as
`Argument list too long` even for `sleep 1`; that message alone does not
prove the environment is oversized. If the collector's sleep fails, it
reports a warning and stops instead of repeatedly launching commands without
a delay. New results record `memtis_helpers_outside_workload_cgroup=1`.

## MEMTIS-specific behavior

HTMM sampling, promotions/demotions, and the fast-tier limit remain enabled.
The runner resets HTMM controls to local kernel-source defaults, retaining
artifact migration mode `htmm_mode=2`, with `htmm_thres_split=0` to disable
MEMTIS adaptive splitting. THP remains `enabled=always` and `defrag=always`.
This prefers huge-page allocation; it does not guarantee huge-page-only allocation. Linux can still allocate
4 KiB pages or split THPs for other reasons. c220g5
uses CXL emulation on nodes 0/1; gsl_optane uses nodes 0/2. Capacity still
reserves startup headroom and may reduce the effective limit when node-0 free
memory is insufficient; requested and effective capacities are logged.

The launcher source is unchanged. Sampling attachment remains
concurrent with workload startup. The original launcher does not propagate
the child's exit status, so its success alone does not prove workload
completion. Timeout detection remains in the runner.

## September 16 split-queue crash repair

The hanging DuckDB run on `5.15.19-htmm-node0-fix8-workers` used
`htmm_thres_split=1`: the script and live sysfs value disagreed with the
no-split configuration documented at the time. The v18 runner subsequently
wrote `0` to disable MEMTIS adaptive split selection. Partial unmaps and
reclaim could still queue pages for splitting, so the kernel lifetime bug
also required a fix. The v19 runner re-enabled adaptive splitting
with `htmm_thres_split=1`. The v20 runner disabled it again; v21 re-enabled
it and the current v22 runner disables it. The fix10 kernel repairs remain necessary.

The `fix9-split-lifetime` patch pins queued pages under the split-queue lock
before traversing them, uses the current page's LRU owner, and isolates only
pages whose LRU ownership it acquires. It transfers one reference to the
split/putback path and retries busy pages without leaking references. The initial
50-examined-page cap is superseded by fix12's upstream 50-success budget below.
Failed splits no longer inflate the hotness histogram
or decrement migration isolation counters that this scanner never incremented.
The kernel source patch and crash evidence are under
`reproductions/memtis-hang-20260916/`.

`python3 tests/test_memtis_split_scan.py` compiles the actual scanner with
mock page/list operations under ASan and UBSan. It injects concurrent frees
and checks ownership, retries, and reference balance. These source-level tests
do not replace booting the kernel and exercising real THP allocation,
deallocation, splitting, and the previously failing DuckDB workload.

## September 17 metadata crash repair

The fix9 boot exposed additional faults outside its deferred-queue repair:
missing PTE metadata was indexed before checking for NULL, and generic shmem
splitting treated uninitialized tail fields as histogram indices. THP migration
also left metadata fields uninitialized and overwrote an overlapping source
record before copying it. The sampler accessed huge-page metadata without the
PMD lock.

The `fix10-split-metadata` kernel validates optional metadata and histogram
indices, copies complete records before restoring overlapping tail storage,
and locks PMD sampling. The current v22 run configuration disables adaptive
splitting (`htmm_thres_split=0`); generic Linux splitting can still occur.
The patch, crash journal, sanitizer results, build logs and post-boot validation
commands are in `reproductions/memtis-hang-20260917/fix10/README.md`. A reboot
into fix10 is required; source-level tests do not establish runtime stability.

## Metadata and validation

New completed `.time` files record the v22 settings profile and `memtis_htmm_thres_split=0`,
`memtis_numa_balancing=0`, `memtis_generic_demotion_policy=inherited`, fresh cgroup status,
`memtis_memory_policy=preferred`, and `memtis_preferred_node` (normally 1 on
c220g5 or 2 on gsl_optane).
`memtis_cache_flush` is `batch-setup`, `setup`, or `pre-run`, depending on who
performed preparation. Kernel release, CPU placement, uncore configuration,
and effective capacity remain recorded. Existing results are not moved.

`python3 tests/test_memtis_runner.py` verifies shared VM/perf settings,
artifact migration controls throughout setup and launch, HTMM adaptive splitting configuration, perf-write ordering,
identical preparation, failure propagation, preferred-node launch, batch
locking, and fresh-cgroup lifecycle. Tests mock privileged writes and
launches and do not modify host settings or execute real benchmarks.

## September 19: restore the upstream split budget

Kernel `5.15.19-htmm-node0-fix12-split-budget` restores the public scanner's
limit of **50 successful splits per pass**, replacing fix9–fix11's local
50-examined-page limit. Busy/ineligible pages and failed splits do not consume
the success budget. The scanner retains safe snapshot references and releases
all remaining pins after reaching the cap. The fix11 producer ownership repair
remains in place. That kernel repair did not change runner settings; the v22
configuration below subsequently disables adaptive splitting.

See `reproductions/memtis-split-budget-20260919/fix12/` for the exact patch,
regression results, and build/install records.

## September 19: adaptive splitting disabled (v22)

The runner writes `htmm_thres_split=0` before every workload and records that
value in result metadata. This disables MEMTIS adaptive split selection; it
does not disable THP allocation or the generic Linux split paths used by
reclaim, partial unmapping, and other VM operations. No reboot is required.

The MEMTIS-specific values written by `configure_memtis()` are:

| Setting | Runner value |
| --- | --- |
| `htmm_sample_period` | 199 |
| `htmm_inst_sample_period` | 100007 |
| `htmm_thres_hot` | 1 |
| `htmm_split_period` | 2 |
| `htmm_adaptation_period` | 100000 |
| `htmm_cooling_period` | 2000000 |
| `htmm_mode` | 2 |
| `htmm_demotion_period_in_ms` | 500 |
| `htmm_promotion_period_in_ms` | 500 |
| `htmm_gamma` | 4 |
| `ksampled_soft_cpu_quota` | 30 |
| `htmm_thres_split` | 0 |
| `htmm_nowarm` | 0 |
| `ksampled_min_sample_ratio` | 50 |
| `ksampled_max_sample_ratio` | 10 |
| `htmm_util_weight` | 10 |
| `htmm_skip_cooling` | enabled |
| `htmm_thres_cooling_alloc` | 2621440 |
| `htmm_cxl_mode` | enabled on c220g5; disabled on gsl_optane |
