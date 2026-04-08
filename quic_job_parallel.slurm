#!/bin/bash
#SBATCH --job-name=quic-cc-compare
#SBATCH --output=quic-cc-%A_%a.out
#SBATCH --error=quic-cc-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=4-00:00:00
#SBATCH --mem=32G
#SBATCH --array=1-60

# Number of relays: pass via sbatch --export=NUM_RELAYS=0 or NUM_RELAYS=1 (default: 1)
# Example: sbatch --export=NUM_RELAYS=0 quic_job_parallel.slurm
#          sbatch --export=NUM_RELAYS=1 quic_job_parallel.slurm
NUM_RELAYS=${NUM_RELAYS:-1}

# Set LD_LIBRARY_PATH to include the build directory
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Determine Algorithm and Run ID based on Task ID (20 runs per algorithm)
if [ $SLURM_ARRAY_TASK_ID -le 30 ]; then
    CC_ALGO="ns3::QuicCongestionControl"
    SHORT_NAME="NewReno"
    RUN_ID=$SLURM_ARRAY_TASK_ID
else
    CC_ALGO="ns3::QuicBbr"
    SHORT_NAME="BBR"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 30))
fi

# Create directory structure: Quic_artifacts/relay_N/Algorithm/run_X
RELAY_DIR="relay_${NUM_RELAYS}"
BASE_DIR="Quic_artifacts/$RELAY_DIR/$SHORT_NAME"
RUN_DIR="$BASE_DIR/run_$RUN_ID"
mkdir -p $RUN_DIR

# Change to the run directory
cd $RUN_DIR

# Run the simulation (4 levels deep: Quic_artifacts/relay_N/Algo/run_X)
echo "Running $CC_ALGO Run $RUN_ID (Task $SLURM_ARRAY_TASK_ID) in $RUN_DIR (numRelay=$NUM_RELAYS)"
../../../../build/scratch/ntn-iab-quic-dash --run=$RUN_ID --ccAlgorithm="$CC_ALGO" --numRelay=$NUM_RELAYS

cd ../../../..
