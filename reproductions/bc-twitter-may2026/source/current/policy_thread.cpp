#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <iostream>

#include "arms_kernel_threads.h"
#include "timer.h"

void *arms_policy_thread(void *arg)
{
    (void)arg;
    register_tiering_runtime_tid();
    // Set thread affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(POLICY_THREAD_CPU, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    struct ptimer loop_timer;
    ptimer_init(&loop_timer, "Policy loop timer");

    while (!terminated.load(std::memory_order_relaxed))
    {
        clear_migration_queue();

        ptimer_start(&loop_timer);
        curr_window_index = global_version % WINDOW_SIZE;
        global_version++;
        prev_access_version = curr_access_version;
        curr_access_version = 1 - curr_access_version;
        __sync_synchronize();

        // Report migrations from the previous period
        uint64_t period_promoted = migrations_up_period.exchange(0, std::memory_order_relaxed);
        uint64_t period_demoted = migrations_down_period.exchange(0, std::memory_order_relaxed);
        if (ARMS_VERBOSE)
        {
            std::cout << "[ARMS] Period migrations - promoted: " << period_promoted
                      << " hugepages, demoted: " << period_demoted << " hugepages" << std::endl;
        }

        if (global_version % (1000000 / policy_thread_interval) == 0)
        {
            detect_hot_change();

            // Calculate latency diff between DRAM and NVM
            float dram_lat = UNLOADED_DRAM_LAT;
            float nvm_lat = UNLOADED_NVM_LAT;
            if (dram_bw_ewma > DRAM_BW_KNEE)
            {
                dram_lat += (dram_bw_ewma - DRAM_BW_KNEE) * DRAM_BW_SLOPE;
            }
            if (nvm_bw_ewma > NVM_RD_BW_KNEE)
            {
                nvm_lat += (nvm_bw_ewma - NVM_RD_BW_KNEE) * NVM_BW_SLOPE;
            }
            latency_diff = nvm_lat - dram_lat;
        }

        // Periodically update scores and schedule migrations
        static size_t timestep = 0;
        timestep++;

        update_scores_and_migrate(timestep);

        // Update sampling frequency if hotset change detected
        if ((active_bias == recn_bias) && sampling_mode != HIGH_FIDELITY)
        {
            change_sampling_frequency();
            sampling_mode = HIGH_FIDELITY;
        }
        else if ((active_bias == hist_bias) && sampling_mode != DEFAULT_SAMPLING)
        {
            change_sampling_frequency();
            sampling_mode = DEFAULT_SAMPLING;
        }

        const uint64_t filtered_preload_ip = take_preload_library_filtered_samples();
        const uint64_t filtered_helper_ip = take_helper_library_filtered_samples();
        const uint64_t filtered_runtime_tid = take_tiering_runtime_tid_filtered_samples();
        const uint64_t filtered_other_pid = take_other_pid_filtered_samples();

        // Print total samples
        if (ARMS_VERBOSE)
        {
            uint64_t unc_m_cas_count_wr = 0;
            const bool have_write_cas_count = get_unc_m_cas_count_wr(&unc_m_cas_count_wr);

            std::cout << "[ARMS] Total samples - READ: " << total_samples[READ] << ", WRITE: " << total_samples[WRITE]
                      << ", filtered_preload_ip: " << filtered_preload_ip
                      << ", filtered_helper_ip: " << filtered_helper_ip
                      << ", filtered_runtime_tid: " << filtered_runtime_tid
                      << ", filtered_other_pid: " << filtered_other_pid;
            if (have_write_cas_count)
            {
                std::cout << ", UNC_M_CAS_COUNT.WR: " << unc_m_cas_count_wr;
            }
            std::cout << std::endl;
        }
        total_samples[READ] = total_samples[WRITE] = 0;

        ptimer_stop(&loop_timer);
        if (ARMS_VERBOSE)
        {
            ptimer_print(&loop_timer);
        }
        double elapsed_us = loop_timer.elapsed_us;

        if (elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval - elapsed_us);
        }
    }

    if (ARMS_VERBOSE)
    {
        std::cout << "[ARMS] Policy thread terminating..." << std::endl;
    }
    return nullptr;
}
