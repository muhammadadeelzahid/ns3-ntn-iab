#!/bin/bash
#SBATCH --job-name=quic-dash-parallel
#SBATCH --output=quic-%A_%a.out
#SBATCH --error=quic-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=02:00:00
#SBATCH --mem=4G
#SBATCH --array=1-50

# Create a unique directory for this run
RUN_DIR="run_${SLURM_ARRAY_TASK_ID}"
mkdir -p $RUN_DIR

# Set LD_LIBRARY_PATH to include the build directory
# Use absolute path to be safe
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Copy necessary config files if any (optional, depending on script needs)
# cp config.txt $RUN_DIR/

# Change to the run directory
cd $RUN_DIR

# Run the simulation directly to ensure CWD is respected
# This assumes the project is already built
../build/scratch/ntn-iab-quic-dash --run=${SLURM_ARRAY_TASK_ID}

# Move back to original directory (optional, as job ends here)
cd ..
