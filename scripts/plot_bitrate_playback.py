#!/usr/bin/env python
"""
Script to plot bitrate over time and video playback/interruption status
from QUIC and TCP job output files.
Supports averaging across multiple runs from Slurm job arrays.
"""

import re
import os
import glob
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
from collections import defaultdict

def find_job_runs(prefix, directory):
    """
    Find all runs for the latest job matching the prefix.
    Returns a dict mapping task_id -> {'log': log_file, 'dir': run_dir}
    """
    # Look for slurm-JOBID_TASKID.out files
    # We assume the prefix helps identify the job, but for Slurm output it's usually just slurm-*.out
    # However, the user might have renamed them or we check the content/filename pattern.
    # The current pattern in the script was f"{prefix}_*.out".
    # But the new slurm script uses slurm-%A_%a.out.
    
    # Strategy:
    # 1. Find all *-*.out files (matching pattern NAME-JOBID_TASKID.out)
    # 2. Group by Job ID
    # 3. Pick latest Job ID
    # 4. Identify Task IDs
    
    # Match pattern: prefix-JOBID_TASKID.out or slurm-JOBID_TASKID.out
    # The user might use "quic-..." or "tcp-..." or "slurm-..."
    
    # We'll search for all .out files and try to match the pattern
    all_out_files = glob.glob(os.path.join(directory, "*.out"))
    job_groups = defaultdict(list)
    
    for filepath in all_out_files:
        filename = os.path.basename(filepath)
        # Match NAME-JOBID_TASKID.out
        # We want to match things like "quic-6548529_1.out" or "slurm-6548529_1.out"
        # But avoid matching things that don't look like job arrays if possible.
        # Regex: (anything)-(digits)_(digits).out
        match = re.search(r"(?:^|[\w-]+-)(\d+)_(\d+)\.out", filename)
        if match:
            job_id = int(match.group(1))
            task_id = int(match.group(2))
            job_groups[job_id].append((task_id, filepath))
            
    if not job_groups:
        # Fallback to legacy behavior or other naming patterns
        print("No JOBID_TASKID.out files found. Checking for legacy/single files...")
        return find_legacy_files(prefix, directory)

    # Pick latest job
    latest_job_id = max(job_groups.keys())
    print(f"Found latest Job ID: {latest_job_id} with {len(job_groups[latest_job_id])} runs")
    
    runs = {}
    for task_id, log_file in job_groups[latest_job_id]:
        run_dir = os.path.join(directory, f"run_{task_id}")
        if os.path.isdir(run_dir):
            runs[task_id] = {'log': log_file, 'dir': run_dir}
        else:
            print(f"Warning: Run directory {run_dir} not found for task {task_id}")
            # Still include it, maybe only log exists? But we need run dir for throughput
            runs[task_id] = {'log': log_file, 'dir': None}
            
    return runs

def find_legacy_files(prefix, directory):
    """Fallback to original file finding logic for single runs."""
    out_files = glob.glob(os.path.join(directory, f"{prefix}_*.out"))
    err_files = glob.glob(os.path.join(directory, f"{prefix}_*.err"))
    all_files = out_files + err_files
    
    if all_files:
        job_files = defaultdict(list)
        for filepath in all_files:
            filename = os.path.basename(filepath)
            match = re.search(rf"{prefix}_(\d+)\.(out|err)", filename)
            if match:
                job_id = match.group(1)
                job_files[job_id].append(filepath)
        
        if job_files:
            latest_job_id = max(job_files.keys(), key=lambda x: int(x))
            return {1: {'log': job_files[latest_job_id][0], 'dir': directory}} # Treat as run 1

    # Legacy zlogs
    legacy_filename = f"z{prefix}logs.txt"
    path = os.path.join(directory, legacy_filename)
    if os.path.exists(path):
        return {1: {'log': path, 'dir': directory}}
        
    return {}

def parse_bitrate_log(filename):
    """Parse bitrate information from a log file."""
    bitrate_data = []
    if not filename or not os.path.exists(filename):
        return bitrate_data
        
    with open(filename, 'r') as f:
        for line in f:
            match = re.search(r'(\d+\.?\d*)\s+Node:\s+\d+\s+newBitRate:\s+(\d+)', line)
            if match:
                time = float(match.group(1))
                bitrate = float(match.group(2))
                bitrate_data.append((time, bitrate))
    
    bitrate_data.sort(key=lambda x: x[0])
    return bitrate_data

