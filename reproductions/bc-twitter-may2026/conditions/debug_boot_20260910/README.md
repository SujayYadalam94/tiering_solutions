# Why bare `debug` breaks this CloudLab node's management network

Investigated 2026-09-10. The failed boots reached Linux userspace. CloudLab's
network-discovery script then selected multiple control interfaces and brought
down the actual management interface, `eno1`, losing its DHCP lease.

The specific trigger is a text-parsing bug: the script searches all of
`networkctl status` for `configured`, including its recent journal messages.
Bare `debug` enables systemd debug logging, and those messages include
`IPv6LL is not configured yet.` even for an interface whose setup state is
still `configuring`.

## Matched boots

All three use `6.18.1-061801-generic` and the same recorded command line apart
from the trailing bare `debug` token. Times in boot labels are September 10 CDT
(UTC−05:00); event offsets are monotonic seconds since boot.

| Boot label | Bare debug | Control-interface claims | Management network |
| --- | --- | --- | --- |
| 07:41 | yes | All four interfaces at +24.328–24.336 s | `eno1` down +24.757 s; DHCP lease lost +24.758 s |
| 07:49 | yes | All four interfaces at +25.748–25.752 s | `eno1` down +26.184 s; DHCP lease lost +26.184 s |
| 07:58 | no | Only `eno1`, +19.142 s | Metadata written successfully; current SSH connection works |

Full boot IDs, original journal hashes and export sizes are in
[journal_manifest.json](journal_manifest.json). The concise event extracts are
[07:41](debug_0741.timeline.txt), [07:49](debug_0749.timeline.txt), and
[07:58](working_0758.timeline.txt).

## Causal chain in the installed scripts

1. [`emulab-networkd.sh`](host_snapshot/emulab-networkd.sh), line 37, uses:

   ```sh
   networkctl status "$iface" | grep -qi configured
   ```

