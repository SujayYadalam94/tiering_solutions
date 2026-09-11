# Remaining allocator controls and the MEMTIS installation hypothesis

Read-only host investigation, September 10, 2026. No benchmark or host-setting change was made. The user currently has `lowmem_reserve_ratio=4 4 4` in both `measurement_settings.sh` and the live kernel; that is recorded, not treated as the historical baseline. [Current snapshot, boot-file hashes, and persistent-configuration search](snapshot.json).

## Subsequent user test report

The user reports that neither proactiveness 100 nor the combined proactiveness 20 / khugepaged pages-to-scan 4096 / scan-sleep 1000 ms / allocation-sleep 1000 ms condition restored the missing gap. This supersedes the recommendation below to try proactiveness 100. Do not propose it again as an untested candidate.

The follow-up readback confirms the combined 20/4096/1000/1000 values are currently applied, with `lowmem_reserve_ratio=256 256 32 0 0`. The settings file itself has been restored to the ordinary 80/8192/0/1 tuple. Those are distinct observations: a future normal setup would overwrite manual runtime overrides. Current readback does not establish each completed test's launch state, but there is no basis here to dismiss the user's reported result as a failed write.

The latest ordinary 4079 ARMS and model `.time` files both have exit status 130, so they cannot supply completed paired totals for this report. Record the result as user-reported lack of gap restoration, without inventing timings or claiming a measured zero effect. The earlier controlled September 9 sleep tests did show ARMS speed sensitivity; neither those results nor this report establishes that a changed sleep setting caused the original May transition.

The next useful diagnostic is attribution of migration destination-allocation work under the normal ratio, using ratio 4 only as the existing positive control if needed. Distinguish requested versus successfully moved bytes, destination-allocation failures, split fallback, and time in direct compaction/reclaim. Proactive compaction and khugepaged controls do not disable those migration-triggered paths. Another guessed compaction setting is not a recovered historical condition.

## Assessment

The reserve-ratio experiment establishes sensitivity to destination allocation eligibility, not a historical reserve setting or a literal missing amount of physical RAM. A different allocation/compaction policy can plausibly affect ARMS more strongly without withholding the same 736 MiB. It has not been shown that any control below changed during the original performance transition.

Installing a separate MEMTIS kernel does not change defaults compiled into the existing 6.18 image. A cross-reboot effect needs another route: changed boot arguments, persistent sysctl/sysfs configuration, a startup service, changed drivers/initramfs, firmware, or shared preparation scripts. The current 6.18 image/config retain December 12 metadata; its initramfs retains March 5 metadata. These dates and current hashes are not a historical May binary manifest.

The elevated search found no relevant matches or read errors in existing sysctl, modprobe, modules-load, systemd-system, udev, initramfs-tools, tmpfiles, and rc.local configuration roots listed in the snapshot. This is not a claim to have audited every arbitrary executable startup hook. MEMTIS's artifact runner writes live HTMM, THP, NUMA and perf controls; those writes alone do not survive reboot into another kernel. The normal setup also overwrites its major overlapping VM/THP controls.

The smaller August 27 HybridTier gap predates the August 31 MEMTIS clone and first installation. Therefore that installation cannot initiate the already observed change, although later changes could affect today's exact timings. [Installation chronology](../cross_kernel_transition/memtis_and_disk.md).

### Follow-up: does the MEMTIS runner persist a setting across reboot?

The current runner, committed September runner, September 7 editor save, five distinct incident-directory runner snapshots, shared preparation/teardown, artifact shell helpers, userspace Makefile, and launcher/stopper C sources were checked. No persistent VM/tiering configuration write was identified. The automated action-pattern scan of the five incident snapshots had no matches for system configuration edits, boot regeneration, service enablement, firmware/device persistence commands, or persistent namespace changes; this supplements the source reads rather than proving arbitrary code has no side effects.

- VM/THP/perf and HTMM writes target procfs/sysfs runtime state. Cgroup limits and membership, task affinity, page placement, loaded modules, and physical fragmentation are not retained as that kernel's state after an ordinary reboot into 6.18.
- The userspace Makefile builds only MEMTIS's launchers and sampler stopper under its own `bin/` directory. Those files and benchmark outputs persist on disk; the build does not install a kernel or replace the normal ARMS library.
- The older uncore helper loads `intel-uncore-frequency` and writes runtime socket-1 frequency limits. It does not program BIOS/NVRAM or install a boot-time service. Hardware state is checked separately rather than assuming every possible reset type has identical semantics: the inspected slow-socket `MSR 0x620` currently reads `0x707`, matching normal ARMS setup's explicit per-run write. The fast-socket read was `0xc14`; no historical readback was inferred from it.
- Kernel installation and edits to shared script files do persist, but are separate operations from executing the measurement runner. The existing installation audit documents version-specific boot/module writes and GRUB regeneration.

At this follow-up `vm.extfrag_threshold` reads 500, the previously observed baseline/default. This is not recorded as evidence that a different threshold was tested successfully, nor as proof of what a prior timed execution used. No benchmark, setting, module or firmware change was made by this follow-up.

## Ranked settings

