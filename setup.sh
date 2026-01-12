SIZE_MIB=${1:-}
if [[ -z "${SIZE_MIB}" ]]; then
	echo "Usage: $0 <sizeMiB>" >&2
	exit 1
fi

pushd ~/colloid/tpp/memeater
export local_size="${SIZE_MIB}"
echo "Setting up memeater module with size ${local_size}MiB"
sudo insmod memeater.ko sizeMiB=$(numastat -m | grep MemFree | awk -v nidx=0 -v sz=$local_size '{print int($(2+nidx)-sz)}')

sudo bash -c 'echo always > /sys/kernel/mm/transparent_hugepage/enabled && echo always > /sys/kernel/mm/transparent_hugepage/defrag'
sudo sysctl -w vm.overcommit_memory=2
sudo sysctl -w kernel.numa_balancing=0
sudo wrmsr --processor 39 0x620 0x707

popd

# Migrate memory of all running user processes to NUMA node 1
for pid in $(ps -e -o pid=); do
	sudo migratepages $pid 0 1
done