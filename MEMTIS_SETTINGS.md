# MEMTIS measurement settings

MEMTIS uses shared VM/perf settings and all-DRAM memory preparation, with the
MEMTIS artifact's migration controls. No optional profile or kernel rebuild is required. Changes take
effect on the next launch and do not reconfigure an already running experiment.

New results identify this configuration as
`memtis_settings_profile=memtis-shared-vm-artifact-migration-preferred-v14`. Earlier
results, including v13 and the 6072/6073 experiments, retain their original
settings and must not be treated as runs of the current configuration.

## Shared settings

Both runners use `measurement_apply_common_settings`. MEMTIS selects its
artifact migration behavior: `numa_balancing=0`, and no write to the generic
`demotion_enabled` switch. Only all DRAM applies the final
`measurement_apply_baseline_default_migration_settings` override. All other
shared VM/perf settings remain aligned.

| Setting | All DRAM and MEMTIS |
| --- | --- |
| `vm.overcommit_memory` | 1 |
| `vm.watermark_scale_factor` | 10 |
| `vm.watermark_boost_factor` | 10000 |
| `vm.compaction_proactiveness` | 80 |
| `vm.user_reserve_kbytes` / `vm.admin_reserve_kbytes` | 16384 / 16384 |
| `vm.min_free_kbytes` | 1048576 |
| `vm.lowmem_reserve_ratio` | 256 256 32 |
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
and both worker periods set to 500 ms. Leaving generic demotion inherited
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

The launch command uses `numactl --preferred=0` by default, using the selected
`MEMTIS_FAST_NODE` when overridden. The launcher and its child inherit this
explicit policy. Remote memory remains available when the fast tier fills;
all DRAM continues to use strict node-0 binding. Workload CPUs remain on node
0 (`0-9,20-29` on c220g5). The runner does not alter kernel-worker affinity.
`MEMTIS_INITIAL_PLACEMENT` is unused, and `measurement_memtis_near.sh` remains
an alias of the same runner.

Each workload receives a fresh memory cgroup. Cleanup stops sampling, leaves
the cgroup, disables HTMM, terminates/waits for leftover tasks, and removes
the cgroup. Failure to remove it prevents another launch. Only the memory
controller is required; the runner does not configure cpuset masks.

## MEMTIS-specific behavior

HTMM sampling, promotions/demotions, and the fast-tier limit remain enabled.
The runner resets HTMM controls to local kernel-source defaults, retaining
the existing artifact choices `htmm_mode=2` and `htmm_thres_split=1`. c220g5
uses CXL emulation on nodes 0/1; gsl_optane uses nodes 0/2. Capacity still
reserves startup headroom and may reduce the effective limit when node-0 free
memory is insufficient; requested and effective capacities are logged.

The launcher source and kernel are unchanged. Sampling attachment remains
concurrent with workload startup. The original launcher does not propagate
the child's exit status, so its success alone does not prove workload
completion. Timeout detection remains in the runner.

## Metadata and validation

Completed `.time` files record the v14 settings profile,
`memtis_numa_balancing=0`, `memtis_generic_demotion_policy=inherited`, fresh cgroup status,
`memtis_memory_policy=preferred`, and `memtis_preferred_node` (normally 0).
`memtis_cache_flush` is `batch-setup`, `setup`, or `pre-run`, depending on who
performed preparation. Kernel release, CPU placement, uncore configuration,
and effective capacity remain recorded. Existing results are not moved.

`python3 tests/test_memtis_runner.py` verifies shared VM/perf settings,
artifact migration controls throughout setup and launch, unchanged HTMM controls, perf-write ordering,
identical preparation, failure propagation, preferred-node launch, batch
locking, and fresh-cgroup lifecycle. Tests mock privileged writes and
launches and do not modify host settings or execute real benchmarks.
