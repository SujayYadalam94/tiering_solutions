for i in {1..10}
do
    for threads in 1 2 4 8 12 16 20
    do
        for workload in a b c f d e
        do
            workload_file="/users/zimooo2/YCSB-cpp/workloads/workload${workload}"
            timeout 2000 taskset 0x3FF003FF sudo DRAMSIZE=1558183936 NVMSIZE=21078474752 LD_PRELOAD=./bazel-bin/libhemem_lib.so OMP_NUM_THREADS=12 MIN_INTERPOSE_MEM_SIZE=134217728 /users/zimooo2/YCSB-cpp/ycsb -run -db sqlite -P ${workload_file} -P /users/zimooo2/YCSB-cpp/sqlite/sqlite.properties -s -threads ${threads}
            sudo chmod 777 output.txt ; sed -i '1i step,page,count,model_selection,model_score,arms_score' output.txt ; mv output.txt ../../tiering_models/data/${workload}_threads${threads}_run${i}.csv
        done
    done
done