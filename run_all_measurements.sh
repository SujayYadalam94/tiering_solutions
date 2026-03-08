#!/bin/bash

SIZES=(2000 4000 6000 8000 10000)
RUNS=1

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    for run in $(seq 1 ${RUNS}); do
        echo "-- Run ${run}/${RUNS} for size ${size}MiB --"

        # Clean up any existing module state before reloading
        sudo bash unsetup.sh
        sudo bash setup.sh "${size}"

        bash defrag.sh

        # Reload the module to ensure clean state
        sudo bash unsetup.sh
        sudo bash setup.sh "${size}"

        # Run both measurement passes for this size and run number
        bash defrag.sh
        ./measurement_arms.sh "${size}" "${run}" ""

        bash defrag.sh
        if [[ -x ./measurement_hybridtier.sh ]]; then
            sudo -E ./measurement_hybridtier.sh "${size}" "${run}" huge
        else
            echo "WARNING: ./measurement_hybridtier.sh not found or not executable; skipping HybridTier"
        fi



        #bash defrag.sh
        #./measurement_model.sh "${size}" "${run}" ""

        sudo bash unsetup.sh
    done
done
