#!/bin/bash

SIZES=(2000)

for size in "${SIZES[@]}"; do
    echo "== Running measurements with size ${size}MiB =="

    # Clean up any existing module state before reloading
    sudo bash unsetup.sh
    sudo bash setup.sh "${size}"

    # Reload the module to ensure clean state
    sudo bash unsetup.sh
    sudo bash setup.sh "${size}"

    # Run both measurement passes for this size
    ./measurement_arms.sh "${size}"
    ./measurement_model.sh "${size}"

done
