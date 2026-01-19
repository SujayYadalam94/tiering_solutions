#!/bin/bash

SIZES=(1000 1250 1500 1750 2000 2250 2500 2750 3000)

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    # Clean up any existing module state before reloading
    sudo bash unsetup.sh
    sudo bash setup.sh "${size}"

    bash defrag.sh

    # Reload the module to ensure clean state
    sudo bash unsetup.sh
    sudo bash setup.sh "${size}"

    # Run both measurement passes for this size
    #bash defrag.sh
    #./measurement_arms.sh "${size}" 

    bash defrag.sh
    ./measurement_model.sh "${size}"

    sudo bash unsetup.sh
done
