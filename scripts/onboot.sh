#!/bin/bash

# CXL emulation
sudo modprobe msr
sudo wrmsr --processor 39 0x620 0x707

# Create memory regions for Hemem to manage
sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax --align 2M
sudo ndctl create-namespace -f -e namespace1.0 --mode=devdax --align 2M

echo 1000000 | sudo tee /proc/sys/vm/max_map_count
echo 0 | sudo tee /proc/sys/kernel/numa_balancing

# Dynamically set LD_LIBRARY_PATH based on the location of this script
SCRIPT_DIR="$(dirname $(realpath "$0"))"
export LD_LIBRARY_PATH="$SCRIPT_DIR/../src:$SCRIPT_DIR/../Hoard/src:$LD_LIBRARY_PATH"
