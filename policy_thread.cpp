#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <iostream>

#include "arms_kernel_threads.h"
#include "timer.h"

void *arms_policy_thread(void *arg)
{
    (void)arg;
    // Set thread affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(POLICY_THREAD_CPU, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    struct ptimer loop_timer;
    ptimer_init(&loop_timer, "Policy loop timer");

    while (!terminated)
    {
        ptimer_start(&loop_timer);
        curr_window_index = global_version % WINDOW_SIZE;
        global_version++;
        prev_access_version = curr_access_version;
        curr_access_version = 1 - curr_access_version;
        __sync_synchronize();

        // Report migrations from the previous period
        uint64_t period_promoted = migrations_up_period.exchange(0, std::memory_order_relaxed);
        uint64_t period_demoted = migrations_down_period.exchange(0, std::memory_order_relaxed);
        std::cout << "[ARMS] Period migrations - promoted: " << period_promoted << ", demoted: " << period_demoted
                  << std::endl;

        // Drop stale migration requests at the start of each policy iteration
        clear_migration_queue();

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

        // Print total samples
        std::cout << "[ARMS] Total samples - DRAMREAD: " << total_samples[DRAMREAD]
                  << ", NVMREAD: " << total_samples[NVMREAD] << ", WRITE: " << total_samples[WRITE] << std::endl;
        total_samples[DRAMREAD] = total_samples[NVMREAD] = total_samples[WRITE] = 0;

        ptimer_stop_and_print(&loop_timer);
        double elapsed_us = loop_timer.elapsed_us;

        if (elapsed_us < policy_thread_interval)
        {
            usleep(policy_thread_interval - elapsed_us);
        }
    }

    std::cout << "[ARMS] Policy thread terminating..." << std::endl;
    return nullptr;
}
