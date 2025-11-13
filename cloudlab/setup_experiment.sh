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

echo "Setup complete on all nodes."