# Surviving May boot records and GRUB provenance

Checked September 10, 2026. Early May boot-event records survive, including
May 7, but no recovered May kernel command line establishes the effective GRUB
arguments of those boots.

* `/var/log/wtmp.1` records the kernel release and boot times. May 7 boots at
  00:59:49 and 14:07:02 CDT use `6.18.1-061801-generic`. See
  [May boot inventory](may1_to14_wtmp_boots.json), retained from the earlier wtmp
  parser and checked again with `last -x` on the original file.
* `/var/emulab/logs/emulab-networkd-*.log` retains startup records on May 2–14,
  including both May 7 boots. These show `eno1` selected successfully, other
  interface helpers exiting, and cnet metadata generation completing. They do
  not record GRUB variables, `/proc/cmdline`, or the size of the initramfs log.
  [Selected original lines](may1_to14_emulab_records.json).
* The current machine's retained journal boot list starts September 8. The
  nonempty journal directory for an earlier machine ID contains only a brief
  February 23 record; other old machine-ID journal files are zero length.
* Rotated `syslog`/`kern.log` files contain August onward, and retained `dmesg`
  snapshots are September boots. The earliest recovered kernel-command-line
  header in those rotated files is August 18, not May.

The closest pre-May saved GRUB version is the March 16 backup captured by the
earlier [GRUB inventory](../../grub_interval_audit/boot_versions.json).
Its effective assignments, after the later repeated definitions override the
earlier ones, are:

```ini
GRUB_CMDLINE_LINUX_DEFAULT="console=ttyS0,115200 earlycon=uart,io,0x3f8,115200 loglevel=7 ignore_loglevel debug rd.shell rd.break memmap=90G!2G"
GRUB_CMDLINE_LINUX="console=ttyS0,115200 emulabcnet=00:5d:73:ef:d9:ee"
GRUB_TERMINAL=serial
GRUB_SERIAL_COMMAND="serial --unit=0 --port=0x3F8 --speed=115200"
```

[Captured backup metadata and numbered lines](march16_grub_snapshot_metadata.json)
retain the old hash and March modification/change timestamps. The live backup
was overwritten on September 10, so it should not be presented as an unchanged
March file today. The final MAC in the captured backup matches the current
node's permanent `eno1` MAC; this corrects the earlier audit's identification.

The [August 18 command-line record](earliest_retained_kernel_cmdline.txt) instead
contains `console=ttyS1,115200 emulabcnet=44:a8:42:47:4a:0b memmap=86G!2G`,
without `earlycon`, `debug`, or `ignore_loglevel`. This establishes that a boot
without those flags occurred by August 18. It does not establish whether the
May 7 benchmark boot used the March settings or when any change occurred.

The user reports earlier use of the verbose defaults. That is useful historical
context, but the surviving records cannot independently attach it to a specific
May benchmark boot. Successful May network discovery alone also cannot identify
the debug flag or management MAC argument.
