
SIZE_MIB=${1:-}
if [[ -z "${SIZE_MIB}" ]]; then
	echo "Usage: $0 <sizeMiB>" >&2
	exit 1
fi

IRQ_AFFINITY=${IRQ_AFFINITY:-10-19,30-39}
PROCESS_CPUSET=${PROCESS_CPUSET:-10-19,30-39}

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
		sudo migratepages "$pid" 0 1
		sudo taskset -pc "${PROCESS_CPUSET}" "$pid"
	done
}

write_sysctl_value vm.overcommit_memory 1
write_sysctl_value vm.watermark_scale_factor 1
write_sysctl_value vm.watermark_boost_factor 0
# Shrink kernel reserves so nearly all RAM is usable by memeater and later apps.
write_sysctl_value vm.min_free_kbytes 16384
write_sysctl_value vm.user_reserve_kbytes 16384
write_sysctl_value vm.admin_reserve_kbytes 16384
#sudo sysctl -w vm.lowmem_reserve_ratio="1 1 1"
#sudo sysctl -w vm.zone_reclaim_mode=0
#sudo sysctl -w vm.dirty_background_ratio=1
#sudo sysctl -w vm.dirty_ratio=20
write_sysctl_value kernel.numa_balancing 0
write_sysfs_value /proc/sys/vm/zone_reclaim_mode 0
write_sysfs_value /proc/sys/kernel/numa_balancing 0
write_sysfs_value /sys/kernel/mm/numa/demotion_enabled 0
write_sysfs_value /proc/sys/vm/watermark_scale_factor 10
write_sysfs_value /sys/kernel/mm/lru_gen/enabled 0x0000
echo "Turning huge page ON"
write_sysfs_value /sys/kernel/mm/transparent_hugepage/enabled always
write_sysfs_value /sys/kernel/mm/transparent_hugepage/defrag always
write_sysfs_value /sys/kernel/mm/transparent_hugepage/khugepaged/defrag 1
write_sysfs_value /proc/sys/vm/compaction_proactiveness 20
sudo wrmsr --processor 39 0x620 0x707
sudo swapoff -a

#sudo systemctl set-property --runtime system.slice AllowedCPUs=10-19,30-39 AllowedMemoryNodes=1
#sudo systemctl set-property --runtime user.slice   AllowedCPUs=10-19,30-39 AllowedMemoryNodes=1

# future IRQs default
#echo "10-19,30-39" | sudo tee /proc/irq/default_smp_affinity_list

# existing IRQs
set_irq_affinity

# Migrate memory of all running user processes to NUMA node 1
migrate_all_processes_to_slow_tier

# Free page cache and reclaimable slab so allocations match the requested headroom.
write_sysctl_value vm.vfs_cache_pressure 2000 >/dev/null

bash defrag.sh

pushd ~/colloid/tpp/memeater
export local_size="${SIZE_MIB}"
echo "Setting up memeater module with size ${local_size}MiB"
# Use NUMA node0 MemFree because memeater allocates only on node0.
node0_free_mib=$(numastat -m | awk '/MemFree/ {printf "%d", $2}')
alloc_mib=$((node0_free_mib - local_size))
if (( alloc_mib <= 0 )); then
	echo "Requested headroom ${local_size}MiB exceeds node0 free ${node0_free_mib}MiB" >&2
	exit 1
fi
#sudo insmod memeater.ko sizeMiB=${alloc_mib} 
popd

bash defrag.sh