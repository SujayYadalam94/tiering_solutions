# Same-kernel boot layout: what the May–June records establish

**No recovered May–June record shows a different Normal/DMA32 managed split, but the surviving “4 GB” labels cannot establish that the split was identical.** They are manually assigned experiment labels, not `MemTotal`, `/proc/cmdline`, or `/proc/zoneinfo` readbacks. This review holds the kernel version constant and does not invoke a kernel upgrade as the explanation.

Focused search evidence is in [boot_zone_history_evidence.json](boot_zone_history_evidence.json). It reuses the existing script/boot inventories, checks the three retained user histories and 230 May–June script/notes editor saves, and searches all Git refs for the specified boot/onlining/reload commands. No host settings or workloads were changed.

## The labels do not measure memory

May [measurement_arms.sh](../hugepage_settings_history/call_paths/may04_git/measurement_arms.sh) obtains `SIZE_MIB` from a positional argument and places it in filenames. [setup.sh](../hugepage_settings_history/call_paths/may04_git/setup.sh) requires that argument, but its old size-based memeater reservation calculation is entirely commented out. Therefore a run named `4035MiB` neither sets nor records exactly 4035 MiB of physical/available memory.

The [May14](../hugepage_settings_history/call_paths/editor/may14_long.sh), [June13](../hugepage_settings_history/call_paths/editor/june13_long.sh), and [June22](../hugepage_settings_history/call_paths/editor/june22_long.sh) retained long-runner saves differ only in their manually edited size labels, 4035/4040/4041. The earlier [timing inventory](../root_cause_timing/selected_boot_inventory.json) places those cohorts in boots beginning May14 07:30:49, June13 22:10:24, and June22 03:55:37 UTC using wtmp. Wtmp supplies boot/kernel chronology; it does not supply their memory maps or zone sizes.

Even an equal *measured* total would not by itself prove equal physical placement. Here the archived evidence is weaker: only labels survive, with no accepted May/June node0 zoneinfo or kernel command line in the audited logs.

## Focused setting evidence

| Candidate | Recovered evidence | Historical conclusion |
| --- | --- | --- |
| `memmap` location/size/type versus `mem=` | March history line871 contains `memmap=86G\$8G`; line914 prints another reserved-range expression. The March16 GRUB backup has `memmap=90G!2G`. Current boot has `memmap=92G!2G`. Later history repeatedly edits GRUB and reboots without recording edited values. | Boot memory maps were actively adjusted in this project, making map/layout a reasonable condition category. Neither March value identifies May's map. The 90→92 GiB change at the same 2 GiB start changes capacity; it is not evidence of an equal-total relocation between zones. |
| `kernelcore`, `movablecore`, `movable_node` | No matching command in the three histories or 230 May–June saves; no relevant all-refs shell-script change. Current command line has none and node0 Movable has zero managed pages. | No positive evidence these options were used around the transition. |
| `online_kernel`, `online_movable`, `auto_online_blocks` | No retained command or May–June script edit. Current read-only `auto_online_blocks` value is `online`. | No evidence of different historical onlining policy; current value does not reconstruct May. |
| `zone_reclaim_mode`, NUMA balancing | May4/May14/June setup explicitly writes both to0; the normal ARMS/model helper passes system=`default`. May-saved history line559 reads NUMA balancing but saves no answer. | No recovered policy-specific or May–June setting delta in successful batch preparation. |
| Allocator shuffle | Config supports it, but no retained `page_alloc.shuffle` command, boot argument, or editor change. Current sysfs parameter read is permission-denied; no value was inferred from that. | No historical evidence to justify attributing the change to shuffle. |
| Sysctl reload/reset after setup | No `sysctl -p`, `sysctl --system`, or `systemd-sysctl` invocation in these histories or May–June saves. Common default launch path makes no such call. | No recovered event resetting settings between full setup and BC launch. External service state was not recorded historically. |

All-refs search does find older explicit boot-map scripts: 2025 `ff5899009`/`5c828a6d1`/`1143af318` in `scripts/setup.sh` and `a8d9cfabc` in `setup/update_kernel.sh`, including maps such as `80G!8G` plus `80G!104G`, or `34G!4G,80G!104G`. Those paths do not exist in May `8d6a8bc66`, are not called by the audited May runners, and have no matching retained invocation. They should not be promoted to May effective settings merely because an all-refs search finds them.

The March backup and current map details were already established in [boot_capacity_followup.json](../../editor_setup_history/boot_capacity_followup.json) and the [privileged history summary](../hugepage_settings_history/history/privileged_read_summary.json). Later GRUB edit/reboot commands demonstrate activity, not their resulting parameter values. Shell history lacks per-command timestamps and merges sessions, so it cannot securely date a particular unseen edit to the June13 boundary.

## What could change while keeping the same kernel?

On x86, `mem=` limits physical address reach, while explicit `memmap` regions select or reclassify particular ranges. `kernelcore`/`movablecore` partition suitable memory into kernel-usable and Movable zones; `movable_node` selects Movable treatment for hotpluggable nodes. Shuffle changes free-list ordering. These controls can affect layout without replacing the kernel. [Linux6.18 kernel-parameter documentation](https://www.kernel.org/doc/html/v6.18/admin-guide/kernel-parameters.html). Memory onlining also selects a zone; `online_kernel` and `online_movable` have distinct behavior. [Kernel memory-hotplug documentation](https://docs.kernel.org/admin-guide/mm/memory-hotplug.html).

But Normal→Movable repartitioning is **not numerically equivalent to the tested lowmem4 change**. DMA32 protection depends on the sum of its higher zones: moving the same managed pages from Normal to Movable leaves that sum approximately unchanged. It can change Normal's protection against Movable allocations and physical fragmentation, but does not create the extra735.6 MiB DMA32 protection measured when the ratio itself changed from256 to4. See [allocator-reserve audit](kernel_reserves.md).

An equal-total map relocation could still leave more or fewer contiguous large blocks or change which physical pages hold long-lived kernel allocations. That is a plausible mechanism to measure once historical evidence identifies a layout, rather than a recovered May configuration. The present audit supplies no defensible historical alternate map to reboot into.

## Post-setup exceptions already known

The common helper's special DRAM/CXL baseline setup intentionally enables NUMA balancing after generic setup; NOMAD/TPP also have explicit overrides. Normal ARMS/model batches do not select those branches and perform fresh default setup. Standalone ARMS does not prepare itself, so it can inherit a preceding special baseline's settings, while the model runner resets them. This is a real call-path distinction, but no newly found manual May ARMS launch establishes it for the archived cost/long cohorts. Full details and failure limitations are in the [call-path audit](../hugepage_settings_history/call_paths/README.md).

The supported answer is therefore: same kernel and similar filename labels leave boot layout unverified; retained evidence does not demonstrate a different historical managed-zone split or a post-setup reset causing the performance gap.

A subsequent narrow audit also checked per-size THP policy, `thp_anon`, `max_ptes_*`, `shrink_underused`, and `use_zero_page`; see [THP subcontrols](thp_subcontrols.md). These are not reset by the old setup, but no valid May–June override was recovered. Current `max_ptes_none=511` disables the specific underused-zero test in6.18 despite `shrink_underused=1`.
