# THP controls below the main enabled switch

These are real controls omitted from the historical setup reset, but no retained May–June change to them was found. The current values match the relevant Linux6.18 defaults. [thp_subcontrols_evidence.json](thp_subcontrols_evidence.json) records current readbacks, the three shell histories, 280 checked editor saves (230 May–June script/notes saves plus50 March setup saves), and selected counters from all28 historical-settings study snapshots.

## Current read-only values

| Control | Value |
| --- | --- |
| Parent `enabled` / `defrag` | `always` / `always` |
| Anonymous 2048 KiB `enabled` | `inherit` |
| Anonymous 16,32,64,128,256,512,1024 KiB `enabled` | All `never` |
| `shrink_underused` | 1 |
| `use_zero_page` | 1 |
| `khugepaged/max_ptes_none` | 511 |
| `khugepaged/max_ptes_swap` | 64 |
| `khugepaged/max_ptes_shared` | 256 |

The current kernel command line has no `thp_anon`, `hugepages`, or `hugepagesz` argument. The explicit hugetlb pool is0 throughout the saved study, as established in [kernel_reserves.md](kernel_reserves.md).

Per-size `inherit` uses the parent control; changing the parent does not reset a size explicitly set to `never` or `always`. `thp_anon` can establish per-size boot policies. `max_ptes_none` controls extra absent/zero pages allowed during collapse, with potential additional memory consumption; swap/shared thresholds constrain their respective collapse candidates. Thus equal parent `enabled=always` does not establish identical THP behavior. [Linux6.18 THP documentation](https://www.kernel.org/doc/html/v6.18/admin-guide/mm/transhuge.html).

## The exact6.18 underused-splitting exception matters here

`mm/huge_memory.c::thp_underused()` immediately returns false when `khugepaged_max_ptes_none == HPAGE_PMD_NR - 1`. That is511 with this system's 2MiB PMD/4KiB base pages. Consequently the current `shrink_underused=1` does not mean that underused-zero splitting is active. Partially mapped folios use a distinct route, and migration-driven splitting is also separate. [Linux6.18 huge_memory.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/mm/huge_memory.c).

The retained VM counters support this distinction: `thp_underused_split_page` is0 in every one of the28 study snapshots. `thp_zero_page_alloc` remains1 and `thp_zero_page_alloc_failed` remains0. These are global before/after observations, not a trace of each workload page, but they rule out attributing the current measured split counts to recorded underused-zero splits during that study.

A historically lower `max_ptes_none` could have different consequences for collapse eligibility and underused splitting on the **same kernel**. No retained valid lower value places that condition in May, and this audit did not change any value or run an experiment.

## What history actually contains

- March-saved shell history lines252–262 inspect `max_ptes_none`, `shrink_underused`, and `use_zero_page`. Line259 is the malformed `sudo cat echo 1 | sudo tee .../use_zero_page`; it does not express a normal write of the literal value1. Line261 reads through `cat`, rather than writing `max_ptes_none`. There are no saved command results proving a changed value.
- March lines1343–1345 inspect anonymous128KiB and2048KiB settings, again without retained output.
- A single March13 20:27:28 UTC setup save, `History/-247d494f/IlY9.sh`, attempts to write **1024** to each of `max_ptes_none`, `max_ptes_swap`, and `max_ptes_shared`. All three Linux6.18 store handlers reject values above511 with `EINVAL`. These lines are an attempted setting edit, not a valid alternate on the kernel being compared. The attempts are absent from the later setup snapshots. [Linux6.18 khugepaged.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/mm/khugepaged.c).
- No match for these controls, `thp_anon`, explicit hugepage boot counts/sizes, or per-size THP settings appears in the later May-saved/current user histories or the230 May–June editor saves.
- The all-refs shell-script search matches only the unrelated Linux5.15.19 initial source import in2022, not a May/June tiering setup change. The audited normal setup/defrag scripts never reset these particular controls.

These controls are worth including in future state capture because a manual or boot override can survive ordinary setup. Current readbacks and retained history, however, do not identify one as the missing May condition. In particular, recommending `shrink_underused=0` as an explanation for the current lowmem split counts would overlook both the511 guard and the zero underused-split counter.