def parse_playback_log(filename):
    """Parse playback status and resolution from a log file."""
    playback_events = []
    if not filename or not os.path.exists(filename):
        return playback_events
        
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    current_resolution = None
    resume_pending = False
    
    for line in lines:
        play_match = re.search(r'(\d+\.?\d*)\s+PLAYING FRAME:.*?Res:\s+(\d+)', line)
        if play_match:
            time = float(play_match.group(1))
            resolution = float(play_match.group(2))
            current_resolution = resolution
            if resume_pending:
                playback_events.append(('resumed', time, current_resolution))
                resume_pending = False
            playback_events.append(('playing', time, resolution))
        
        interrupt_match = re.search(r'(\d+\.?\d*)\s+No frames to play', line)
        if interrupt_match:
            time = float(interrupt_match.group(1))
            playback_events.append(('interrupted', time, current_resolution))
            resume_pending = False
        
        resume_match = re.search(r'Play resumed', line)
        if resume_match:
            resume_pending = True
            
    playback_events.sort(key=lambda x: x[1])
    return playback_events

def calculate_playback_stats(playback_events):
    """Calculate total playback and interruption times."""
    if not playback_events:
        return 0.0, 0.0, [], []
    
    sorted_events = sorted(playback_events, key=lambda x: x[1])
    playback_time = 0.0
    interruption_time = 0.0
    playback_periods = []
    interruption_periods = []
    
    state = None
    state_start_time = None
    
    for event_type, time, resolution in sorted_events:
        if event_type == 'playing':
            if state == 'interrupted' and state_start_time is not None:
                interruption_time += time - state_start_time
                interruption_periods.append((state_start_time, time))
            if state != 'playing':
                state_start_time = time
            state = 'playing'
        elif event_type == 'interrupted':
            if state == 'playing' and state_start_time is not None:
                playback_time += time - state_start_time
                playback_periods.append((state_start_time, time))
            if state != 'interrupted':
                state_start_time = time
            state = 'interrupted'
        elif event_type == 'resumed':
            if state == 'interrupted' and state_start_time is not None:
                interruption_time += time - state_start_time
                interruption_periods.append((state_start_time, time))
            state = 'playing'
            state_start_time = time
            
    if state == 'playing' and state_start_time is not None:
        last_time = sorted_events[-1][1]
        playback_time += last_time - state_start_time
        playback_periods.append((state_start_time, last_time))
    elif state == 'interrupted' and state_start_time is not None:
        last_time = sorted_events[-1][1]
        interruption_time += last_time - state_start_time
        interruption_periods.append((state_start_time, last_time))
        
    return playback_time, interruption_time, playback_periods, interruption_periods

def parse_dash_throughput(run_dir, prefix):
    """
    Parse DashClientRx logs in the run directory.
    prefix: 'DashClientRx_UE_' or 'DashClientRx_TCP_UE_'
    """
    throughputs = []
    if not run_dir:
        return throughputs
        
    files = glob.glob(os.path.join(run_dir, f"{prefix}*.txt"))
    
    for filename in files:
        try:
            with open(filename, 'r') as f:
                lines = f.readlines()
            data_lines = [line for line in lines if not line.strip().startswith('#') and line.strip()]
            if not data_lines: continue
            
            first_parts = data_lines[0].split()
            last_parts = data_lines[-1].split()
            
            if len(first_parts) < 4 or len(last_parts) < 4: continue
            
            start_time = float(first_parts[0])
            end_time = float(last_parts[0])
            total_bytes = int(last_parts[3])
            
            duration = end_time - start_time
            if duration > 0:
                throughput_mbps = (total_bytes * 8) / duration / 1e6
                throughputs.append(throughput_mbps)
        except Exception:
            continue
            
    return throughputs

