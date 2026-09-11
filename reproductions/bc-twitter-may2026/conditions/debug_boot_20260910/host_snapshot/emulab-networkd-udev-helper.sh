#!/bin/sh

#
# This simply drops a .network file for interface $1 into
# /run/systemd/network so that networkd can pick it up, unless it gets
# overridden.
#

iface=$1
if [ -z "$iface" ]; then
    echo "ERROR: must provide an interface as the sole argument"
    exit 1
fi

ifacemac=`cat /sys/class/net/$iface/address | sed -ne 's/^\([a-fA-F0-9:]*\)$/\1/p' | tr ABCDEF abcdef | tr -d :`

CMDLINECNETMAC=`cat /proc/cmdline | sed -ne 's/^.* emulabcnet=\([a-fA-F0-9:]*\) *.*$/\1/p' | tr ABCDEF abcdef | tr -d :`
if [ -n "$CMDLINECNETMAC" -a -n "$ifacemac" ]; then
    for sifpath in /sys/class/net/* ; do
	sifname=`basename $sifpath`
	if [ -s $sifpath/address ]; then
	    sifmac=`cat $sifpath/address | sed -ne 's/^\([a-fA-F0-9:]*\)$/\1/p' | tr ABCDEF abcdef | tr -d :`
	    if [ -z "$sifmac" ]; then
		continue
	    fi

	    # If CMDLINECNETMAC exists on our system, and does *not* match
	    # our input iface name, do *not* generate a $iface.network file
	    # for systemd-networkd.
	    if [ "$sifmac" = "$CMDLINECNETMAC" -a ! "$ifacemac" = "$CMDLINECNETMAC" ]; then
		echo "Skipping $iface ($ifacemac) in favor of existing, hinted $CMDLINECNETMAC"
		exit 0
	    fi
	fi
    done
fi

#
# If this is a management interface, ignore it.
#
((echo "$iface" | grep -qi idrac) || (echo "$iface" | grep -qi ilo)) && exit 0

# Special-case custom DHCP config for older systemd-networkd.
DHCPSECNAME="DHCPv4"
NETWORKDVERSION=`networkctl --version | sed -ne 's/^systemd[^ ]*\ *\([0-9]*\).*/\1/p'`
if [ -n "$NETWORKDVERSION" -a $NETWORKDVERSION -le 237 ]; then
    DHCPSECNAME="DHCP"
fi

#
# NB, if the user has overridden this by some file in
# /etc/systemd/network, that takes precedence, and this won't be run.
# We have specific code in emulab-networkd.sh that is run from
# emulab-networkd-online@.service that checks to see if this file was
# used or not; we only handle the iface there if this file got used.
# NB: CriticalConnection=yes is added later by emulab-networkd.sh to
# the config file for the control net iface before systemd-networkd
# is restarted; thus, it does not appear below.
#
mkdir -p /run/systemd/network
cat <<EOF > /run/systemd/network/${iface}.network
[Match]
Name=$iface

[Network]
Description=Emulab control net search on $iface
DHCP=yes

[${DHCPSECNAME}]
UseNTP=yes
UseHostname=no
UseDomains=yes
EOF

exit 0
