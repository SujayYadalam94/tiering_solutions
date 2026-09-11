#!/bin/bash

# Same uncore MSR, ratio limits, and target CPUs for every measurement system.
measurement_apply_uncore_settings() {
    MEASUREMENT_UNCORE_METHOD=unchanged
    MEASUREMENT_UNCORE_CPUS=
    MEASUREMENT_UNCORE_FREQ_KHZ=
    if [[ "${MEASUREMENT_PLATFORM:-c220g5}" != "c220g5" ]]; then
        echo "Skipping uncore MSR setup for platform ${MEASUREMENT_PLATFORM}"
        return 0
    fi

    local cpu first last i
    MEASUREMENT_UNCORE_CPUS=${CPU_SLOWDOWN_CPUS:-10-19,30-39}
    for cpu in ${MEASUREMENT_UNCORE_CPUS//,/ }; do
        if [[ "${cpu}" == *-* ]]; then
            first=${cpu%-*}
            last=${cpu#*-}
            for ((i = first; i <= last; i++)); do
                sudo wrmsr --processor "${i}" 0x620 0x707 || return 1
            done
        else
            sudo wrmsr --processor "${cpu}" 0x620 0x707 || return 1
        fi
    done
    MEASUREMENT_UNCORE_METHOD=wrmsr
    MEASUREMENT_UNCORE_FREQ_KHZ=700000
    echo "Uncore: wrmsr 0x620=0x707 on CPUs ${MEASUREMENT_UNCORE_CPUS} (700 MHz)"
}

# Shared experiment settings. The writer takes an absolute path and a value.
# System-specific tiering mechanisms are configured by their own runners.
measurement_apply_common_settings() {
    local writer=$1
    local perf_sample_rate=${2:-1000000}
    local perf_cpu_percent=${3:-0}
    local system=${4:-default}
    local path value
    while read -r path value; do
        # The MEMTIS artifact leaves generic reclaim demotion inherited;
        # its HTMM workers perform promotion/demotion independently.
        if [[ "${path}" == /sys/kernel/mm/numa/demotion_enabled &&
              ( "${system}" == memtis || "${system}" == memtis_near ) ]]; then
            continue
        fi
        "${writer}" "${path}" "${value}" || return 1
    done <<'SETTINGS'
/proc/sys/vm/overcommit_memory 1
/proc/sys/vm/watermark_scale_factor 10
/proc/sys/vm/watermark_boost_factor 10000
/proc/sys/vm/user_reserve_kbytes 16384
/proc/sys/vm/admin_reserve_kbytes 16384
/proc/sys/vm/min_free_kbytes 1048576
/proc/sys/vm/lowmem_reserve_ratio 256 256 32
/proc/sys/vm/zone_reclaim_mode 0
/proc/sys/vm/vfs_cache_pressure 2000
/proc/sys/kernel/numa_balancing 0
/sys/kernel/mm/numa/demotion_enabled 0
/sys/kernel/mm/transparent_hugepage/enabled always
/sys/kernel/mm/transparent_hugepage/defrag always
/sys/kernel/mm/transparent_hugepage/shmem_enabled force
/sys/kernel/mm/transparent_hugepage/khugepaged/defrag 1
/proc/sys/vm/compaction_proactiveness 80
/sys/kernel/mm/transparent_hugepage/khugepaged/pages_to_scan 8192
/sys/kernel/mm/transparent_hugepage/khugepaged/scan_sleep_millisecs 0
/sys/kernel/mm/transparent_hugepage/khugepaged/alloc_sleep_millisecs 1
/sys/kernel/mm/ksm/run 0
SETTINGS

    # MGLRU is absent on the MEMTIS kernel; disable it where supported.
    if [[ -e /sys/kernel/mm/lru_gen/enabled ]]; then
        "${writer}" /sys/kernel/mm/lru_gen/enabled 0x0000 || return 1
    fi

    # The kernel rejects sample-rate writes while throttling is disabled.
    # Temporarily enable it before applying the requested final perf limits.
    "${writer}" /proc/sys/kernel/perf_cpu_time_max_percent 25 || return 1
    "${writer}" /proc/sys/kernel/perf_event_max_sample_rate "${perf_sample_rate}" || return 1
    "${writer}" /proc/sys/kernel/perf_cpu_time_max_percent "${perf_cpu_percent}" || return 1
}

measurement_write_setting() {
    printf '%s\n' "$2" | sudo tee "$1" >/dev/null
}

# Final NUMA controls for baseline runs. MEMTIS uses its artifact's controls.
measurement_apply_baseline_default_migration_settings() {
    local writer=${1:-measurement_write_setting}
    "${writer}" /proc/sys/kernel/numa_balancing 1 || return 1
    "${writer}" /proc/sys/vm/zone_reclaim_mode 0 || return 1
    "${writer}" /sys/kernel/mm/numa/demotion_enabled false || return 1
}
