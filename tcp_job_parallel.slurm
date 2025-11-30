#!/bin/bash
#SBATCH --job-name=tcp-dash-parallel
#SBATCH --output=tcp-%A_%a.out
#SBATCH --error=tcp-%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=02:00:00
#SBATCH --mem=2G
#SBATCH --array=1-50

# Export LD_LIBRARY_PATH to include the build directory
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build

# Create a unique directory for this run if it doesn't exist
mkdir -p run_$SLURM_ARRAY_TASK_ID

# Change to that directory
cd run_$SLURM_ARRAY_TASK_ID

# Run the simulation
# passing the array task ID as the run number (RNG seed)
../build/scratch/ntn-iab-tcp-dash --run=${SLURM_ARRAY_TASK_ID}
