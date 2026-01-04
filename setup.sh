sudo bash -c 'echo always > /sys/kernel/mm/transparent_hugepage/enabled && echo always > /sys/kernel/mm/transparent_hugepage/defrag'
sudo sysctl -w vm.overcommit_memory=2
sudo sysctl -w kernel.numa_balancing=0

