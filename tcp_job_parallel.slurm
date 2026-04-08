#!/bin/bash
#SBATCH --job-name=tcp-cc-compare
#SBATCH --output=tcp-cc-%A_%a.out
#SBATCH --error=tcp-cc-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=2-00:00:00
#SBATCH --mem=8G
#SBATCH --array=1-90

# Number of relays: pass via sbatch --export=NUM_RELAYS=0 or NUM_RELAYS=1 (default: 1)
# Example: sbatch --export=NUM_RELAYS=0 tcp_job_parallel.slurm
#          sbatch --export=NUM_RELAYS=1 tcp_job_parallel.slurm
NUM_RELAYS=${NUM_RELAYS:-1}

# Set LD_LIBRARY_PATH to include the build directory
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Determine Algorithm and Run ID based on Task ID (20 runs per algorithm)
if [ $SLURM_ARRAY_TASK_ID -le 30 ]; then
    CC_ALGO="ns3::TcpCubic"
    SHORT_NAME="Cubic"
    RUN_ID=$SLURM_ARRAY_TASK_ID
elif [ $SLURM_ARRAY_TASK_ID -le 60 ]; then
    CC_ALGO="ns3::TcpNewReno"
    SHORT_NAME="NewReno"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 30))
elif [ $SLURM_ARRAY_TASK_ID -le 90 ]; then
    CC_ALGO="ns3::TcpBbr"
    SHORT_NAME="BBR"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 60))
else
    echo "Invalid Task ID: $SLURM_ARRAY_TASK_ID"
    exit 1
fi

# Create directory structure: Tcp_artifacts/relay_N/Algorithm/run_X
RELAY_DIR="relay_${NUM_RELAYS}"
BASE_DIR="Tcp_artifacts/$RELAY_DIR/$SHORT_NAME"
RUN_DIR="$BASE_DIR/run_$RUN_ID"
mkdir -p $RUN_DIR

# Change to the run directory
cd $RUN_DIR

# Run the simulation (4 levels deep: Tcp_artifacts/relay_N/Algo/run_X)
echo "Running $CC_ALGO Run $RUN_ID (Task $SLURM_ARRAY_TASK_ID) in $RUN_DIR (numRelay=$NUM_RELAYS)"
../../../../build/scratch/ntn-iab-tcp-dash --run=$RUN_ID --ccAlgorithm="$CC_ALGO" --numRelay=$NUM_RELAYS

cd ../../../..
