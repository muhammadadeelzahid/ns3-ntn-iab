#!/usr/bin/env python
"""
Script to plot bitrate over time and video playback/interruption status
from QUIC and TCP job output files.
Supports averaging across multiple runs from Slurm job arrays.
"""

import re
import os
import glob
import subprocess
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
from collections import defaultdict

def find_all_runs():
    """
    Find all runs in Quic_artifacts and Tcp_artifacts.
    Returns a dict: { 'Algorithm Name': [list of run directories] }
    """
    run_groups = defaultdict(list)
    project_root = os.getcwd()
    
    # Check Quic_artifacts
    if os.path.exists("Quic_artifacts"):
        for algo in os.listdir("Quic_artifacts"):
            algo_path = os.path.join("Quic_artifacts", algo)
            if os.path.isdir(algo_path):
                # Find run_* directories
                runs = glob.glob(os.path.join(algo_path, "run_*"))
                if runs:
                    name = f"QUIC {algo}"
                    # Store absolute paths
                    run_groups[name].extend([os.path.abspath(r) for r in runs])

    # Check Tcp_artifacts
    if os.path.exists("Tcp_artifacts"):
        for algo in os.listdir("Tcp_artifacts"):
            algo_path = os.path.join("Tcp_artifacts", algo)
            if os.path.isdir(algo_path):
                # Find run_* directories
                runs = glob.glob(os.path.join(algo_path, "run_*"))
                if runs:
                    name = f"TCP {algo}"
                    # Store absolute paths
                    run_groups[name].extend([os.path.abspath(r) for r in runs])
                    
    return run_groups

def map_logs_to_runs():
    """
    Scan all .out files in the current directory to map them to run directories.
    Returns dict: { run_dir_abs_path: log_file_abs_path }
    """
    mapping = {}
    log_files = glob.glob("*.out")
    project_root = os.getcwd()
    
    print(f"Scanning {len(log_files)} log files for run mapping...")
    
    for log_file in log_files:
        try:
            with open(log_file, 'r') as f:
                first_line = f.readline()
                # Look for: Running <Algo> Run <ID> (Task <ID>) in <Dir>
                # Example: Running ns3::TcpCubic Run 1 (Task 1) in Tcp_artifacts/Cubic/run_1
                match = re.search(r"Running .* in (.*)", first_line)
                if match:
                    run_dir_rel = match.group(1).strip()
                    run_dir_abs = os.path.abspath(run_dir_rel)
                    log_abs = os.path.abspath(log_file)
                    mapping[run_dir_abs] = log_abs
        except Exception as e:
            # print(f"Error reading {log_file}: {e}")
            pass
            
    print(f"Mapped {len(mapping)} runs to log files.")
    return mapping

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
        
    # Optimization: Use grep to filter relevant lines from large log files
    cmd = f"grep -a -E 'PLAYING FRAME|No frames to play|Play resumed' {filename}"
    try:
        output = subprocess.check_output(cmd, shell=True).decode('utf-8', errors='replace')
        lines = output.splitlines()
    except subprocess.CalledProcessError:
        return playback_events
    except Exception as e:
        print(f"Error reading log {filename}: {e}")
        return playback_events
    
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

