#!/bin/bash

sudo apt update
sudo apt-get install -y git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison cmake htop clang pkg-config libcapstone-dev numactl msr-tools

git submodules update --init --recursive

pushd ./linux

patch -p1 < ../linux.patch

cp /boot/config-$(uname -r) .config
scripts/config --disable SYSTEM_REVOCATION_KEYS

# TODO: Enable memmap configs
echo 'CONFIG_BLK_DEV_PMEM=y' >> .config
echo 'CONFIG_NVDIMM_PFN=y' >> .config
echo 'CONFIG_NVDIMM_DAX=y' >> .config
echo 'CONFIG_FS_DAX=y' >> .config
echo 'CONFIG_DAX=y' >> .config
echo 'CONFIG_DEV_DAX=y' >> .config
echo 'CONFIG_DEV_DAX_PMEM=y' >> .config
echo 'CONFIG_DEV_DAX_KMEM=y' >> .config
echo 'CONFIG_X86_MSR=y' >> .config

yes '' | make localmodconfig

make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install

popd

# Build Hoard
# HC: Had to checkout master to build properly
pushd ./Hoard/src
git checkout master
make -j$(nproc)
sudo make install
popd

# Build syscall_intercept library
git clone https://github.com/pmem/syscall_intercept.git

pushd syscall_intercept/
mkdir build; cd build;
cmake ..
make -j$(nproc)
sudo make install
popd

# Build Hemem library
# **IMPORTANT** Make changes to pebs.c to change PEBS events to track remote DRAM access
pushd ./src
make
make libhemem-lru.so
popd

# Install ndctl
sudo apt install -y ndctl

echo "=============================="
echo "Setup almost done, you need to update /etc/initramfs-tools/initramfs.conf with gzip compression."
echo "Edit /etc/default/grub and add memmap=32G!4G,66G!112G."
