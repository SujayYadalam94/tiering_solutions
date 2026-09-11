# GRUB versions and the May–June performance transition

Read-only follow-up, September 10, 2026. **No recovered GRUB version establishes the command line used by the slow May versus fast June BC runs.** This pass strengthens the evidence against a kernel, initramfs, GAPBS executable, or ordinary runtime-library replacement, and adds historical Emulab boot-service logs. It does not identify the root cause. PMEM performance/detach results are excluded at the user's request. Recent September edits are kept separate from possible historical causes.

## Which interval is actually bracketed?

The closest same-workload comparison remains nominal 4 GB, 6.18, BC Twitter with 100 trials:

| Cohort | ARMS mean | Model mean | ARMS minus model |
| --- | ---: | ---: | ---: |
| May 14 / 4035 | 808.557 s | 744.401 s | 64.156 s |
| June 13–14 / 4040 | 625.315 s | 614.238 s | 11.077 s |
| June 22–23 / 4041 | 628.544 s | 619.911 s | 8.633 s |

The last slow ARMS ends May 14 09:06:50.839 UTC. The first fast ARMS starts June 13 23:59:08.519 UTC and ends June 14 00:09:29.783. The separate ordinary 10-trial series has a wider May 7–August 27 observation gap. These brackets do not establish that one permanent change explains both series. [Original timing identities and limits](../change_point/README.md).

## Retained GRUB content

The inventory includes 18 source/menu/environment/entry files, with hashes, modification times, inode-change times, and relevant numbered lines. The protected `/etc/default/boot` directory is **empty**. The full EFI `grub.cfg` is only a redirect to `/boot/grub`, not an independent historical menu. The package cache duplicates `grub.ucf-dist`. [Collected versions](boot_versions.json), [read-only collector](collect_boot_versions.py).

| Retained source | Date evidence | Content and historical value |
| --- | --- | --- |
| `/etc/default/grub.dist` | June 2024 mtime | Distribution template, no memory map. Not a May benchmark configuration. |
| `/etc/default/grub.ucf-dist` and UCF cache | May 2025 mtime, identical hashes | Console/network identification, no memory map. Package-managed content is not proof of an effective benchmark boot. |
| `/boot/grub/grub.cfg.preemulab` | February 23 mtime, March 2 ctime | Earlier 6.8 menu without a memory map; carries a different network identifier. Not a May 6.18 menu. |
| `/etc/default/grub.bak` | March 16 22:06:09 UTC mtime and ctime | `memmap=90G!2G` in normal-entry defaults, verbose serial/debug options, and conflicting repeated machine/console assignments. Detailed caution below. |
| Saved GRUB source under `/var/backups/linux-stock-6.2-20260908-124802` | Source mtime September 7, backup ctime September 8 | Empty `GRUB_CMDLINE_LINUX_DEFAULT`; `memmap=90G!2G` in `GRUB_CMDLINE_LINUX`. |
| September 7–8 incident/menu copies | September timestamps | All collected kernel-entry copies use `memmap=90G!2G`. Too late to identify May or June settings. |
| Earlier September 9 diagnostic readback | Contemporaneous saved command line | `memmap=92G!2G`; already documented in the earlier boot-capacity investigation. Recent experiment, not a recovered May setting. |
| Current source/menu/running boot | September 9 source/menu changes; read September 10 | `memmap=90G!2G`, stock 6.2 running. Recorded only to prevent conflating current state with the preceding diagnostic. |

The earlier September 9 `92G!2G` readback is in [boot_capacity_followup.json](../editor_setup_history/boot_capacity_followup.json). Neither the March `90G` nor September `90G`/`92G` records establish May's effective value. Filename labels such as `4035MiB` did not enforce or record capacity; the historical size-based memory-reservation code was commented out. [Earlier zone/launch audit](../arms_version_provenance/may_memory_cleanup/boot_zone_history.md).

### What is suspicious in the March backup?

The saved `GRUB_CMDLINE_LINUX_DEFAULT` contains:

```text
console=ttyS0,115200 earlycon=uart,io,0x3f8,115200 loglevel=7 ignore_loglevel debug rd.shell rd.break memmap=90G!2G
```

This is an actual saved alternative to the later quiet/default command line. The file assigns `GRUB_CMDLINE_LINUX` twice: line 12 contains `emulabcnet=44:a8:42:47:4a:0b`, and line 41 replaces it with `emulabcnet=00:5d:73:ef:d9:ee` and `ttyS0`. It likewise repeats `GRUB_TERMINAL` and `GRUB_SERIAL_COMMAND`. Last assignment wins when sourced. Therefore line 12 alone is **not** its effective `GRUB_CMDLINE_LINUX` value. The map remains in `GRUB_CMDLINE_LINUX_DEFAULT`, which normal entries append; the installed `/usr/sbin/grub-mkconfig:162` sources the file and `/etc/grub.d/10_linux:500` combines the variables. This is saved configuration, not proof of the effective command line during May benchmarks.

**September 10 follow-up correction:** a hardware check during the debug-boot investigation establishes that `eno1`'s current and permanent MAC are **`00:5d:73:ef:d9:ee`**, so the backup's final assignment matches this node. The previous version of this paragraph incorrectly identified the earlier `44:a8:42:47:4a:0b` assignment as this machine's identifier. That earlier value matches no present interface. The correct final hint would restrict CloudLab's discovery to the management interface and provides a concrete mechanism by which the saved debug configuration could avoid today's multi-interface discovery failure. See [debug boot diagnosis](../debug_boot_20260910/README.md). The live `/etc/default/grub.bak` was subsequently overwritten on September 10; use this audit's `boot_versions.json` for the previously captured March metadata and content.

