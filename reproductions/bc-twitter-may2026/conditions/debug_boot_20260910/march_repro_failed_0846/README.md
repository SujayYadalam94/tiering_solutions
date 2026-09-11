# March GRUB reproduction: September 10, 08:46 CDT

Boot ID `5f62ade37f9349d3974299b664e3665c` used exactly the proposed March-style
command line: 6.18.1, ttyS0, earlycon at 0x3f8, debug, ignore_loglevel,
`emulabcnet=00:5d:73:ef:d9:ee`, and `memmap=90G!2G`.

Linux reached userspace and a serial login prompt; SSH started. The management
network failed again. Correcting the MAC hint alone was not sufficient.

## What the boot records establish

* The two high-speed NIC helpers run at about +19.7–19.9 seconds.
* The management NIC driver reports its MAC at +20.245 seconds and its helper
  starts around +21.17 seconds. The hinted interface was not yet available
  during the earlier probes, explaining why the helper's conditional MAC
  filter did not exclude those NICs.
* The corrected MAC filter does exclude `eno2`, recorded explicitly at
  +21.201 seconds and again at +21.465 seconds.
* At +25.938–25.941 seconds, `enp94s0f1np1`, `eno1`, and `enp94s0f0np0` each
  claim to be the control interface. Both other NICs were still configuring.
* `eno1` goes down at +26.355 seconds and loses its DHCP lease immediately.
  Its helper exits with status 1 because the lease file is gone.
* Network wait-online times out at +142.531 seconds. SSH starts afterward, but
  the missing management address prevents remote access.
* A power-button event occurs at +281.413 seconds, followed by orderly shutdown.

See [timeline](timeline.txt), [full journal](failed_boot.journal.log), and
[journal manifest](manifest.json). The attached console output independently
shows the wait-online failure, SSH startup, and `localhost login:` prompt.

## Repair installed without restarting networking

Changed `/usr/libexec/emulab/emulab-networkd.sh` (also reached through the
`/usr/local/etc/emulab` symlink):

1. Use `networkctl --lines=0 --no-pager`, fixed locale, and no colors, then
   match only the actual `State: ... (configured)` field. Journal text such as
   `IPv6LL is not configured yet` cannot satisfy this check.
2. Create `/run/cnet` with shell noclobber, so concurrent helpers cannot
   overwrite another helper's control-interface selection.
3. Suppress journal output for the subsequent Network File check as well.

The daemon's debug log level is not changed. No kernel, migration, benchmark,
GRUB, or running network configuration was modified during this repair.

[Patch](emulab-networkd-debug.patch), [original](emulab-networkd.original.sh),
[installed replacement](emulab-networkd.fixed.sh), and
[installation receipt](installation_receipt.json) are retained here.
The protected original backup is:
`/var/backups/emulab-networkd-before-debug-fix-20260910T141715Z.sh`.

## Validation and remaining test

The [isolated check](check_network_discovery.py) executes the actual discovery
and cleanup fragment under `/bin/sh`, with all runtime paths redirected into
a temporary directory and network operations replaced by local fixtures.
The old helper reproduces three claims and disables `eno1`. The replacement
selects only `eno1` with normal and debug output, and a separate simultaneous
configuration case permits only one claimant without disabling that winner.
[Results](discovery_check_results.json).

The new state predicate also correctly identifies only `eno1` on the live
node in a read-only check. Full-script shell syntax passes. After installation,
the management addresses, default routes, networkd PID, and GRUB contents
remain unchanged. No networking restart or reboot was performed.

A real boot with the repaired helper is still required to validate end-to-end
operation. [grub.march-debug.ready](grub.march-debug.ready) is a prepared retry
configuration derived from the user's current GRUB file, restoring the four
March assignments while retaining its other settings and comments. It uses
`memmap=90G!2G` rather than the currently running `92G!2G`; this is a separate
2 GiB memory-map difference. The candidate passed shell syntax checking but
has not been installed. The user can install it and run `update-grub` before
selecting the 6.18 kernel for the next boot.
