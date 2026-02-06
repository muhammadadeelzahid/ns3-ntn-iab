#!/bin/bash
#SBATCH --job-name=quic_dash
#SBATCH --account=def-ekram
#SBATCH --time=2-00:00:00
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --output=quic_%j.out
#SBATCH --error=quic_%j.err

cd /home/adeel/projects/def-ekram/adeel/ns3-ntn-iab
python3 ./waf --run "ntn-iab-quic-dash --numRelay=1"