def aggregate_bitrate(all_runs_bitrate, time_bin=1.0, global_max_time=None):
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
            
    if global_max_time is not None:
        max_time = max(max_time, global_max_time)
            
    if min_time == float('inf'):
        return [], [], []
        
    bins = np.arange(min_time, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin/2
    
    binned_values = defaultdict(list)
    
    for run_data in all_runs_bitrate:
        # For each run, sample the bitrate at each bin
        # Simple approach: take the last bitrate value before or within the bin
        
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

def smooth_data(data, window_size=5):
    """Apply moving average smoothing."""
    if not data or len(data) < window_size:
        return data
    return np.convolve(data, np.ones(window_size)/window_size, mode='same')

def plot_single_algo(stats, algo_name, output_dir):
    """Create plots for a single algorithm."""
    if not stats['bitrate_mean']:
        print(f"Skipping plots for {algo_name} (no bitrate data)")
        return

    # Bitrate Plot
    fig, ax = plt.subplots(figsize=(10, 6))
    times = stats['bitrate_time']
    means = [b/1e6 for b in stats['bitrate_mean']]
    stds = [b/1e6 for b in stats['bitrate_std']]
    
    ax.plot(times, means, 'b-', linewidth=2, label=f"{algo_name} (Avg)")
    ax.fill_between(times, 
                    [m - s for m, s in zip(means, stds)],
                    [m + s for m, s in zip(means, stds)],
                    color='blue', alpha=0.2)
    
    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title(f'{algo_name}: Average Bitrate Over Time', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend()
    
    filename = f"bitrate_{algo_name.replace(' ', '_')}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")
    
    # Playback Interruption Plot
    if stats['playback_runs']:
        fig, ax = plt.subplots(figsize=(10, 6))
        times, probs = calculate_interruption_prob(stats['playback_runs'])
        probs = smooth_data(probs, window_size=5)
        playing_probs = [1.0 - p for p in probs]
        
        ax.fill_between(times, 0, playing_probs, color='green', alpha=0.5, label='Playing')
        ax.fill_between(times, playing_probs, 1.0, color='red', alpha=0.5, label='Interrupted')
        
        ax.set_xlabel('Time (s)', fontsize=12)
        ax.set_ylabel('Probability', fontsize=12)
        ax.set_ylim(0, 1.0)
        ax.set_title(f'{algo_name}: Playback Status', fontsize=14, fontweight='bold')
        ax.legend(loc='center right')
        
        filename = f"playback_{algo_name.replace(' ', '_')}.png"
        fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
        plt.close(fig)
        print(f"Saved {filename}")

def plot_combined_bitrate(all_stats, protocol_type, output_dir):
    """Create combined bitrate plot for a protocol type (QUIC or TCP)."""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    has_data = False
    for name, stats in all_stats.items():
        if protocol_type in name and stats['bitrate_mean']:
            has_data = True
            times = stats['bitrate_time']
            means = [b/1e6 for b in stats['bitrate_mean']]
            ax.plot(times, means, linewidth=2, label=name)
            
    if not has_data:
        plt.close(fig)
        return

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title(f'{protocol_type}: Combined Bitrate Comparison', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend()
    
    filename = f"bitrate_combined_{protocol_type}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def plot_combined_playback(all_stats, protocol_type, output_dir):
    """Create combined playback interruption plot for a protocol type."""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    has_data = False
    for name, stats in all_stats.items():
        if protocol_type in name and stats['playback_runs']:
            has_data = True
            times, probs = calculate_interruption_prob(stats['playback_runs'])
            probs = smooth_data(probs, window_size=5)
            # Plot interruption probability line
            ax.plot(times, probs, linewidth=2, label=name)
            
    if not has_data:
        plt.close(fig)
        return

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Interruption Probability', fontsize=12)
    ax.set_ylim(0, 1.0)
    ax.set_title(f'{protocol_type}: Combined Playback Interruption Probability', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend()
    
    filename = f"playback_combined_{protocol_type}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def parse_playback_logs_bulk(log_files):
    """
    Parse playback logs for multiple files using a single grep command.
    Returns a dict: { filename: [events] }
    """
    results = defaultdict(list)
    if not log_files:
        return results
        
    # Filter out non-existent files
    valid_files = [f for f in log_files if os.path.exists(f)]
    if not valid_files:
        return results

    print(f"Grepping {len(valid_files)} files for playback events...")
    
    # Use grep with filename output (-H is default for multiple files but good to be explicit)
    # We grep for the patterns
    # Command: grep -a -E 'PLAYING FRAME|No frames to play|Play resumed' file1 file2 ...
    
    # Argument list might be too long, so we might need to batch it.
    # Max arg length is usually huge on Linux, but let's be safe with batches of 50.
    batch_size = 50
    
    for i in range(0, len(valid_files), batch_size):
        batch = valid_files[i:i+batch_size]
        cmd = ["grep", "-a", "-E", "PLAYING FRAME|No frames to play|Play resumed"] + batch
        
        try:
            # Run grep
            # We use subprocess.run to avoid shell=True and quoting issues
            # Capture stdout
            # For Python < 3.7, use stdout=subprocess.PIPE instead of capture_output=True
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            output = proc.stdout
            
            for line in output.splitlines():
                # Output format: filename:line_content
                # We need to split by the first colon
                parts = line.split(':', 1)
                if len(parts) < 2: continue
                
                filename = parts[0]
                content = parts[1]
                
                # Parse content
                play_match = re.search(r'(\d+\.?\d*)\s+PLAYING FRAME:.*?Res:\s+(\d+)', content)
                if play_match:
                    time = float(play_match.group(1))
                    resolution = float(play_match.group(2))
                    results[filename].append(('playing', time, resolution))
                    continue
                
                interrupt_match = re.search(r'(\d+\.?\d*)\s+No frames to play', content)
                if interrupt_match:
                    time = float(interrupt_match.group(1))
                    results[filename].append(('interrupted', time, None))
                    continue
                
                resume_match = re.search(r'Play resumed', content)
                if resume_match:
                    # We need time for resumed? usually it's just a flag.
                    # But we need to associate it with a time.
                    # If the line has a timestamp at start?
                    # "2.345 Play resumed" ?
                    # Let's check the regex in original: re.search(r'Play resumed', line)
                    # It didn't extract time. It just set resume_pending=True.
                    # But we need time to sort events.
                    # The original code iterated lines in order.
                    # Here we get lines in order per file.
                    # So we can just store it as an event without time if we process sequentially later?
                    # Or we try to extract time if present.
                    # If no time, we can't sort it easily if we mix.
                    # But grep output is ordered by line number.
                    # So we can just append to the list and process later.
                    results[filename].append(('resumed', 0, None)) # Time 0 placeholder, order matters
                    
        except Exception as e:
            print(f"Error in grep batch: {e}")
            
    # Post-process to handle 'resumed' logic and sorting
    final_results = {}
    for filename, events in results.items():
        # Events are in order of appearance in file (thanks to grep)
        # We need to handle the 'resumed' logic which depended on state
        
        processed_events = []
        resume_pending = False
        
        for evt in events:
            evt_type = evt[0]
            
            if evt_type == 'resumed':
                resume_pending = True
            elif evt_type == 'playing':
                time = evt[1]
                res = evt[2]
                if resume_pending:
                    processed_events.append(('resumed', time, res))
                    resume_pending = False
                processed_events.append(('playing', time, res))
            elif evt_type == 'interrupted':
                time = evt[1]
                processed_events.append(('interrupted', time, None))
                resume_pending = False
                
        processed_events.sort(key=lambda x: x[1])
        final_results[filename] = processed_events
        
    return final_results

def process_runs(runs, protocol_name, log_mapping):
    """Process all runs for a protocol and return aggregated stats."""
    all_bitrates = []
    all_playback = []
    all_throughputs = []
    
    total_play_time = []
    total_inter_time = []
    
    print(f"Processing {len(runs)} runs for {protocol_name}...")
    
    # 1. Gather all log files we need to parse for playback
    files_to_grep = []
    run_to_log = {}
    
    for run_dir in runs:
        log_file = log_mapping.get(run_dir)
        if log_file:
            # Prefer .err file
            err_file = log_file.replace('.out', '.err')
            if os.path.exists(err_file):
                target_file = err_file
            else:
                target_file = log_file
            
            files_to_grep.append(target_file)
            run_to_log[run_dir] = target_file
            
    # 2. Bulk parse playback logs
    bulk_playback_data = parse_playback_logs_bulk(files_to_grep)
    
    for run_dir in runs:
        # Use mapping to find log file
        log_file = log_mapping.get(run_dir)
        
        bitrate = []
        playback = []
        
        if log_file:
            bitrate = parse_bitrate_log(log_file)
            
            # Get playback from bulk results
            target_file = run_to_log.get(run_dir)
            if target_file and target_file in bulk_playback_data:
                playback = bulk_playback_data[target_file]
        
        all_bitrates.append(bitrate)
        all_playback.append(playback)
        
        # Throughput from DashClientRx files in run dir
        prefix = "DashClientRx_UE_" if "QUIC" in protocol_name else "DashClientRx_TCP_UE_"
        tp = parse_dash_throughput(run_dir, prefix)
        all_throughputs.extend(tp)
            
        # Stats
        pt, it, _, _ = calculate_playback_stats(playback)
        total_play_time.append(pt)
        total_inter_time.append(it)
        
    # Calculate global max time
    global_max_time = 0
    for run in all_playback:
        if run:
            global_max_time = max(global_max_time, run[-1][1])
            
    # Aggregate Bitrate
    b_time, b_mean, b_std = aggregate_bitrate(all_bitrates, global_max_time=global_max_time)
    
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
    run_groups = find_all_runs()
    
    if not run_groups:
        print("No run directories found in Quic_artifacts or Tcp_artifacts.")
        return
        
    print(f"Found algorithms: {list(run_groups.keys())}")
    
    # Map logs
    log_mapping = map_logs_to_runs()
    
    all_stats = {}
    
    for name, runs in run_groups.items():
        all_stats[name] = process_runs(runs, name, log_mapping)
        
    # Create output directory
    output_dir = "Analysis_artifacts"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    print("\nGenerating Plots...")
    
    # 1. Individual Plots
    for name, stats in all_stats.items():
        plot_single_algo(stats, name, output_dir)
        
    # 2. Combined QUIC Plots
    plot_combined_bitrate(all_stats, "QUIC", output_dir)
    plot_combined_playback(all_stats, "QUIC", output_dir)
    
    # 3. Combined TCP Plots
    plot_combined_bitrate(all_stats, "TCP", output_dir)
    plot_combined_playback(all_stats, "TCP", output_dir)
    
    print(f"\nAll plots saved to {output_dir}")

if __name__ == '__main__':
    main()
