sudo bash -c 'echo always > /sys/kernel/mm/transparent_hugepage/enabled && echo always > /sys/kernel/mm/transparent_hugepage/defrag'


sudo sync
sudo bash -c 'sync; echo 3 > /proc/sys/vm/drop_caches'
sudo bash -c 'sync; echo 2 > /proc/sys/vm/drop_caches'
sudo bash -c 'echo 1 > /proc/sys/vm/compact_memory'
sudo sync
sudo bash -c 'sync; echo 3 > /proc/sys/vm/drop_caches'
sudo bash -c 'sync; echo 2 > /proc/sys/vm/drop_caches'