#!/bin/bash

SETUP_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "${SETUP_SCRIPT_DIR}/measurement_settings.sh" || exit 1

SIZE_MIB=${1:-}
if [[ -z "${SIZE_MIB}" ]]; then
	echo "Usage: $0 <sizeMiB> [measurementSystem] [platform]" >&2
	exit 1
fi

MEASUREMENT_SYSTEM=${2:-default}
MEASUREMENT_PLATFORM=${3:-c220g5}

case "${MEASUREMENT_PLATFORM}" in
	c220g5)
		DEFAULT_IRQ_AFFINITY="10-19,30-39"
		DEFAULT_PROCESS_CPUSET="10-19,30-39"
		DEFAULT_MIGRATE_FROM_NODES="0"
		DEFAULT_CPU_SLOWDOWN_CPUS="10-19,30-39"
		;;
	gsl_optane)
		DEFAULT_IRQ_AFFINITY="16-31,48-63"
		DEFAULT_PROCESS_CPUSET="16-31,48-63"
		DEFAULT_MIGRATE_FROM_NODES="0"
		DEFAULT_CPU_SLOWDOWN_CPUS=""
		;;
	*)
		echo "ERROR: unsupported platform '${MEASUREMENT_PLATFORM}' (expected c220g5|gsl_optane)" >&2
		exit 1
		;;
esac

IRQ_AFFINITY=${IRQ_AFFINITY:-${DEFAULT_IRQ_AFFINITY}}
PROCESS_CPUSET=${PROCESS_CPUSET:-${DEFAULT_PROCESS_CPUSET}}
MIGRATE_FROM_NODES=${MIGRATE_FROM_NODES:-${DEFAULT_MIGRATE_FROM_NODES}}
MIGRATE_TO_NODE=${MIGRATE_TO_NODE:-1}
CPU_SLOWDOWN_CPUS=${CPU_SLOWDOWN_CPUS:-${DEFAULT_CPU_SLOWDOWN_CPUS}}

write_sysfs_value() {
	local path=$1
	local value=$2
	printf '%s' "${value}" | sudo tee "${path}" >/dev/null
}

write_sysctl_value() {
	local key=$1
	local value=$2
	sudo sysctl -w "${key}=${value}"
}

set_irq_affinity() {
	for f in /proc/irq/*/smp_affinity_list; do
	  echo "${IRQ_AFFINITY}" | sudo tee "$f" >/dev/null 2>&1 || true
	done
}

migrate_all_processes_to_slow_tier() {
	for pid in $(ps -e -o pid=); do
		sudo migratepages "$pid" "${MIGRATE_FROM_NODES}" "${MIGRATE_TO_NODE}" || true
		sudo taskset -pc "${PROCESS_CPUSET}" "$pid"
	done
}

measurement_apply_common_settings write_sysfs_value 1000000 0 "${MEASUREMENT_SYSTEM}" || exit 1

measurement_apply_uncore_settings || exit 1
sudo swapoff -a

if [[ "${MEASUREMENT_SYSTEM}" == "nomad" ]]; then
	echo "Applying NOMAD memory-tiering settings"
	write_sysfs_value /sys/kernel/mm/numa/demotion_enabled 1
	write_sysfs_value /proc/sys/kernel/numa_balancing 2
	write_sysctl_value vm.demote_scale_factor 1000
	sudo swapoff -a
fi

#sudo systemctl set-property --runtime system.slice AllowedCPUs=10-19,30-39 AllowedMemoryNodes=1
#sudo systemctl set-property --runtime user.slice   AllowedCPUs=10-19,30-39 AllowedMemoryNodes=1

# future IRQs default
#echo "10-19,30-39" | sudo tee /proc/irq/default_smp_affinity_list

# existing IRQs
set_irq_affinity

# All-NUMA workloads need both tiers and should not move or re-affine the SSH,
# tmux, and system processes that launched the experiment.
if [[ "${MEASUREMENT_SYSTEM}" == "all_numa" ]]; then
	echo "Skipping global process migration for all-NUMA workload"
else
	# Migrate memory of all running user processes to the slow tier.
	migrate_all_processes_to_slow_tier
fi

#bash defrag.sh

#pushd ~/colloid/tpp/memeater
#export local_size="${SIZE_MIB}"
#echo "Setting up memeater module with size ${local_size}MiB"
# Use NUMA node0 MemFree because memeater allocates only on node0.
#node0_free_mib=$(numastat -m | awk '/MemFree/ {printf "%d", $2}')
#alloc_mib=$((node0_free_mib - local_size))
#if (( alloc_mib <= 0 )); then
#	echo "Requested headroom ${local_size}MiB exceeds node0 free ${node0_free_mib}MiB" >&2
#	exit 1
#fi
#sudo insmod memeater.ko sizeMiB=${alloc_mib} 
#popd

bash "${SETUP_SCRIPT_DIR}/defrag.sh" "${MEASUREMENT_SYSTEM}"
