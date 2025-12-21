#!/bin/bash
#SBATCH --job-name=tcp_dash
#SBATCH --account=def-ekram
#SBATCH --time=4-00:00:00
#SBATCH --cpus-per-task=2
#SBATCH --mem=16G
#SBATCH --output=tcp_%j.out
#SBATCH --error=tcp_%j.err

cd /home/adeel/projects/def-ekram/adeel/ns3-ntn-iab

python3 ./waf --run "ntn-iab-tcp-dash"

