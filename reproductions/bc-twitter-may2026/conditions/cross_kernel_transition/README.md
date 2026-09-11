# Cross-kernel chronology and MEMTIS follow-up

Read-only investigation on September 9, 2026. Dates are UTC. No benchmark, build, graph copy, cache flush, TRIM, or host-setting change was performed in this follow-up.

The newer HybridTier result confirms that changing back to that kernel does not recover the old ARMS/model gap. **No recovered change yet establishes the cause of the May 7 slowdown.** This pass identifies a substantial earlier sampling change relevant to the archived 6.2 ARMS results and dates the small HybridTier gap before MEMTIS was installed.

## Current and pre-MEMTIS HybridTier results

| BC Twitter cohort | ARMS, seconds | Model, seconds | Difference |
|---|---:|---:|---:|
| August 27, 4053, three runs | 129.837 | 126.418 | 3.420 |
| September 9, 4078 run 1 | 134.981 | 130.877 | 4.104 |

Both cohorts fall in `6.2.0-hybridtier+` boots in the retained boot ledger. August uses the **general `all` forest**; September uses the **BC Twitter forest**. This is not a controlled comparison of identical models. Nevertheless, ARMS itself was already this fast before MEMTIS, and a small gap already existed with the general model.

The user's follow-up prompted an independent raw-ledger check: all twelve August 27 BC Twitter/Kron files fall in the HybridTier boot that began **August 26 13:17:41** and ended **August 27 10:36:48**. A later August 27 boot used 6.18, so the entire date must not be labeled HybridTier. [Verification and limits](aug27_boot_verification.md).

The September ARMS result ended at **21:25:07.637**, model at **21:28:35.910**. Both record `exit_status=0` and `timed_out=0`. Their exact contents, hashes, and nanosecond mtimes are preserved in [the snapshot](latest_hybridtier_and_august_20260909T213825Z.json), together with the August runs. The files named 4078 are being reused: the earlier September 6.18 BC-Kron ARMS result was subsequently replaced by a **0.412-second incomplete result**. That replacement must not be used as a successful HybridTier BC-Kron run. The earlier valid pair remains preserved in [the BC-Kron audit](../post_may7_history/bc_kron_4078.md).

## Archived 6.2 ARMS and model results come from different code eras

All six archived 4 GB ARMS BC-Kron/Twitter files match exact Git blobs already present in **`fedcde1ee`, March 15 at 23:05:27**, under `times/arms/`. They are now under `times/c220g5/arms-6.2/` and copied into final. This proves existence by March 15; it is not a per-run build manifest or exact execution timestamp. [Direct tree/blob evidence](march15_archive_origin.json).

Their original-directory mtimes cluster at **April 11 19:42:22**, during a 6.18 boot. They are copies even though they are under `c220g5`; neither those mtimes nor the later final-copy mtimes identify their execution boot. March's boot ledger is consistent with HybridTier being used then. **Automatic mtime-to-boot assignments in `originals.json` and `four_gb_summary.json` are not execution evidence for these copied rows.**

The selected 4030 model results are original May 11 files, in the **20:58:36 HybridTier boot**, ending between 21:36 and 23:40. [Six model rows and hashes](may11_6_2_models.json).

| Workload | Archived ARMS, already present March 15 | Model measured May 11 |
|---|---:|---:|
| BC Twitter, mean of three | 157.481 s | 135.314 s |
| BC Kron, mean of three | 335.475 s | 271.632 s |

These are not contemporaneous ARMS/model pairs. They cannot establish that one post-May change explains every archived 6.2/6.18 gap.

## Concrete earlier ARMS changes

The March 15 tree opens three events on each of 20 C220G5 CPUs: local L3-miss loads, remote L3-miss loads, and stores. `PERF_PAGES=1+(1<<12)` gives **16 MiB of data per ring**, excluding its metadata page. May ARMS opens two events on the same 20 CPUs with `1+(1<<11)`, or **8 MiB per ring**.

| Setting | March 15 tree | May 7 tree |
|---|---|---|
| Events per sampled CPU | 3 | 2 |
| Total perf ring data | **960 MiB** | **320 MiB** |
| Migration backoff | 4 policy intervals | 0 |
| Read events | local and remote L3 misses separately | combined L3 misses, `0x20d1` |

The **640 MiB difference in allocated ring data** is a real capacity/overhead candidate for the old March measurements. It is not proof of 640 MiB less usable node0 memory: kernel allocations can fall back to another node. The local HybridTier kernel's `kernel/events/ring_buffer.c:787` allocates regular perf pages with `alloc_pages_node(cpu_to_node(cpu), GFP_KERNEL | __GFP_ZERO, 0)`, so placement initially targets the sampled CPU's node.

Git records the intervening changes:

- **March 16, `ec524f599`:** ring size falls from a brief `1<<15` setting to `1<<10`. The same commit changes fragmented-page failure accounting and returns a failure count instead of a Boolean.
- **April 9, `d92cb822f`:** three PEBS events become two; the read event temporarily samples all loads.
- **April 11, `e405eb61a`:** read sampling switches to combined L3 misses (`0x20d1`).
- **May 2, `058f63258`:** Git records zero ARMS backoff and the ARMS-specific `1<<11` ring size. These were already present in the surviving **April 30 binary**, so the commit date is not the first possible use.

[Saved source, commit metadata, and patches](historical_source/) make these differences reviewable. The [April binary audit](../legacy_binary/README.md) independently establishes the May-like event count, ring size, and zero backoff before the May 7 target. Consequently, these March changes are credible candidates for the older 6.2 archive, **not an explanation of a subsequent May 7-to-current transition**.

## Remaining boundary

For the separate BC Twitter **100-trial** series, ARMS remains slow on May 14 and is faster by June 14/22. Nearest saved ARMS source, headers, worker, build rules, and ordinary setup agree across that interval. June's temporary model adapter change cannot directly speed up normal ARMS and was restored before the second fast June cohort. [Source audit](../arms_version_provenance/root_cause_code/README.md).

This is evidence against one September-only source/install change explaining the whole history. It does not prove the absence of an unrecorded host/storage condition or a different loaded binary. The original May 7 cohort still lacks a per-run binary hash, graph hash/device record, read/trial split, and prelaunch physical-memory state. The documented predecessor sequence remains an actual historical condition that has not yet been replayed, although BC-Kron's result weakens a Twitter-specific ordering explanation.

The user's disk-cleanup hypothesis is evaluated separately in [the MEMTIS and storage follow-up](memtis_and_disk.md).
