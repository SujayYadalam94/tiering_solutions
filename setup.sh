
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

kernel_auto_min_free_kbytes() {
	# Match init_per_zone_wmark_min() in this kernel: sqrt(lowmem_kbytes * 16),
	# clamped to 128 KiB..256 MiB. On these 64-bit hosts, MemTotal is a close
	# approximation of the kernel's managed low-memory page count. Enabling THP
	# may subsequently raise this to khugepaged's normal recommendation.
	awk '
		$1 == "MemTotal:" {
			value = int(sqrt($2 * 16))
			if (value < 128) value = 128
			if (value > 262144) value = 262144
			print value
			exit
		}
	' /proc/meminfo
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

apply_cpu_slowdown_if_needed() {
	if [[ "${MEASUREMENT_PLATFORM}" != "c220g5" ]]; then
		echo "Skipping CPU slowdown for platform ${MEASUREMENT_PLATFORM}"
		return
	fi
	if [[ "${MEASUREMENT_SYSTEM}" == "memtis" ]]; then
		echo "Skipping common MSR slowdown; MEMTIS will use its artifact script at the equivalent 700 MHz setting"
		return
	fi

	local cpu
	for cpu in ${CPU_SLOWDOWN_CPUS//,/ }; do
		if [[ "${cpu}" == *-* ]]; then
			local start=${cpu%-*}
			local end=${cpu#*-}
			for ((i = start; i <= end; i++)); do
				sudo wrmsr --processor "$i" 0x620 0x707
			done
		else
			sudo wrmsr --processor "$cpu" 0x620 0x707
		fi
	done
}

if [[ "${MEASUREMENT_SYSTEM}" == "memtis" ]]; then
	echo "Applying kernel-default VM settings for MEMTIS"
	MEMTIS_MIN_FREE_KBYTES=$(kernel_auto_min_free_kbytes)
	if [[ -z "${MEMTIS_MIN_FREE_KBYTES}" ]]; then
		echo "ERROR: could not calculate the kernel-default vm.min_free_kbytes" >&2
		exit 1
	fi
	write_sysctl_value vm.overcommit_memory 0
	write_sysctl_value vm.watermark_scale_factor 10
	write_sysctl_value vm.watermark_boost_factor 15000
	write_sysctl_value vm.min_free_kbytes "${MEMTIS_MIN_FREE_KBYTES}"
	write_sysctl_value vm.user_reserve_kbytes 131072
	write_sysctl_value vm.admin_reserve_kbytes 8192
else
	write_sysctl_value vm.overcommit_memory 1
	write_sysctl_value vm.watermark_scale_factor 10
	write_sysctl_value vm.watermark_boost_factor 10000
	# Experiment-specific reserves used by the other measurement systems.
	write_sysctl_value vm.min_free_kbytes 1048576
	write_sysctl_value vm.user_reserve_kbytes 16384
	write_sysctl_value vm.admin_reserve_kbytes 16384
fi
sudo sysctl -w vm.lowmem_reserve_ratio="256 256 32"
#sudo sysctl -w vm.zone_reclaim_mode=0
#sudo sysctl -w vm.dirty_background_ratio=1
#sudo sysctl -w vm.dirty_ratio=20
write_sysctl_value kernel.numa_balancing 0
write_sysfs_value /proc/sys/vm/zone_reclaim_mode 0
write_sysfs_value /proc/sys/kernel/numa_balancing 0
write_sysfs_value /sys/kernel/mm/numa/demotion_enabled 0
if [[ "${MEASUREMENT_SYSTEM}" != "memtis" ]]; then
	write_sysfs_value /sys/kernel/mm/lru_gen/enabled 0x0000
fi
echo "Turning huge page ON"
write_sysfs_value /sys/kernel/mm/transparent_hugepage/enabled always
write_sysfs_value /sys/kernel/mm/transparent_hugepage/defrag always
if [[ "${MEASUREMENT_SYSTEM}" == "memtis" ]]; then
	# The MEMTIS kernel and artifact leave shmem THP at its default, "never".
	# Set it explicitly because a preceding non-MEMTIS run may have selected
	# "force" through this shared setup script.
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/shmem_enabled never
else
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/shmem_enabled force
fi
write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/defrag 1
if [[ "${MEASUREMENT_SYSTEM}" == "memtis" ]]; then
	write_sysfs_value /proc/sys/vm/compaction_proactiveness 20
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/pages_to_scan 4096
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs 10000
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs 60000
else
	write_sysfs_value /proc/sys/vm/compaction_proactiveness 80
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/pages_to_scan 8192
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs 0
	write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs 1
fi
echo 1000000 | sudo tee /proc/sys/kernel/perf_event_max_sample_rate
echo 0 | sudo tee /proc/sys/kernel/perf_cpu_time_max_percent

write_sysfs_value /sys/kernel/mm/ksm/run 0

apply_cpu_slowdown_if_needed
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

# Free page cache and reclaimable slab so allocations match the requested headroom.
if [[ "${MEASUREMENT_SYSTEM}" == "memtis" ]]; then
	write_sysctl_value vm.vfs_cache_pressure 100 >/dev/null
else
	write_sysctl_value vm.vfs_cache_pressure 2000 >/dev/null
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

bash defrag.sh
