#!/bin/bash

SIZES=(1000 1250 1500 1750 2000 2250 2500 2750 3000)
RUNS=10

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
        ./measurement_arms.sh "${size}" "${run}"

        bash defrag.sh
        ./measurement_model.sh "${size}" "${run}"

        sudo bash unsetup.sh
    done
done
