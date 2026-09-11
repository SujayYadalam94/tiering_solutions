# Settings follow-up after the cgroup/module audit

September 10, 2026. Investigation only: no benchmark, cache flush, settings write,
service change, driver reload, or process-affinity change was performed. Read-only
collectors can cause transient CPU and cache activity. Recent deliberate user
experiments are not attributed to the original May performance transition.

## Result

No new dated settings change explains the missing May ARMS/Manta gap. The current
settings and additional historical checks narrow several possibilities, but do
not establish May's effective readbacks. In particular, matching the text of a
setup script does not prove that every old write or process migration succeeded.

The most useful new checks are:

* Current large editor/service processes have their mapped resident pages on
  node 1, not node 0. Their several GiB of aggregate RSS is therefore not evidence
  of a comparable loss of fast memory. Node 0 currently has about 2,683 MiB free,
  zero AnonPages and 6.1 MiB FilePages. This is an investigation-time observation,
  not a historical prelaunch measurement.
* Persistent VM, service, user-session and device-tuning configuration inventories
  have no recovered post-May change other than the already investigated current
  GRUB files. No new memory cap, swap/zram, oomd, numad, tuned or irqbalance service
  was found. See [host evidence and limits](host/README.md).
* Intel microcode and firmware packages were last updated February 23, before
  May. The current microcode is `0x2007006`; BIOS reports
  `C220M5.4.2.3c.0.0129230853`, dated January 29, 2023. Neither a BIOS release date
  nor absence of fwupd history proves when firmware was installed or its settings.
* Fast-socket CPU frequency and uncore are not locked by normal setup. Current
  intel_pstate is active with `powersave`, EPP `balance_performance`, CPU limits
  800–3000 MHz and turbo permitted. Fast uncore allows 1200–2000 MHz; slow uncore
  is fixed at 700 MHz by the existing setup write. No historical override or
  May per-run frequency record was recovered.
* Some storage interrupts remain on fast CPUs despite setup's attempt to move
  all IRQs. The readback records mpt3sas vectors 20–39 on CPUs 0–9,20–29. This is
  a limitation of assuming setup fully isolates those CPUs, not evidence of a
  newly changed IRQ policy. No affinity write was attempted by this audit.

The final cgroup sweep includes competing services, not only the benchmark's
ancestors: all 61 host groups were traversed, 59 expose memory controls, and every
memory.low/min is zero and memory.high/max unlimited, with no read errors.
[Complete protection check](host/cgroup_protection.json).

The storage follow-up also compares 75 retained controller startups from August
18 through September 10. All report 40 MSI-X vectors and the same queue sizes and
firmware, including both August 27 boots. They do not record CPU affinity and do
not reach May. [Controller chronology](storage/mpt3sas_summary.json).

[Host CPU-register, kernel-thread and IRQ snapshot](cpu_irq_snapshot.json),
[inherited CPU/VM settings and firmware package chronology](inherited_settings.json).

## Actual source differences and why they do not identify the cause

| Difference | Evidence and implication |
| --- | --- |
| Model history mode/cost defaults changed May 10 | Real post-May-7 model selection change; already restored in historical-model reconstructions. Cannot directly explain faster ARMS. |
| Current perf setting write order is more reliable | Older setup could inherit a lower rate after a rejected write. The existing 100000-rate test gave ARMS 104.95 s / model 99.62 s, still below archived ARMS 115.804 s. No accepted May rate is recovered. |
| Current setup checks more write failures and resolves inner defrag by absolute path | Could change preparation after an error or launch from a different working directory. No such failure/CWD is established for the historical target. |
| vfs_cache_pressure=2000 moved earlier in preparation | Same final value, and no matching change between the slow May and faster June source states. Full historical-setup reconstructions already failed to restore the old gap. |
| September MEMTIS-specific setup changes | Explicitly gated on the MEMTIS system argument; ordinary ARMS/model use default. They also postdate the faster June and August 27 cohorts. |
| Graph files were replaced/copied | Current SSD copies have September creation metadata. Original graph metadata has matching sizes but no old hashes or block layout. This is real provenance uncertainty, not a discovered scheduler/readahead change or evidence of per-policy graph switching. |

The core May/June setup and launch files checked are identical, and all 23 common
requested VM/THP/perf/MGLRU settings match. Current normal setup uses the same
main-thread `taskset -pc` behavior as May. It does not select the later all-NUMA
exception, add `--all-tasks`, or place the workload in a MEMTIS cgroup.

[Script comparisons, exact commits and saved-file inventory](scripts/README.md),
[storage configuration, history and graph provenance](storage/README.md).

## Interpreting the remaining inherited settings

`hwp_dynamic_boost=1` initially looked like a possible override. The current ACPI
power-management profile is 4 (enterprise server), and the 6.18 driver enables
that boost by default for server profiles. It is not evidence of a newly installed
tuning service. With HWP active, the `powersave` name also does not mean a fixed
800 MHz clock. [Linux intel_pstate documentation](https://www.kernel.org/doc/html/v6.18/admin-guide/pm/intel_pstate.html),
[6.18 initialization source](https://github.com/torvalds/linux/blob/v6.18/drivers/cpufreq/intel_pstate.c).

The sampled prefetch-control register `0x1a4` is zero on CPUs 0 and 10, and uncore
register `0x620` reads `0xc14` / `0x707` respectively. These are current register
values, not recovered May values. The histories do not provide a post-May BIOS,
prefetch, turbo, governor, fast-uncore, or CPU-energy-preference transition.

Unreset VM controls currently include dirty ratios 20/10 with zero byte overrides,
vfs_cache_pressure_denom 100, percpu_pagelist_high_fraction 0, extfrag_threshold
500 and defrag_mode 0. No historical write explaining a change in those controls
was found. Recommending arbitrary alternative values would be a sensitivity test,
not a reconstruction of demonstrated May conditions.

The remaining historical uncertainty concerns successful preparation, effective
frequency/IRQ behavior, and the documented predecessor-workload sequence and
resulting page layout. No extra 400–750 MiB reservation or new post-May ARMS
configuration was identified here. The previously tested compaction/khugepaged,
reserve-ratio, perf and source alternatives must not be presented as new leads.
