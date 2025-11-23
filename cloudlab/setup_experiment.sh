#!/bin/bash
# This script sets up the environment for a CloudLab experiment.
# Arguments:
# 1: Cloudlab username
# 2: Name of the experiment
# 3: Start node of experiment
# 4: End node of experiment

USER_NAME=$1
EXP_NAME=$2
END_NODE=$4
DOMAIN="wisc.cloudlab.us"
PROJECT_EXT="ldos-ut-PG0"

# Get the list of hostnames for all nodes in the experiment
i=$3
HOSTS=()
while [ "$i" -le "$END_NODE" ]; do
    HOSTS+=("$USER_NAME@node$i.$EXP_NAME.$PROJECT_EXT.$DOMAIN")
    i=$((i+1))
done

echo "Hosts in the experiment:"
for host in "${HOSTS[@]}"; do
    echo "$host"
done

# Own all important directories
for i in "${!HOSTS[@]}"; do
  host=${HOSTS[$i]}
  echo "Changing ownership on $host ..."
  ssh -o StrictHostKeyChecking=no $host "tmux new-session -d -s chown \"
    sudo chown -R \$USER: /mnt/data/workloads &&
    sudo chown -R \$USER: /usr/local/hemem
    \""
done
wait

# Clone the memory-tiering repository
for i in "${!HOSTS[@]}"; do
  host=${HOSTS[$i]}
  echo "Setting up on $host ..."
  ssh -o StrictHostKeyChecking=no $host "tmux new-session -d -s setup \"
    git clone https://github.com/SujayYadalam94/tiering_solutions.git &&
    pushd tiering_solutions &&
    git checkout policysmith &&
    rm -r linux &&
    ln -s /usr/local/hemem/linux linux &&
    popd
    \""
done
wait

# Create the data/ directory and clone worklaads.
for i in "${!HOSTS[@]}"; do
  host=${HOSTS[$i]}
  echo "Cloning workloads on $host ..."
  ssh -o StrictHostKeyChecking=no $host "tmux new-session -d -s workloads \"
    sudo mkdir -p /mnt/data &&
    sudo chown -R \$USER: /mnt/data &&
    
    pushd /mnt/data &&
    git clone --recursive https://github.com/SujayYadalam94/workloads.git &&
    sudo chown -R \$USER: workloads &&

    pushd workloads/gups_hemem &&
    make &&
    popd &&

    popd
    \""
done
wait

# Push the update_kernel.sh script to all nodes and run it
for i in "${!HOSTS[@]}"; do
  host=${HOSTS[$i]}
  echo "Setting up crontab on $host ..."
  scp -o StrictHostKeyChecking=no setup/update_kernel.sh $host:~/
  ssh -o StrictHostKeyChecking=no $host "bash update_kernel.sh"
done
wait

# Reboot all nodes to finalize setup
for i in "${!HOSTS[@]}"; do
  host=${HOSTS[$i]}
  echo "Rebooting $host ..."
  ssh -o StrictHostKeyChecking=no $host "sudo reboot"
done
wait

echo "Setup complete on all nodes."