def aggregate_bitrate(all_runs_bitrate, time_bin=1.0):
    """
    Aggregate bitrate data from multiple runs.
    Returns (time_points, mean_bitrates, std_bitrates)
    """
    if not all_runs_bitrate:
        return [], [], []
        
    # Find global time range
    min_time = float('inf')
    max_time = float('-inf')
    for run_data in all_runs_bitrate:
        if run_data:
            min_time = min(min_time, run_data[0][0])
            max_time = max(max_time, run_data[-1][0])
            
    if min_time == float('inf'):
        return [], [], []
        
    bins = np.arange(min_time, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin/2
    
    binned_values = defaultdict(list)
    
    for run_data in all_runs_bitrate:
        # For each run, sample the bitrate at each bin
        # Simple approach: take the last bitrate value before or within the bin
        # Better approach: weighted average within bin?
        # Let's use simple interpolation or "last known value"
        
        current_idx = 0
        current_val = 0
        
        for i, bin_end in enumerate(bins[1:]):
            # Find values in this bin
            values_in_bin = []
            while current_idx < len(run_data) and run_data[current_idx][0] <= bin_end:
                current_val = run_data[current_idx][1]
                values_in_bin.append(current_val)
                current_idx += 1
            
            # If no value in bin, use last known value
            val_to_use = current_val
            if values_in_bin:
                val_to_use = sum(values_in_bin) / len(values_in_bin) # Average within bin
            
            binned_values[i].append(val_to_use)
            
    mean_bitrates = []
    std_bitrates = []
    
    for i in range(len(bin_centers)):
        vals = binned_values[i]
        if vals:
            mean_bitrates.append(sum(vals) / len(vals))
            std_bitrates.append(np.std(vals))
        else:
            mean_bitrates.append(0)
            std_bitrates.append(0)
            
    return bin_centers, mean_bitrates, std_bitrates

def calculate_interruption_prob(all_runs_playback, time_bin=1.0):
    if not all_runs_playback: return [], []
    
    # Find range
    max_time = 0
    for run in all_runs_playback:
        if run: max_time = max(max_time, run[-1][1])
        
    bins = np.arange(0, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin/2
    probs = []
    
    for i, bin_end in enumerate(bins[1:]):
        bin_start = bins[i]
        interrupted_count = 0
        total_runs = 0
        
        for run in all_runs_playback:
            if not run: continue
            total_runs += 1
            # Check if interrupted at any point in this bin
            # Simplified: check status at bin_center
            # Reconstruct state at bin_center
            state = 'playing' # Default
            # Find last event before bin_center
            last_evt = None
            for evt in run:
                if evt[1] <= bin_start + time_bin/2:
                    last_evt = evt
                else:
                    break
            
            if last_evt:
                if last_evt[0] == 'interrupted':
                    state = 'interrupted'
                elif last_evt[0] == 'playing' or last_evt[0] == 'resumed':
                    state = 'playing'
            
            if state == 'interrupted':
                interrupted_count += 1
        
        if total_runs > 0:
            probs.append(interrupted_count / total_runs)
        else:
            probs.append(0)
            
    return bin_centers, probs

def plot_comparison(quic_stats, tcp_stats, clean=False):
    """Create comparison plots with multi-run support."""
    fig = plt.figure(figsize=(16, 12))
    # Use sharex=True via subplots instead of GridSpec for easier shared axis handling
    # But GridSpec allows custom height ratios. Let's stick to subplots with sharex for common axis requirement.
    # The user wants "each axis has its numbering", so we need to enable tick labels.
    
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(16, 12), sharex=True, gridspec_kw={'height_ratios': [1, 1, 1]})
    fig.suptitle('Bitrate and Playback Status Comparison' + (' (Clean)' if clean else ' (Averaged)'), fontsize=16, fontweight='bold')
    
    # Plot 1: Bitrate
    if quic_stats['bitrate_mean']:
        times = quic_stats['bitrate_time']
        means = [b/1e6 for b in quic_stats['bitrate_mean']]
        stds = [b/1e6 for b in quic_stats['bitrate_std']]
        ax1.plot(times, means, 'r-', linewidth=2, label=f"QUIC (Avg of {quic_stats['count']} runs)")
        if not clean:
            ax1.fill_between(times, 
                            [m - s for m, s in zip(means, stds)],
                            [m + s for m, s in zip(means, stds)],
                            color='blue', alpha=0.2)

    if tcp_stats['bitrate_mean']:
        times = tcp_stats['bitrate_time']
        means = [b/1e6 for b in tcp_stats['bitrate_mean']]
        stds = [b/1e6 for b in tcp_stats['bitrate_std']]
        ax1.plot(times, means, 'b-', linewidth=2, label=f"TCP (Avg of {tcp_stats['count']} runs)")
        if not clean:
            ax1.fill_between(times, 
                            [m - s for m, s in zip(means, stds)],
                            [m + s for m, s in zip(means, stds)],
                            color='red', alpha=0.2)
                        
    ax1.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax1.set_title('Average Bitrate Over Time', fontsize=13, fontweight='bold')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    # Ensure x-axis labels are visible
    ax1.tick_params(labelbottom=True)
    
    # Plot 2: QUIC Interruption Probability
    if quic_stats['playback_runs']:
        q_times, q_probs = calculate_interruption_prob(quic_stats['playback_runs'])
        ax2.plot(q_times, q_probs, 'b-', label='Interruption Probability')
        if not clean:
            ax2.fill_between(q_times, 0, q_probs, color='blue', alpha=0.3)
        ax2.set_ylim(0, 1.1)
        ax2.set_title('QUIC: Probability of Interruption', fontsize=13, fontweight='bold')
    else:
        ax2.text(0.5, 0.5, 'No QUIC data', ha='center')
    ax2.set_ylabel('Probability', fontsize=12)
    ax2.grid(True, alpha=0.3)
    ax2.tick_params(labelbottom=True)

    # Plot 3: TCP Interruption Probability
    if tcp_stats['playback_runs']:
        t_times, t_probs = calculate_interruption_prob(tcp_stats['playback_runs'])
        ax3.plot(t_times, t_probs, 'r-', label='Interruption Probability')
        if not clean:
            ax3.fill_between(t_times, 0, t_probs, color='red', alpha=0.3)
        ax3.set_ylim(0, 1.1)
        ax3.set_title('TCP: Probability of Interruption', fontsize=13, fontweight='bold')
    else:
        ax3.text(0.5, 0.5, 'No TCP data', ha='center')
    ax3.set_ylabel('Probability', fontsize=12)
    ax3.set_xlabel('Time (s)', fontsize=12)
    ax3.grid(True, alpha=0.3)
    ax3.tick_params(labelbottom=True)

    plt.tight_layout()
    return fig

def process_runs(runs, protocol_name):
    """Process all runs for a protocol and return aggregated stats."""
    all_bitrates = []
    all_playback = []
    all_throughputs = []
    
    total_play_time = []
    total_inter_time = []
    
    print(f"Processing {len(runs)} {protocol_name} runs...")
    
    for task_id, paths in runs.items():
        # Bitrate & Playback from stdout log
        bitrate = parse_bitrate_log(paths['log'])
        playback = parse_playback_log(paths['log'])
        
        all_bitrates.append(bitrate)
        all_playback.append(playback)
        
        # Throughput from DashClientRx files in run dir
        if paths['dir']:
            prefix = "DashClientRx_UE_" if protocol_name == 'QUIC' else "DashClientRx_TCP_UE_"
            tp = parse_dash_throughput(paths['dir'], prefix)
            all_throughputs.extend(tp)
            
        # Stats
        pt, it, _, _ = calculate_playback_stats(playback)
        total_play_time.append(pt)
        total_inter_time.append(it)
        
    # Aggregate Bitrate
    b_time, b_mean, b_std = aggregate_bitrate(all_bitrates)
    
    return {
        'count': len(runs),
        'bitrate_time': b_time,
        'bitrate_mean': b_mean,
        'bitrate_std': b_std,
        'playback_runs': all_playback,
        'throughputs': all_throughputs,
        'play_times': total_play_time,
        'inter_times': total_inter_time
    }

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    print(f"Searching for runs in: {project_root}")
    
    # Find runs
    # Note: The user might have different job IDs for QUIC and TCP if run separately
    # But usually we compare two different sets of runs.
    # The script assumes we want to find *some* QUIC runs and *some* TCP runs.
    
    # Find runs
    # We want to support both:
    # 1. Job arrays (slurm-JOBID_TASKID.out or quic-JOBID_TASKID.out)
    # 2. Single runs (legacy behavior, e.g. quic_1.out or just logs in current dir)
    
    # We'll search for all .out files and try to classify them
    all_out_files = glob.glob(os.path.join(project_root, "*.out"))
    
    # Group by Job ID if possible
    job_groups = defaultdict(list)
    single_files = []
    
    for f in all_out_files:
        filename = os.path.basename(f)
        # Match NAME-JOBID_TASKID.out
        match = re.search(r"(?:^|[\w-]+-)(\d+)_(\d+)\.out", filename)
        if match:
            job_groups[int(match.group(1))].append((int(match.group(2)), f))
        else:
            single_files.append(f)
            
    quic_job_runs = {}
    tcp_job_runs = {}
    
    # 1. Process Job Arrays
    if job_groups:
        sorted_jobs = sorted(job_groups.keys(), reverse=True)
        for job_id in sorted_jobs:
            # Check first task to guess protocol
            task_id, log_file = job_groups[job_id][0]
            run_dir = os.path.join(project_root, f"run_{task_id}")
            
            is_quic = False
            is_tcp = False
            
            filename = os.path.basename(log_file)
            if "quic" in filename.lower(): is_quic = True
            elif "tcp" in filename.lower(): is_tcp = True
            
            if not is_quic and not is_tcp and os.path.isdir(run_dir):
                if glob.glob(os.path.join(run_dir, "DashClientRx_UE_*.txt")): is_quic = True
                elif glob.glob(os.path.join(run_dir, "DashClientRx_TCP_UE_*.txt")): is_tcp = True
                
            if not is_quic and not is_tcp:
                try:
                    with open(log_file, 'r') as f:
                        content = f.read(1000)
                        if "Quic" in content or "QUIC" in content: is_quic = True
                        elif "Tcp" in content or "TCP" in content: is_tcp = True
                except: pass
                
            if is_quic and not quic_job_runs:
                print(f"Identified Job {job_id} as QUIC (Job Array)")
                for t, l in job_groups[job_id]:
                    quic_job_runs[t] = {'log': l, 'dir': os.path.join(project_root, f"run_{t}")}
            elif is_tcp and not tcp_job_runs:
                print(f"Identified Job {job_id} as TCP (Job Array)")
                for t, l in job_groups[job_id]:
                    tcp_job_runs[t] = {'log': l, 'dir': os.path.join(project_root, f"run_{t}")}

    # 2. Process Single Files (Fallback if no job array found for a protocol)
    if not quic_job_runs:
        # Look for quic*.out or similar in single_files
        for f in single_files:
            if "quic" in os.path.basename(f).lower():
                print(f"Found single QUIC run: {os.path.basename(f)}")
                quic_job_runs[1] = {'log': f, 'dir': project_root}
                break
        # Also check for DashClientRx_UE_*.txt in project root
        if not quic_job_runs and glob.glob(os.path.join(project_root, "DashClientRx_UE_*.txt")):
             print("Found QUIC traces in project root")
             quic_job_runs[1] = {'log': None, 'dir': project_root}

    if not tcp_job_runs:
        for f in single_files:
            if "tcp" in os.path.basename(f).lower():
                print(f"Found single TCP run: {os.path.basename(f)}")
                tcp_job_runs[1] = {'log': f, 'dir': project_root}
                break
        if not tcp_job_runs and glob.glob(os.path.join(project_root, "DashClientRx_TCP_UE_*.txt")):
             print("Found TCP traces in project root")
             tcp_job_runs[1] = {'log': None, 'dir': project_root}
            
    # Process
    quic_stats = process_runs(quic_job_runs, 'QUIC')
    tcp_stats = process_runs(tcp_job_runs, 'TCP')
    
    # Print Summary
    print(f"\n{'='*60}")
    print("SUMMARY STATISTICS")
    print(f"{'='*60}")
    
    for proto, stats in [('QUIC', quic_stats), ('TCP', tcp_stats)]:
        print(f"\n{proto} ({stats['count']} runs):")
        if stats['count'] > 0:
            avg_play = sum(stats['play_times']) / stats['count']
            avg_inter = sum(stats['inter_times']) / stats['count']
            avg_tp = sum(stats['throughputs']) / len(stats['throughputs']) if stats['throughputs'] else 0
            
            print(f"  Avg Playback Time: {avg_play:.2f} s")
            print(f"  Avg Interruption Time: {avg_inter:.2f} s")
            print(f"  Avg Throughput: {avg_tp:.4f} Mbps")
        else:
            print("  No data found.")
            
    # Plot
    print("\nGenerating plots...")
    # Plot Standard
    print("\nGenerating plots...")
    fig = plot_comparison(quic_stats, tcp_stats, clean=False)
    output_file = 'bitrate_playback_comparison.png'
    fig.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to: {output_file}")
    
    # Plot Clean
    fig_clean = plot_comparison(quic_stats, tcp_stats, clean=True)
    output_file_clean = 'bitrate_playback_comparison_clean.png'
    fig_clean.savefig(output_file_clean, dpi=300, bbox_inches='tight')
    print(f"Clean plot saved to: {output_file_clean}")

if __name__ == '__main__':
    main()