| Control | Current readback | Why it matters | Historical support / limitation |
|---|---|---|---|
| `vm.compaction_proactiveness` | `80` | More aggressive background compaction can consume CPU/memory bandwidth or improve large-block availability. Either outcome can interact disproportionately with ARMS migrations. | **Actual March 13 setup saves request `100`.** May setup requests 80. No May execution at 100 is established. The existing 80-to-20 test was uninformative for the old gap; 100 is not the same experiment. |
| `vm.extfrag_threshold` | `500`, omitted from setup | Governs whether costly-order allocations should attempt compaction based on fragmentation index. A lower threshold admits more attempts; a higher threshold skips more. Costs can shift between compaction and failed large-page allocation/splitting. | Mechanistic candidate only. No retained write or persistent override found. Do not infer runtime direction or an old value from the current timing gap. |
| `vm.defrag_mode` | `0`, omitted from setup | At 1 the allocator preserves the no-fragmentation constraint across more fallback paths, changing placement and allocation recovery. It is intended to preserve higher-order blocks, preferably from early boot. | Mechanistic candidate only, not evidence of a changed default. It is absent from the inspected local 6.2 and MEMTIS page allocator sources, weakening a single explanation spanning those kernels. |
| `max_ptes_none` and `shrink_underused` | `511` and `1`, omitted from setup | Collapse eligibility and zero-content underused splitting can change huge-page coverage and allocation traffic. | **At 511, the kernel's underused test immediately returns false.** Current boot-total `thp_underused_split_page=0`. Simply toggling `shrink_underused` is therefore a weak next experiment. Lowering `max_ptes_none` changes both mechanisms, not a pure memory reserve. |
| `vm.percpu_pagelist_high_fraction` | `0`, omitted from setup | Changes per-CPU free-page caching, potentially affecting drain/coalescing work and allocation locality. | Lower priority: no historical override. Previously observed node0 PCP counts were roughly 10–88 MiB, not a demonstrated hidden 750 MiB reserve. |

Exact implementation: [6.18 compaction threshold and proactiveness](https://github.com/torvalds/linux/blob/v6.18/mm/compaction.c), [allocator fragmentation mode](https://github.com/torvalds/linux/blob/v6.18/mm/page_alloc.c), [underused detection](https://github.com/torvalds/linux/blob/v6.18/mm/huge_memory.c). The migration destination allocator uses `GFP_TRANSHUGE` with the requested node; its direct-compaction path is not simply disabled by changing the top-level THP fault `defrag` control. [Migration allocation](https://github.com/torvalds/linux/blob/v6.18/mm/migrate.c).

The March 13 `20:27:28.171 UTC` [IlY9.sh save](../editor_history/setup.sh/IlY9.sh:57) requests proactiveness 100, scan/allocation sleeps 10/10 ms, and pages-to-scan 4096. Several subsequent saves retain 100. It also briefly requests `max_ptes_none/swap/shared=1024`, which 6.18 rejects because the maximum is 511; those lines are removed four minutes later. That attempted write must not be promoted into an accepted historical setting. March shell history contains reads of these THP controls but no successful alternative value. [Earlier history and execution audit](../arms_version_provenance/hugepage_settings_history/README.md).

## Lower-priority or excluded directions

- Per-size anonymous THP controls currently match defaults: 2 MiB inherits top-level `always`, smaller sizes are `never`. Setup does not reset those per-size controls, but no hidden override was observed.
- Explicit hugetlb pools and CMA are zero. `compact_unevictable_allowed=1` is the ordinary non-RT default; no different historical value was found.
- `CONFIG_MEM_ALLOC_PROFILING`, `CONFIG_PAGE_OWNER`, `CONFIG_PAGE_EXTENSION`, and `CONFIG_DEBUG_PAGEALLOC` are disabled in this 6.18 config. They cannot explain an unnoticed runtime change in metadata reservations on this image.
- Allocation zeroing is enabled and free-time zeroing disabled, confirmed by this boot's `mem auto-init` message. No retained shell/GRUB evidence of `init_on_free`, page poisoning, or allocator-debug overrides was found. Merely adding generic `debug` does not select those modes.
- The already tested boost 0, min-free 16 MiB, khugepaged batch 4096, and proactiveness 20 did not recover the old separation. Longer khugepaged sleeps made ARMS faster; historical short sleeps are already restored. [Completed tests](../historical_settings_tests/results.md), [sleep sensitivity](../khugepaged_tests/results.md).

## Previous proposed comparison — superseded by the user report above

Use the normal historical reserve ratio `256 256 32` for both policies and compare proactiveness 80 versus the actually saved 100, leaving other controls fixed. This would test a historical candidate, not demonstrate that May used it. Record Read Time, average iteration, prelaunch per-zone watermarks/buddy state, migration syscall time, and interval VM counters. A useful result must preserve Manta near its original absolute runtime while increasing ARMS's computation cost; matching only the gap is insufficient.

`extfrag_threshold` and `defrag_mode` are additional mechanism probes if the scope later expands beyond settings supported by retained history. Neither is an identified historical correction. No tests were launched by this investigation.
