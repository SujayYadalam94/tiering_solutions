# August 27 kernel verification

The specific **4053MiB ARMS and general-model BC Twitter/Kron results** cited in this investigation fall in a **`6.2.0-hybridtier+`** boot. This identification uses original file timestamps and the raw reboot ledger, not the policy directory name. All times here are UTC.

Independent `last` output from `/var/log/wtmp.1`, with `TZ=UTC`:

```text
6.2.0-hybridtier+       Aug 26 13:17:41 -> Aug 27 10:36:48
6.2.0-hybridtier+       Aug 27 10:40:20 -> Aug 27 18:12:32
6.18.1-061801-generic   Aug 27 18:15:00 -> Aug 30 08:19:20
```

The six original BC Twitter files end between **August 27 08:07:42 and 09:45:55**, inside the first listed HybridTier boot. The six BC-Kron files end between **07:42:46 and 09:24:58**, also inside it. Subtracting their few-minute runtimes leaves all starts well after the August 26 boot. All three ARMS/model repetitions therefore have the same inferred boot, with no intervening reboot.

[Raw-record offsets and every file's timestamp/boot assignment](aug27_boot_verification.json) preserve an independent binary parse of the same ledger. The relevant boot record is at byte offset **930048** of `/var/log/wtmp.1` and explicitly contains the complete release string `6.2.0-hybridtier+`.

There **was** a 6.18 boot later on August 27. Consequently, the date alone must not be used to label every August 27 result HybridTier.

Limit: the timing files do not embed their own kernel release. This is a timestamp-to-boot reconstruction. These Twitter result blobs first appear in the September 3 commit `671e0e644`, so Git does not independently prove existence before MEMTIS was cloned on August 31. Their original timestamps and interleaved execution sequence support the August 27 attribution; unlike the March archive's copied rows, they are spread across a coherent run sequence. [Exact Git object search](aug27_git_provenance.json).