`ignore_loglevel` sends all emitted kernel messages to the console. A slow serial console could add workload-dependent overhead if ARMS causes substantially more kernel output. This setting does **not** cause every migration failure to print a message, and does not enable every compiled-out debug call. No May/June command line or console log establishes that these flags were active, or that ARMS emitted enough output. Thus verbose console handling is a concrete historical setting candidate with weak timing/provenance support, not a diagnosed cause. [Kernel parameter documentation](https://www.kernel.org/doc/html/v6.18/admin-guide/kernel-parameters.html).

Likewise, changing a `memmap` range can change the RAM available to the page allocator and its distribution. But this pass recovers no May-to-June range change, no alternate `kernelcore`/`movablecore` value, and no usable May command line to reconstruct an exact boot. Do not adopt the entire debug backup as a reproduction recipe.

## What shell/editor history adds

The current shell history includes two uniquely identifiable May 30 commits: tiering_solutions `d6f720be6` at 19:26:56 UTC (`.bash_history:40`) and tiering_models `25cd9ed` at 19:27:26 UTC (`:49`). Later in the recorded sequence, lines 68–73 show GRUB edit → update-grub → reboot → long runner; lines 134–148 and 165–175 contain more GRUB edits/long-run/reboot activity. This supports continued boot-configuration editing around the broad period of interest. It does not identify the changed values or securely assign one sequence to June 13: all three histories lack per-command timestamps, and shell-session merges can reorder commands. [Filtered history](shell_history_evidence.json), [commit anchors](commit_anchor_dates.json).

Rechecked all 130 indexed VS Code resources: none is a GRUB, `/etc`, initramfs, or sysctl configuration resource. The precise observation interval contains 47 retained source/script/text saves; none contains a GRUB/boot-argument edit. User/root Vim stores provide no recoverable May/June GRUB contents; root has no persistent undo store, and its history dates to March. Searches of retained Copilot conversation/resource text and global user storage found no target GRUB command-line text. Home filename searches locate only the later September incident backups and unrelated Colloid artifact templates. [Saved-file interval inventory](editor_interval_evidence.json).

This is limited surviving history, not proof that an unsaved or subsequently overwritten change never happened.

## Stronger evidence against changed boot/runtime binaries

Modification timestamps alone can survive copying. This pass also examined inode change times, which ordinary writes and file replacements update:

| Component | Retained inode change-time evidence |
| --- | --- |
| 6.18 kernel image | March 5 19:29:31 UTC |
| 6.18 initramfs | March 5 19:29:53 UTC |
| 6.18 config and all 6,827 ordinary files under its module tree | March 5 |
| HybridTier kernel image and initramfs | March 7 |
| GAPBS `bc` and `pr` executables | March 5 |
| System libc, libgomp, libstdc++ files | February 23 |

These metadata records strongly argue against an ordinary May/June rebuild or installation of the retained files. They do not exclude a different launch path, a different loaded library, boot-time module choices, firmware changes, or restoring an entire filesystem image with preserved metadata. The core-library identity limitations remain those in the existing source audit. [Boot component metadata](boot_versions.json), [executable hashes and metadata](executable_config_metadata.json).

Of 1,038 regular non-symlink files currently under `/etc`, none has an inode change time inside May 14–June 13. `/etc/fstab` retains March metadata. This is additional negative evidence for an ordinary surviving system-config edit, not historical version control: a later edit could overwrite the evidence. Retained dpkg logs contain April then September records and no May/June installation events; absence alone was not treated as proof.

## Newly recovered May/June boot-service logs

Unlike the retained journal and rotated kernel logs, `/var/emulab/logs` contains service logs covering March through September. Nineteen text logs were inspected. They do not contain kernel command lines, free-memory snapshots, or allocator counters. [Filtered raw evidence](emulab_log_evidence.json).

The watchdog's reported control intervals are identical around the May 14 and June 13 boots: check/rusage 1800 seconds, isalive 180 seconds, drift 14400 seconds, and the same remaining controls. There is no recovered watchdog configuration change to explain the performance transition.

There is a real network-cleanup difference in the boot logs. During May 14 and May 30, `eno1` explicitly downs `eno2`; on June 13 it instead explicitly downs `enp94s0f1np1`. Other simultaneous interface helpers remove their own configuration files. The installed helper's lines 77–90 enumerate those files and use them to decide whether to down an interface, while lines 23–26 let other helpers remove files concurrently. Thus “not ours; ignoring” can reflect a file disappearing during cleanup and is not evidence of a user-authored network configuration. The messages alone do not establish final interface state or its memory cost.

Crucially, the boot containing the fast June 22/23 cohort returns to the May cleanup pattern: down `eno2`, with the same high-speed-interface file messages. This prevents attributing both fast cohorts to the June 13 cleanup difference. Boot-service allocation/ordering remains a possible mechanism, but these logs do not expose a persistent new setup condition.

## Updated causal assessment

1. **A changed GRUB memory range remains unverified.** There is real edit/reboot activity, but no old/new May/June values. It remains a plausible cause of changed eligible memory, not a recovered alternate configuration.
2. **Verbose serial-console handling is an actual saved alternative, with weak applicability.** It exists only in the March mixed-identity backup; no May execution evidence supports it. Establishing kernel-output volume would be necessary before treating it as a useful performance explanation.
3. **A kernel/initramfs/GAPBS/runtime update is less likely than before this pass.** The unchanged inode metadata provides stronger backing than filenames or Git alone.
4. **The known June workload sequence and temporary model-adapter edit remain correlations.** No new normal-ARMS core/worker/perf/VM delta appears in the bounded interval. The model edit does not change the isolated normal-ARMS object, and the small gap persists after its restoration. Fresh boots between the user's current runs weaken previous-workload residue as an explanation for those tests; historical batch ordering should not be promoted into a universal cause. [Source and cohort audit](../correlation_deep_dive/README.md).
5. **Storage and conditions created during each boot/load remain open.** Existing history records mount attempts without success/device readbacks. Neither GRUB nor these new boot-service records resolves historical HDD versus SSD. No PMEM results are used in this assessment.

The useful outcome is a more constrained list, not a justified GRUB reversion. No benchmark, reboot, mount, bootloader regeneration, interface change, or production-code edit was performed.
