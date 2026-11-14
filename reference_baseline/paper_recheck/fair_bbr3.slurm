#!/bin/bash
# TCP-BBRv3 fair row for Table II. Config reproduces the fair recheck EXACTLY (10 UE, numSat=3,
# hoTime=262 -> HO@131/393s, 600s, 100Mbps, feederDelay=0.010, targetDt=30, BOLA), only CC differs.
# traces=false (does not affect QoE, which is parsed from inline sim.log interTime/newBitRate).
#SBATCH --job-name=fair_bbr3
#SBATCH --account=def-ekram
#SBATCH --output=/project/6030214/adeel/ns3-ntn-iab-dash/reference_baseline/paper_recheck/logs/bbr3_%A_%a.out
#SBATCH --error=/project/6030214/adeel/ns3-ntn-iab-dash/reference_baseline/paper_recheck/logs/bbr3_%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=16G
#SBATCH --time=24:00:00
#SBATCH --array=0-21%22

NS3=/project/6030214/adeel/ns3-ntn-iab-dash
SEEDS=(2 3 4 5 7 12 20 21 25 26 28 31 32 33 34 36 37 41 44 46 47 49)
SEED=${SEEDS[$SLURM_ARRAY_TASK_ID]}
RUNDIR=$NS3/reference_baseline/paper_recheck/fair/tcp_bbr3/run_$SEED
mkdir -p "$RUNDIR"; cd "$RUNDIR" || exit 1
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$NS3/build
echo "cc=tcp_bbr3 seed=$SEED host=$(hostname) start=$(date)"
"$NS3/build/scratch/ntn-iab-tcp-dash" \
  --ccAlgorithm=ns3::TcpBbr3 --run="$SEED" \
  --numUes=10 --numRelay=1 --numSat=3 --hoTime=262 --simDuration=600 \
  --backhaulRate=100Mbps --feederDelay=0.010 --targetDt=30 \
  --abrAlgorithm=ns3::BolaClient --traces=false --ueMobility=false \
  > sim.log 2>&1
echo "cc=tcp_bbr3 seed=$SEED exit=$? end=$(date)"
