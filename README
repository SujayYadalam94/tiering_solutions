# Memory Tiering Solutions - AutoNUMA Branch

This branch contains the source of Linux v6.2 with subtle modifications to enable tiering across NUMA nodes. 

This branch also includes a patch to prioritize file page allocations on a remote NUMA node (node 1). This is useful to prevent AutoNUMA from trying to migrate file pages, AutoNUMA can focus on migrating only anonymous pages.

## Building Linux

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