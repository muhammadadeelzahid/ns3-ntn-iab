#!/bin/bash
#SBATCH --job-name=quic-cc-compare
#SBATCH --output=quic-cc-%A_%a.out
#SBATCH --error=quic-cc-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=2-00:00:00
#SBATCH --mem=4G
#SBATCH --array=1-100

# Set LD_LIBRARY_PATH to include the build directory
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Determine Algorithm and Run ID based on Task ID
if [ $SLURM_ARRAY_TASK_ID -le 50 ]; then
    CC_ALGO="ns3::QuicCongestionControl"
    SHORT_NAME="NewReno"
    RUN_ID=$SLURM_ARRAY_TASK_ID
else
    CC_ALGO="ns3::QuicBbr"
    SHORT_NAME="BBR"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 50))
fi

# Create directory structure
BASE_DIR="Quic_artifacts/$SHORT_NAME"
RUN_DIR="$BASE_DIR/run_$RUN_ID"
mkdir -p $RUN_DIR

# Change to the run directory
cd $RUN_DIR

# Run the simulation
echo "Running $CC_ALGO Run $RUN_ID (Task $SLURM_ARRAY_TASK_ID) in $RUN_DIR"
# Note: We need to adjust the path to the executable because we are deeper in the directory structure
# Original: ../build/scratch/ntn-iab-quic-dash
# New: ../../../build/scratch/ntn-iab-quic-dash (Quic_artifacts/ShortName/run_X is 3 levels deep)
../../../build/scratch/ntn-iab-quic-dash --run=$RUN_ID --ccAlgorithm="$CC_ALGO"

# Move back to original directory (optional)
cd ../../..