2. Systemd v255's common logging parser recognizes bare `debug`, including for
   its component programs; it is not solely a kernel printk setting. See
   [upstream log.c](https://github.com/systemd/systemd/blob/v255/src/basic/log.c#L1216).
   The installed systemd reports version `255.4-1ubuntu8.12`.

3. `networkctl status` appends up to ten journal entries by default. Its
   interface filter includes `INTERFACE`, `DEVICE`, and `_KERNEL_DEVICE`.
   `--lines=0` suppresses these entries. See
   [upstream networkctl.c](https://github.com/systemd/systemd/blob/v255/src/network/networkctl.c#L1582).

4. The failed boots contain priority-7 debug messages matching `configured` for
   all three interfaces that had not finished configuring. In both boots,
   `eno2`'s last interface log message before the first control-interface claim
   is `link_check_ready(): IPv6LL is not configured yet.` Its last setup-state
   transition is `initialized -> configuring`.

5. Each helper writes its interface name into `/run/cnet` without locking.
   Lines 75–90 then read that shared file, remove other generated network files,
   and run `ip link set ... down`. Multiple erroneous winners overwrite the
   shared selection and disable interfaces, including `eno1`.

6. Per-interface CloudLab logs explicitly record `downed eno1` and subsequent
   failure because no DHCP lease file remains. See the saved
   [eno2 helper excerpt](emulab-networkd-eno2.log.excerpt.txt), corroborated by
   the other three interface logs. Without `debug`, the non-management helpers
   exit and `eno1` completes metadata generation normally.

## Verification and its limit

Reconstructed each interface's final ten journal entries immediately before the
first management-interface claim, using the upstream networkctl filter and
the failed boot's structured journal records. Running the installed script's
literal `grep -qi configured` on those journal tails alone returns success for
all four interfaces, in both failed boots. Only `eno1` had actually reached
the `configured` setup state.

Results: [07:41 reconstruction](debug_0741.predicate_reconstruction.txt) and
[07:49 reconstruction](debug_0749.predicate_reconstruction.txt). The underlying
selected structured events are saved alongside them as `.interface_events.jsonl`.
These are reconstructions, not recordings of each helper's networkctl stdout.
No additional failing boot was induced. The failure sequence, source code,
debug messages, and matching successful boot give strong evidence for this
mechanism.

## A contributing GRUB error

The recorded kernel argument is `emulabcnet=44:a8:42:47:4a:0b`, which matches
none of this node's four interfaces. `eno1`'s current and permanent hardware
address are both `00:5d:73:ef:d9:ee` (`ethtool -P eno1`). The current control
interface recorded by CloudLab is `eno1`.

The installed [udev helper](host_snapshot/emulab-networkd-udev-helper.sh),
lines 17–37, excludes other interfaces only when the hinted MAC exists on the
node. Since it does not, all four interfaces participate in discovery. A
correct hint can restrict discovery when the matching interface is already
present. The subsequent [08:46 reproduction](march_repro_failed_0846/README.md)
shows why this was insufficient: the two other NICs ran discovery before the
management interface appeared. The MAC alone cannot prevent this failure.
This does not establish when the stale hint originated or a May benchmark effect.

## Shutdown and console observations

The 07:41 boot records `Power key pressed short.` at +281.422 s; the 07:49 boot
records it at +39.296 s. Both then perform an orderly poweroff. These logs
do not identify who or what generated the power-button events, and they do
not demonstrate a kernel panic or a watchdog-triggered reset. The repeated
power transitions therefore remain distinct from the identified network failure.

GRUB uses serial unit 0, while Linux's `console=ttyS1,115200` uses serial unit 1.
The actual UART ports are 0x3f8 and 0x2f8 respectively. This mismatch could
contribute to missing console output, but the CloudLab console's selected port
has not been verified here.

## Practical remedies

The already-working kernel logging arguments are `loglevel=7 ignore_loglevel`
without bare `debug`; the current boot confirms `ignore_loglevel=Y`. This
avoids enabling systemd-wide debug output while retaining verbose kernel
console output.

For booting with systemd debug output, fix the CloudLab helper to inspect the
actual interface setup state, suppress journal output during detection, and
serialize the control-interface claim. Correct this node's stale `emulabcnet`
hint as well. These are proposed repairs, not changes installed by this audit.

Snapshots of the installed scripts, GRUB defaults, helper logs, source mtimes,
hashes, and read-only host checks are under [host_snapshot](host_snapshot).
The investigation changed only files in this evidence directory.

## Follow-up: user-reported historical debug defaults

The user subsequently confirmed previously using:

```ini
GRUB_CMDLINE_LINUX_DEFAULT="earlycon=uart,io,0x3f8,115200 loglevel=7 ignore_loglevel debug"
```

With the installed initramfs-tools implementation, bare `debug` independently
enables shell tracing to `/run/initramfs/initramfs.debug`. `earlycon` is a kernel
serial-console option; it does not select the initramfs `netconsole` branch.
An additional `systemd.log_level=info` would therefore preserve initramfs tracing
while reducing systemd logging. On this node `/run` is tmpfs and swap is disabled;
the trace can retain RAM after early boot, although no trace exists on the
current non-debug boot and its historical size is unknown. Early console output
normally ends when the regular console takes over unless retained explicitly.

The earlier [GRUB inventory](../grub_interval_audit/boot_versions.json) captured
`/etc/default/grub.bak` with March 16 modification/change timestamps and hash
`f92f3b64241ccb6f47864f0fc2e3cbda7aedb279bc3cb0b1159c46ee9e2b1e30`.
Its final `GRUB_CMDLINE_LINUX` assignment is:

```ini
GRUB_CMDLINE_LINUX="console=ttyS0,115200 emulabcnet=00:5d:73:ef:d9:ee"
```

That MAC matches this node's confirmed permanent management MAC. The first
assignment in the same file used the stale `44:a8:42:47:4a:0b`, but the final
assignment overrides it. The older configuration therefore has a concrete
network-discovery difference that could permit full debug logging without
today's failure. The live backup has since changed (September 10 mtime/hash),
so historical metadata here refers to the retained inventory, not today's file.

The user boot-tested that proposed configuration at 08:46 CDT, and network
discovery still failed. A corrected MAC hint alone was insufficient because of
device initialization order. The [follow-up](march_repro_failed_0846/README.md)
records the boot and the subsequently installed discovery-script repair, which
retains full debug output. Neither the user report nor the March snapshot
establishes the exact command line or log sizes of the May benchmark boot.
The earlier GRUB report was also corrected to identify the matching MAC accurately.
