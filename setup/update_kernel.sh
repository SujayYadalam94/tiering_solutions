#!/bin/bash

sudo apt update
sudo apt-get install -y git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison cmake htop clang pkg-config libcapstone-dev numactl msr-tools

sudo chown -R SujayYS /usr/local/hemem
pushd /usr/local/hemem/linux
echo 'CONFIG_X86_MSR=y' >> .config
echo 'CONFIG_BLK_DEV_PMEM=y' >> .config
echo 'CONFIG_NVDIMM_PFN=y' >> .config
echo 'CONFIG_NVDIMM_DAX=y' >> .config
echo 'CONFIG_FS_DAX=y' >> .config
echo 'CONFIG_DAX=y' >> .config
echo 'CONFIG_DEV_DAX=y' >> .config
echo 'CONFIG_DEV_DAX_PMEM=y' >> .config
echo 'CONFIG_DEV_DAX_KMEM=y' >> .config
yes '' | make localmodconfig
make -j $(nproc) && sudo make modules_install -j $(nproc) && sudo make install
popd

sudo chown -R dsaxena /usr/local/hemem

# Add memmap kernel boot parameter to /etc/default/grub
# Check if memmap is already present to avoid duplicates
if ! grep -q "memmap=34G\!4G,80G\!104G" /etc/default/grub; then
    sudo sed -i 's/GRUB_CMDLINE_LINUX="\(.*\)"/GRUB_CMDLINE_LINUX="\1 memmap=34G!4G,80G!104G"/' /etc/default/grub
    echo "Added memmap parameter to GRUB configuration"
else
    echo "memmap parameter already present in GRUB configuration"
fi

sudo update-grub

sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.1.0-rc4+"

# Add commands to root's crontab to run after every reboot
(sudo crontab -l 2>/dev/null | grep -v "randomize_va_space\|modprobe msr\|wrmsr --processor 39\|ndctl create-namespace"; cat <<EOF
@reboot echo 0 > /proc/sys/kernel/randomize_va_space
@reboot modprobe msr
@reboot wrmsr --processor 39 0x620 0x707
@reboot ndctl create-namespace -f -e namespace0.0 --mode=devdax --align 2M
@reboot ndctl create-namespace -f -e namespace1.0 --mode=devdax --align 2M
@reboot sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.1.0-rc4+"
@reboot sudo update-grub
EOF
) | sudo crontab -

echo "Root crontab entries added successfully. Commands will run after every reboot."

exit