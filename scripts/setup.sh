#!/bin/bash

set -euo pipefail
trap 'echo "ERR: command \"$BASH_COMMAND\" failed at ${BASH_SOURCE}:${LINENO}" >&2' ERR

GRUB_FILE="/etc/default/grub"
APPEND_STR="memmap=80G!8G memmap=80G!104G"

# Skip if memmap already present
if grep -q "$APPEND_STR" "$GRUB_FILE"; then
    echo "grub already has $APPEND_STR"
else
    # Backup first
    cp -a "$GRUB_FILE" "$GRUB_FILE.bak.$(date +%s)"
    tac /etc/default/grub | \
        sed '0,/GRUB_CMDLINE_LINUX="[^"]*"/{s/\(GRUB_CMDLINE_LINUX="[^"]*\)"/\1 '"$APPEND_STR"'"\n# \1"/}' | \
        tac | \
        sudo tee $GRUB_FILE
    sudo update-grub
fi

CONF_FILE="/etc/initramfs-tools/initramfs.conf"

# Read current value of COMPRESS
current_compress=$(grep -E '^COMPRESS=' "$CONF_FILE" | cut -d'=' -f2- || echo "")

if [[ "$current_compress" == "gzip" ]]; then
    echo "COMPRESS is already set to gzip. No changes made."
else
    BACKUP_FILE="${CONF_FILE}.bak.$(date +%s)"

    # Make a backup first
    sudo cp "$CONF_FILE" "$BACKUP_FILE"

    # Update COMPRESS= line
    sudo sed -i 's/^COMPRESS=.*/COMPRESS=gzip/' "$CONF_FILE"

    echo "Updated COMPRESS= to gzip in $CONF_FILE"
    echo "Backup saved as $BACKUP_FILE"
fi

sudo apt update
sudo apt-get install -y git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison cmake htop clang pkg-config libcapstone-dev numactl msr-tools

git submodule update --init --recursive

pushd ./linux

#patch -p1 < ../linux.patch
git apply ../linux.patch

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

set +e # Disable return checking
yes '' | make localmodconfig
set -e

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

# Configure crontab to run onboot.sh on boot

# Find the absolute path to onboot.sh
ONBOOT_SCRIPT="$(realpath ./scripts/onboot.sh)"
echo "Adding crontab entry to run onboot.sh on boot for current user..."

# Add the crontab entry for the current user if it doesn't already exist
(crontab -l 2>/dev/null | grep -v '@reboot' || true; echo "@reboot $ONBOOT_SCRIPT") | crontab -

echo "Setting grub default and configuring auto-boot..."
KERNEL_VERSION="5.1.0-hemem-rc4+"

# Update GRUB configuration with GRUB_SAVEDEFAULT and GRUB_DEFAULT
if ! grep -q "GRUB_SAVEDEFAULT" "$GRUB_FILE"; then
    echo "GRUB_SAVEDEFAULT=true" | sudo tee -a "$GRUB_FILE"
fi

if grep -q "^GRUB_DEFAULT=" "$GRUB_FILE"; then
    sudo sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/' "$GRUB_FILE"
else
    echo "GRUB_DEFAULT=saved" | sudo tee -a "$GRUB_FILE"
fi

sudo update-grub
sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux $KERNEL_VERSION"

echo "=============================="
echo "Setup complete! The system will now reboot with the new kernel."
echo "On boot, the onboot.sh script will run automatically via crontab."
echo "=============================="

# Reboot the system
sudo reboot
