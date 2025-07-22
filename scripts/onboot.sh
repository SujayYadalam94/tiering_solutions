#!/bin/bash

# CXL emulation
sudo modprobe msr
sudo wrmsr --processor 39 0x620 0x707

# Create memory regions for Hemem to manage
sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax --align 2M
sudo ndctl create-namespace -f -e namespace1.0 --mode=devdax --align 2M

echo 1000000 | sudo tee /proc/sys/vm/max_map_count
echo 0 | sudo tee /proc/sys/kernel/numa_balancing
export LD_LIBRARY_PATH=/mydata/hemem/src:/mydata/hemem/Hoard/src:$LD_LIBRARY_PATH
