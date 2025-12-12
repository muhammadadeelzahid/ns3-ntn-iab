#!/bin/bash
#SBATCH --job-name=tcp-cc-compare
#SBATCH --output=tcp-cc-%A_%a.out
#SBATCH --error=tcp-cc-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=2-00:00:00
#SBATCH --mem=8G
#SBATCH --array=1-300

# Set LD_LIBRARY_PATH to include the build directory
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Determine Algorithm and Run ID based on Task ID
if [ $SLURM_ARRAY_TASK_ID -le 50 ]; then
    CC_ALGO="ns3::TcpCubic"
    SHORT_NAME="Cubic"
    RUN_ID=$SLURM_ARRAY_TASK_ID
elif [ $SLURM_ARRAY_TASK_ID -le 100 ]; then
    CC_ALGO="ns3::TcpNewReno"
    SHORT_NAME="NewReno"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 50))
elif [ $SLURM_ARRAY_TASK_ID -le 150 ]; then
    CC_ALGO="ns3::TcpWestwood"
    SHORT_NAME="Westwood"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 100))
elif [ $SLURM_ARRAY_TASK_ID -le 200 ]; then
    CC_ALGO="ns3::TcpVegas"
    SHORT_NAME="Vegas"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 150))
elif [ $SLURM_ARRAY_TASK_ID -le 250 ]; then
    CC_ALGO="ns3::TcpHybla"
    SHORT_NAME="Hybla"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 200))
elif [ $SLURM_ARRAY_TASK_ID -le 300 ]; then
    CC_ALGO="ns3::TcpBbr"
    SHORT_NAME="BBR"
    RUN_ID=$(($SLURM_ARRAY_TASK_ID - 250))
else
    echo "Invalid Task ID: $SLURM_ARRAY_TASK_ID"
    exit 1
fi

# Create directory structure
BASE_DIR="Tcp_artifacts/$SHORT_NAME"
RUN_DIR="$BASE_DIR/run_$RUN_ID"
mkdir -p $RUN_DIR

# Change to the run directory
cd $RUN_DIR

# Run the simulation
echo "Running $CC_ALGO Run $RUN_ID (Task $SLURM_ARRAY_TASK_ID) in $RUN_DIR"
# Note: We need to adjust the path to the executable because we are deeper in the directory structure
# Original: ../build/scratch/ntn-iab-tcp-dash
# New: ../../../build/scratch/ntn-iab-tcp-dash (Tcp_artifacts/ShortName/run_X is 3 levels deep)
../../../build/scratch/ntn-iab-tcp-dash --run=$RUN_ID --ccAlgorithm="$CC_ALGO"

# Move back to original directory (optional)
cd ../../..
