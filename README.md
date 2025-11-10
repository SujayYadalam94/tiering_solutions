# Tiering Solutions - AutoNUMA Branch

This branch contains the source of Linux v6.2 with subtle modifications to enable tiering across NUMA nodes. 

This branch also includes a patch to prioritize file page allocations on a remote NUMA node (node 1). This is useful to prevent AutoNUMA from trying to migrate file pages, AutoNUMA can focus on migrating only anonymous pages.

## Building Linux

First, initialize the submodule.

```bash
git submodule update --init
```

You could follow your own approach to build Linux kernel or use the steps below:

Optional: Apply the patch if you want to measure AutoNUMA tiering performance only for anonymous pages.

```bash
pushd autonuma-linux
git apply ../alloc_filepages_node1.patch
popd
```

Install dependencies required to build Linux.

```bash
sudo apt update
sudo apt-get install -y git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison numactl htop tree cgroup-tools libtraceevent-dev pkg-config msr-tools
```

We enable some kernel flags that we use for our evaluations. You could skip them.

```bash
cd autonuma-linux

cp /boot/config-$(uname -r) .config
scripts/config --disable SYSTEM_REVOCATION_KEYS

echo 'CONFIG_MEMORY_HOTPLUG=y' >> .config
echo 'CONFIG_BLK_DEV_PMEM=m' >> .config
echo 'CONFIG_NVDIMM_PFN=y' >> .config
echo 'CONFIG_NVDIMM_DAX=y' >> .config
echo 'CONFIG_FS_DAX=y' >> .config
echo 'CONFIG_DAX=y' >> .config
echo 'CONFIG_DEV_DAX=m' >> .config
echo 'CONFIG_DEV_DAX_PMEM=m' >> .config
echo 'CONFIG_DEV_DAX_KMEM=m' >> .config
echo 'CONFIG_X86_MSR=y' >> .config
```

Build Linux.

```bash
yes '' | make localmodconfig
make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
```

Reboot the system into the desired kernel:

```bash
sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 6.2.0+"
```

## Running applications with AutoNUMA

Verify that you have booted into the AutoNUMA kernel.

```bash
uname -r
```

Enable AutoNUMA.

```bash
# numad will override autoNUMA, so stop it
sudo service numad stop

echo 15 > /proc/sys/vm/zone_reclaim_mode
echo 2 > /proc/sys/kernel/numa_balancing
echo 1 > /sys/kernel/mm/numa/demotion_enabled
echo 200 > /proc/sys/vm/watermark_scale_factor

# Optional: If you want to use MGLRU demotion rather than simple LRU demotion
echo 0x0007 > /sys/kernel/mm/lru_gen/enabled

# Optional: Enable Hugepages
echo always | sudo tee /sys/kernel/mm/transparent_hugepage/enabled

# Optional: Lower node 1 memory controller frequency to emulate CXL
sudo modprobe msr
sudo wrmsr --processor 10 0x620 0x707
```

If you want to restrict the size of local memory to a smaller value than the full capacity, you can use the `memeater` module from Colloid's repo.

```bash
git clone https://github.com/host-architecture/colloid.git
cd colloid/tpp/memeater
make
export local_size=... # Desired value
sudo insmod memeater.ko sizeMiB=$(numastat -m | grep MemFree | awk -v nidx=0 -v sz=$local_size '{print int($(2+nidx)-sz)}')
```

Additionally, you might want to reset kswapd stats. kswapd stops migrating if the number of migrations failures exceed a threshold. Colloid's repo again has a useful tool to reset the stats periodically.

```bash
cd colloid/tpp/kswapdrst
make
sudo insmod kswapdrst.ko
```

Run any workload by pinning the threads to a single NUMA node. You could use `numactl` or `taskset` for this. For example,

```bash
numactl -N0 ./app
```

You can monitor the memory usage using `numastat -m` and monitor migrations using `cat /proc/vmstat`.