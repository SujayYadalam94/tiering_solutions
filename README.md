# Tiering Solutions - MEMTIS branch

This branch contains the source for Linux with MEMTIS changes. It also includes the scripts to run applications with MEMTIS.

MEMTIS supports two system configurations
* DRAM + Intel DCPMM (used only single socket)
* local DRAM + remote DRAM (used two socket, CXL emulation mode)

## Building Linux

## Building Linux

First, initialize the submodule.

```bash
git submodule update --init
```

You could follow your own approach to build Linux kernel or use the steps below:

Firt, install dependencies required to build Linux.

```bash
sudo apt update
sudo apt-get install -y git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison numactl htop tree cgroup-tools libtraceevent-dev pkg-config msr-tools
```

Enable kernel config flags, some are mandatory such as `CONFIG_HTMM` while others are optional.

``bash
cd autonuma-linux

cp /boot/config-$(uname -r) .config
scripts/config --disable SYSTEM_REVOCATION_KEYS

# Mandatory
echo 'CONFIG_HTMM=y' >> .config

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
sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.15.19-htmm"
```

## Running applications with MEMTIS

Verify that you have booted into MEMTIS kernel.

### Userspace scripts
See memtis-userspace/

Please read memtis-userspace/README.md for detailed explanations

### Setting tiered memory systems with Intel DCPMM
* Reconfigures a namespace with devdax mode
```
sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax
...
```
* Reconfigures a dax device with system-ram mode (KMEM DAX)
```
sudo daxctl reconfigure-device dax0.0 --mode=system-ram
...
```

### Using the run script to run workloads
It is necessary to create/update a simple script for each benchmark. The `memtis-userspace/bench\_cmds` directory contains the scripts for some workloads.
If you want to execute *XSBench*, for instance, you have to create memtis-userspace/bench\_cmds/XSBench.sh.

Here is a sample:
```
# memtis-userspace/bench_cmds/XSBench.sh

BIN=/path/to/benchmark
BENCH_RUN="${BIN}/XSBench [Options]"

# Provide the DRAM size for each memory configuration setting.
# You must first check the resident set size of a benchmark.
if [[ "x${NVM_RATIO}" == "x1:16" ]]; then
    BENCH_DRAM="3850MB"
elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="7200MB"
elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
    BENCH_DRAM="21800MB"
fi

# required
export BENCH_RUN
export BENCH_DRAM

```

Once the script exists in that directory, you can run the workload using the commands below:

```
cd memtis-userspace/

# check running options
./scripts/run_bench.sh --help

# create an executable binary file
make

# run
sudo ./scripts/run_bench.sh -B ${BENCH} -R ${MEM_CONFIG} -V ${TEST_NAME}
```