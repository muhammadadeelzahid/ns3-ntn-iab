#!/usr/bin/env python3
"""
Script to plot bitrate over time and video playback/interruption status
from QUIC and TCP job output files.
Supports averaging across multiple runs from Slurm job arrays.
"""

import re
import os
import glob
import subprocess
import argparse
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

def parse_bitrate_log(filename, node_ids=None):
    """
    Parse bitrate information from a log file.
    node_ids: Optional list of node IDs to filter. If None, includes all nodes.
              If provided, aggregates bitrate across specified nodes.
    """
    bitrate_by_node = defaultdict(list)
    if not filename or not os.path.exists(filename):
        return []
        
    with open(filename, 'r') as f:
        for line in f:
            match = re.search(r'(\d+\.?\d*)\s+Node:\s+(\d+)\s+newBitRate:\s+(\d+)', line)
            if match:
                time = float(match.group(1))
                node_id = int(match.group(2))
                bitrate = float(match.group(3))
                
                # Filter by node_ids if provided
                if node_ids is None or node_id in node_ids:
                    bitrate_by_node[node_id].append((time, bitrate))
    
    if not bitrate_by_node:
        return []
    
    # If node_ids specified and multiple nodes, aggregate across nodes
    if node_ids and len(bitrate_by_node) > 1:
        # Aggregate by time bins
        all_times = set()
        for node_data in bitrate_by_node.values():
            for time, _ in node_data:
                all_times.add(time)
        
        if not all_times:
            return []
        
        sorted_times = sorted(all_times)
        aggregated = []
        
        for time in sorted_times:
            # Find bitrate values for each node at this time (or closest before)
            node_bitrates = []
            for node_id, node_data in bitrate_by_node.items():
                # Find closest bitrate value at or before this time
                closest_bitrate = None
                for t, b in node_data:
                    if t <= time:
                        closest_bitrate = b
                    else:
                        break
                if closest_bitrate is not None:
                    node_bitrates.append(closest_bitrate)
            
            if node_bitrates:
                avg_bitrate = sum(node_bitrates) / len(node_bitrates)
                aggregated.append((time, avg_bitrate))
        
        return aggregated
    else:
        # Single node or no filtering - return the data directly
        # If multiple nodes but no filtering requested, aggregate all
        if len(bitrate_by_node) > 1:
            # Aggregate all nodes
            all_times = set()
            for node_data in bitrate_by_node.values():
                for time, _ in node_data:
                    all_times.add(time)
            
            sorted_times = sorted(all_times)
            aggregated = []
            
            for time in sorted_times:
                node_bitrates = []
                for node_data in bitrate_by_node.values():
                    closest_bitrate = None
                    for t, b in node_data:
                        if t <= time:
                            closest_bitrate = b
                        else:
                            break
                    if closest_bitrate is not None:
                        node_bitrates.append(closest_bitrate)
                
                if node_bitrates:
                    avg_bitrate = sum(node_bitrates) / len(node_bitrates)
                    aggregated.append((time, avg_bitrate))
            
            return aggregated
        else:
            # Single node
            first_node_data = list(bitrate_by_node.values())[0]
            first_node_data.sort(key=lambda x: x[0])
            return first_node_data

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

def parse_client_rx_data(file_path):
    """
    Parse clientQUIC-rx-data or clientTCP-rx-data files.
    Returns list of (timestamp, packet_size) tuples.
    """
    data = []
    if not file_path or not os.path.exists(file_path):
        return data
        
    try:
        with open(file_path, 'r') as f:
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 2:
                    try:
                        timestamp = float(parts[0])
                        packet_size = int(parts[1])
                        data.append((timestamp, packet_size))
                    except ValueError:
                        continue
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        
    return data

def parse_multi_user_rx_data(run_dir, protocol_name, client_nodes):
    """
    Parse and aggregate data from multiple user data files.
    protocol_name: 'QUIC' or 'TCP'
    client_nodes: list of client node IDs (e.g., [2, 3, 4])
    Returns aggregated time series data averaged across users.
    """
    all_user_data = []
    
    for client_node in client_nodes:
        if protocol_name == "QUIC":
            filename = f"clientQUIC-rx-data{client_node}.txt"
        else:
            filename = f"clientTCP-rx-data{client_node}.txt"
            
        file_path = os.path.join(run_dir, filename)
        user_data = parse_client_rx_data(file_path)
        if user_data:
            all_user_data.append(user_data)
    
    if not all_user_data:
        return []
    
    # Aggregate data across users using time bins
    # Find global time range
    min_time = float('inf')
    max_time = float('-inf')
    for user_data in all_user_data:
        if user_data:
            min_time = min(min_time, user_data[0][0])
            max_time = max(max_time, user_data[-1][0])
    
    if min_time == float('inf'):
        return []
    
    # Bin data and calculate average throughput per bin
    time_bin = 0.5
    bins = np.arange(min_time, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin/2
    
    aggregated_data = []
    for i, bin_end in enumerate(bins[1:]):
        bin_start = bins[i]
        bin_throughputs = []
        
        for user_data in all_user_data:
            # Calculate bytes in this bin for this user
            bytes_in_bin = 0
            for timestamp, packet_size in user_data:
                if bin_start <= timestamp < bin_end:
                    bytes_in_bin += packet_size
            
            # Calculate throughput in Mbps
            if time_bin > 0:
                throughput_mbps = (bytes_in_bin * 8) / (time_bin * 1e6)
                bin_throughputs.append(throughput_mbps)
        
        if bin_throughputs:
            avg_throughput = sum(bin_throughputs) / len(bin_throughputs)
            aggregated_data.append((bin_centers[i], avg_throughput))
    
    return aggregated_data

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

def truncate_and_pad_playback_prob(times, probs, max_time=60.0, end_time=63.0, time_bin=1.0):
    """
    Truncate playback probability data at max_time and pad missing data before max_time 
    with interruption probability = 1.0 using quantized time bins.
    
    Args:
        times: array/list of time points
        probs: array/list of interruption probabilities (0.0 to 1.0)
        max_time: maximum time to keep data (default 60.0)
        end_time: unused (kept for compatibility)
        time_bin: bin size for quantization (default 1.0)
    
    Returns:
        (truncated_times, truncated_probs) truncated at max_time, with missing data padded with interruption=1.0
    """
    # Check if empty - handle numpy arrays properly
    if times is None or probs is None:
        # Return interruption=1.0 from 0 to max_time using quantized bins
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_probs = np.ones(len(padded_times))  # Missing = interrupted
        return padded_times.tolist(), padded_probs.tolist()
    
    # Convert to numpy arrays first to check length
    times = np.array(times)
    probs = np.array(probs)
    
    if len(times) == 0 or len(probs) == 0:
        # Return interruption=1.0 from 0 to max_time using quantized bins
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_probs = np.ones(len(padded_times))  # Missing = interrupted
        return padded_times.tolist(), padded_probs.tolist()
    
    # Handle mismatched lengths - truncate to minimum length
    min_len = min(len(times), len(probs))
    times = times[:min_len]
    probs = probs[:min_len]
    
    # Truncate at max_time (keep only data <= max_time)
    mask = times <= max_time
    truncated_times = times[mask]
    truncated_probs = probs[mask]
    
    # If no data, return interruption=1.0 from 0 to max_time
    if len(truncated_times) == 0:
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_probs = np.ones(len(padded_times))  # Missing = interrupted
        return padded_times.tolist(), padded_probs.tolist()
    
    # Pad missing data from 0 to first data point with interruption=1.0 using quantized bins
    first_data_time = truncated_times[0]
    if first_data_time > 0:
        # Create quantized time bins from 0 to first_data_time
        padding_times = np.arange(0, first_data_time, time_bin)
        if len(padding_times) > 0:
            padding_probs = np.ones(len(padding_times))  # Missing = interrupted
            truncated_times = np.concatenate([padding_times, truncated_times])
            truncated_probs = np.concatenate([padding_probs, truncated_probs])
    
    # Pad missing data between last data point and max_time with interruption=1.0 using quantized bins
    last_data_time = truncated_times[-1]
    if last_data_time < max_time:
        # Create quantized time bins from last_data_time to max_time
        pad_start = np.ceil(last_data_time / time_bin) * time_bin
        if pad_start <= max_time:
            padding_times = np.arange(pad_start, max_time + time_bin, time_bin)
            padding_times = padding_times[padding_times <= max_time]
            if len(padding_times) > 0:
                padding_probs = np.ones(len(padding_times))  # Missing = interrupted
                truncated_times = np.concatenate([truncated_times, padding_times])
                truncated_probs = np.concatenate([truncated_probs, padding_probs])
    
    # Ensure we have a point exactly at max_time if needed
    if truncated_times[-1] < max_time:
        truncated_times = np.append(truncated_times, max_time)
        truncated_probs = np.append(truncated_probs, 1.0)  # Missing = interrupted
    
    return truncated_times.tolist(), truncated_probs.tolist()

def truncate_and_pad_data(times, values, max_time=60.0, end_time=63.0, time_bin=0.5):
    """
    Truncate data at max_time and pad missing data before max_time with zeros using quantized time bins.
    
    Args:
        times: array/list of time points
        values: array/list of corresponding values
        max_time: maximum time to keep data (default 60.0)
        end_time: unused (kept for compatibility)
        time_bin: bin size for quantization (default 0.5)
    
    Returns:
        (truncated_times, truncated_values) truncated at max_time, with missing data padded with zeros
    """
    # Check if empty - handle numpy arrays properly
    if times is None or values is None:
        # Return zeros from 0 to max_time using quantized bins
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_values = np.zeros(len(padded_times))
        return padded_times.tolist(), padded_values.tolist()
    
    # Convert to numpy arrays first to check length
    times = np.array(times)
    values = np.array(values)
    
    if len(times) == 0 or len(values) == 0:
        # Return zeros from 0 to max_time using quantized bins
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_values = np.zeros(len(padded_times))
        return padded_times.tolist(), padded_values.tolist()
    
    # Handle mismatched lengths - truncate to minimum length
    min_len = min(len(times), len(values))
    times = times[:min_len]
    values = values[:min_len]
    
    # Truncate at max_time (keep only data <= max_time)
    mask = times <= max_time
    truncated_times = times[mask]
    truncated_values = values[mask]
    
    # If no data, return zeros from 0 to max_time
    if len(truncated_times) == 0:
        padded_times = np.arange(0, max_time + time_bin, time_bin)
        padded_times = padded_times[padded_times <= max_time]
        padded_values = np.zeros(len(padded_times))
        return padded_times.tolist(), padded_values.tolist()
    
    # Pad missing data from 0 to first data point with zeros using quantized bins
    first_data_time = truncated_times[0]
    if first_data_time > 0:
        # Create quantized time bins from 0 to first_data_time
        padding_times = np.arange(0, first_data_time, time_bin)
        if len(padding_times) > 0:
            padding_values = np.zeros(len(padding_times))
            truncated_times = np.concatenate([padding_times, truncated_times])
            truncated_values = np.concatenate([padding_values, truncated_values])
    
    # Pad missing data between last data point and max_time with zeros using quantized bins
    last_data_time = truncated_times[-1]
    if last_data_time < max_time:
        # Create quantized time bins from last_data_time to max_time
        pad_start = np.ceil(last_data_time / time_bin) * time_bin
        if pad_start <= max_time:
            padding_times = np.arange(pad_start, max_time + time_bin, time_bin)
            padding_times = padding_times[padding_times <= max_time]
            if len(padding_times) > 0:
                padding_values = np.zeros(len(padding_times))
                truncated_times = np.concatenate([truncated_times, padding_times])
                truncated_values = np.concatenate([truncated_values, padding_values])
    
    # Ensure we have a point exactly at max_time if needed
    if truncated_times[-1] < max_time:
        truncated_times = np.append(truncated_times, max_time)
        truncated_values = np.append(truncated_values, 0.0)
    
    return truncated_times.tolist(), truncated_values.tolist()

def plot_single_algo(stats, algo_name, output_dir):
    """Create plots for a single algorithm."""
    if not stats['bitrate_mean']:
        print(f"Skipping plots for {algo_name} (no bitrate data)")
        return

    # Determine max time
    max_time = 0
    if len(stats['bitrate_time']) > 0:
        max_time = stats['bitrate_time'][-1]
        
    if stats['playback_runs']:
        for run in stats['playback_runs']:
            if run:
                max_time = max(max_time, run[-1][1])
    
    plot_max_time = min(60, max_time) if max_time > 0 else 60

    # Bitrate Plot
    fig, ax = plt.subplots(figsize=(10, 6))
    times = stats['bitrate_time']
    means = [b/1e6 for b in stats['bitrate_mean']]
    stds = [b/1e6 for b in stats['bitrate_std']]
    
    # Truncate at T=60 and pad missing data with zeros using quantized bins
    # Save original times for stds processing
    times_orig = times
    times, means = truncate_and_pad_data(times, means, max_time=60.0, end_time=63.0)
    # Use original times for stds, then align to the padded times from means
    times_std, stds = truncate_and_pad_data(times_orig, stds, max_time=60.0, end_time=63.0)
    
    # Ensure lengths match - use times from means and interpolate/pad stds if needed
    times = np.array(times)
    means = np.array(means)
    stds = np.array(stds)
    
    if len(times) != len(times_std) or not np.allclose(times, times_std):
        # Use times from means and interpolate stds to match
        if len(stds) > 0 and len(times_std) > 0:
            stds = np.interp(times, times_std, stds)
        else:
            stds = np.zeros(len(times))
    
    # Convert back to lists for consistency
    times = times.tolist()
    means = means.tolist()
    stds = stds.tolist()
    
    ax.plot(times, means, 'b-', linewidth=2, label=f"{algo_name} (Avg)")
    ax.fill_between(times, 
                    [m - s for m, s in zip(means, stds)],
                    [m + s for m, s in zip(means, stds)],
                    color='blue', alpha=0.2)
    
    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title(f'{algo_name}: Average Bitrate Over Time', fontsize=14, fontweight='bold')
    ax.set_xlim(0, 60)
    ax.grid(True, alpha=0.3)
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
    filename = f"bitrate_{algo_name.replace(' ', '_')}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")
    
    # Playback Interruption Plot
    if stats['playback_runs']:
        fig, ax = plt.subplots(figsize=(10, 6))
        times, probs = calculate_interruption_prob(stats['playback_runs'])
        probs = smooth_data(probs, window_size=5)
        
        # Truncate at T=60 and pad missing data with interruption probability = 1.0 using quantized bins
        times, probs = truncate_and_pad_playback_prob(times, probs, max_time=60.0, end_time=63.0, time_bin=1.0)
        
        playing_probs = [1.0 - p for p in probs]
        
        ax.fill_between(times, 0, playing_probs, color='green', alpha=0.5, label='Playing')
        ax.fill_between(times, playing_probs, 1.0, color='red', alpha=0.5, label='Interrupted')
        
        ax.set_xlabel('Time (s)', fontsize=12)
        ax.set_ylabel('Probability', fontsize=12)
        ax.set_ylim(0, 1.0)
        ax.set_xlim(0, 60)
        ax.set_title(f'{algo_name}: Playback Status', fontsize=14, fontweight='bold')
        ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
        
        filename = f"playback_{algo_name.replace(' ', '_')}.png"
        fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
        plt.close(fig)
        print(f"Saved {filename}")

def plot_combined_bitrate(all_stats, protocol_type, output_dir):
    """Create combined bitrate plot for a protocol type (QUIC or TCP)."""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    has_data = False
    max_time = 0
    
    for name, stats in all_stats.items():
        if protocol_type in name and stats['bitrate_mean']:
            has_data = True
            times = stats['bitrate_time']
            means = [b/1e6 for b in stats['bitrate_mean']]
            
            # Truncate at T=60 and pad missing data with zeros using quantized bins and pad missing data with interruption probability = 1.0 using quantized bins and pad missing data with zeros using quantized bins
            times, means = truncate_and_pad_data(times, means, max_time=60.0, end_time=63.0)
            
            ax.plot(times, means, linewidth=2, label=name)
            
    if not has_data:
        plt.close(fig)
        return

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title(f'{protocol_type}: Combined Bitrate Comparison', fontsize=14, fontweight='bold')
    ax.set_xlim(0, 60)
    ax.grid(True, alpha=0.3)
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
    filename = f"bitrate_combined_{protocol_type}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def plot_combined_playback(all_stats, protocol_type, output_dir):
    """Create combined playback interruption plot for a protocol type."""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    has_data = False
    max_time = 0
    
    for name, stats in all_stats.items():
        if protocol_type in name and stats['playback_runs']:
            has_data = True
            times, probs = calculate_interruption_prob(stats['playback_runs'])
            probs = smooth_data(probs, window_size=5)
            
            # Truncate at T=60 and pad missing data with zeros using quantized bins and pad missing data with interruption probability = 1.0 using quantized bins and pad to T=63 with interruption probability = 1.0 for missing times
            # Uses quantized time bins
            times, probs = truncate_and_pad_playback_prob(times, probs, max_time=60.0, end_time=63.0, time_bin=1.0)
            
            # Plot interruption probability line
            ax.plot(times, probs, linewidth=2, label=name)
            
    if not has_data:
        plt.close(fig)
        return

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Interruption Probability', fontsize=12)
    ax.set_ylim(0, 1.0)
    ax.set_xlim(0, 60)
    ax.set_title(f'{protocol_type}: Combined Playback Interruption Probability', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
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

def process_runs(runs, protocol_name, log_mapping, client_nodes=None):
    """
    Process all runs for a protocol and return aggregated stats.
    client_nodes: Optional list of client node IDs. If provided, bitrate will be aggregated across these nodes.
    """
    all_bitrates = []
    all_playback = []
    all_throughputs = []
    
    total_play_time = []
    total_inter_time = []
    
    print(f"Processing {len(runs)} runs for {protocol_name}...")
    if client_nodes:
        print(f"  Multi-user mode: Aggregating across client nodes {client_nodes}")
    
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
            # Parse bitrate with optional node filtering
            bitrate = parse_bitrate_log(log_file, node_ids=client_nodes)
            
            # Also try to parse throughput from client data files if client_nodes provided
            if client_nodes:
                throughput_data = parse_multi_user_rx_data(run_dir, protocol_name, client_nodes)
                # Note: throughput_data is in (time, throughput_mbps) format
                # We could add this to the stats if needed
            
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

def plot_all_combined_bitrate(run_groups, log_mapping, output_dir, client_nodes=None):
    """
    Create a plot combining ALL TCP congestion algorithms and ALL QUIC congestion algorithms.
    Aggregates raw bitrate data from all runs across all algorithms.
    client_nodes: Optional list of client node IDs to filter/aggregate bitrate.
    """
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Collect all raw bitrate data from all QUIC runs
    all_quic_bitrates = []
    all_quic_playback = []
    
    # Collect all raw bitrate data from all TCP runs
    all_tcp_bitrates = []
    all_tcp_playback = []
    
    print("Collecting all bitrate data for combined plot...")
    
    # Process all QUIC algorithms
    for name, runs in run_groups.items():
        if "QUIC" in name:
            for run_dir in runs:
                log_file = log_mapping.get(run_dir)
                if log_file:
                    bitrate = parse_bitrate_log(log_file, node_ids=client_nodes)
                    if bitrate:
                        all_quic_bitrates.append(bitrate)
    
    # Process all TCP algorithms
    for name, runs in run_groups.items():
        if "TCP" in name:
            for run_dir in runs:
                log_file = log_mapping.get(run_dir)
                if log_file:
                    bitrate = parse_bitrate_log(log_file, node_ids=client_nodes)
                    if bitrate:
                        all_tcp_bitrates.append(bitrate)
    
    max_time = 0
    
    # Aggregate QUIC bitrate data
    if all_quic_bitrates:
        # Calculate global max time from playback data if available
        global_max_time = 0
        for bitrate_data in all_quic_bitrates:
            if bitrate_data:
                global_max_time = max(global_max_time, bitrate_data[-1][0])
        
        quic_time, quic_mean, quic_std = aggregate_bitrate(all_quic_bitrates, global_max_time=global_max_time)
        
        if len(quic_time) > 0 and len(quic_mean) > 0:
            # Truncate at T=60 and pad missing data with zeros using quantized bins
            quic_time_orig = quic_time
            quic_time, quic_mean = truncate_and_pad_data(quic_time, quic_mean, max_time=60.0, end_time=63.0)
            quic_time_std, quic_std = truncate_and_pad_data(quic_time_orig, quic_std, max_time=60.0, end_time=63.0)
            
            # Ensure lengths match
            quic_time = np.array(quic_time)
            quic_mean = np.array(quic_mean)
            quic_std = np.array(quic_std)
            
            if len(quic_time) != len(quic_time_std) or not np.allclose(quic_time, quic_time_std):
                if len(quic_std) > 0 and len(quic_time_std) > 0:
                    quic_std = np.interp(quic_time, quic_time_std, quic_std)
                else:
                    quic_std = np.zeros(len(quic_time))
            
            ax.plot(quic_time, [b/1e6 for b in quic_mean], 'r-', linewidth=3, label=f'QUIC (All Algorithms, {len(all_quic_bitrates)} runs)')
            ax.fill_between(quic_time,
                           [b/1e6 for b in [m - s for m, s in zip(quic_mean, quic_std)]],
                           [b/1e6 for b in [m + s for m, s in zip(quic_mean, quic_std)]],
                           color='red', alpha=0.2)
    
    # Aggregate TCP bitrate data
    if all_tcp_bitrates:
        # Calculate global max time from playback data if available
        global_max_time = 0
        for bitrate_data in all_tcp_bitrates:
            if bitrate_data:
                global_max_time = max(global_max_time, bitrate_data[-1][0])
        
        tcp_time, tcp_mean, tcp_std = aggregate_bitrate(all_tcp_bitrates, global_max_time=global_max_time)
        
        if len(tcp_time) > 0 and len(tcp_mean) > 0:
            # Truncate at T=60 and pad missing data with zeros using quantized bins
            tcp_time_orig = tcp_time
            tcp_time, tcp_mean = truncate_and_pad_data(tcp_time, tcp_mean, max_time=60.0, end_time=63.0)
            tcp_time_std, tcp_std = truncate_and_pad_data(tcp_time_orig, tcp_std, max_time=60.0, end_time=63.0)
            
            # Ensure lengths match
            tcp_time = np.array(tcp_time)
            tcp_mean = np.array(tcp_mean)
            tcp_std = np.array(tcp_std)
            
            if len(tcp_time) != len(tcp_time_std) or not np.allclose(tcp_time, tcp_time_std):
                if len(tcp_std) > 0 and len(tcp_time_std) > 0:
                    tcp_std = np.interp(tcp_time, tcp_time_std, tcp_std)
                else:
                    tcp_std = np.zeros(len(tcp_time))
            
            ax.plot(tcp_time, [b/1e6 for b in tcp_mean], 'b-', linewidth=3, label=f'TCP (All Algorithms, {len(all_tcp_bitrates)} runs)')
            ax.fill_between(tcp_time,
                           [b/1e6 for b in [m - s for m, s in zip(tcp_mean, tcp_std)]],
                           [b/1e6 for b in [m + s for m, s in zip(tcp_mean, tcp_std)]],
                           color='blue', alpha=0.2)
    
    if not all_quic_bitrates and not all_tcp_bitrates:
        plt.close(fig)
        print("No bitrate data found for combined plot.")
        return

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title('Combined Bitrate: All TCP vs All QUIC Algorithms', fontsize=14, fontweight='bold')
    ax.set_xlim(0, 60)
    ax.grid(True, alpha=0.3)
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
    filename = "bitrate_all_combined.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def plot_grand_combined_bitrate(all_stats, output_dir):
    """Create a Grand Combined Bitrate plot (QUIC vs TCP)."""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Aggregate QUIC
    quic_means = []
    quic_times = []
    for name, stats in all_stats.items():
        if "QUIC" in name and stats['bitrate_mean']:
            quic_means.append(stats['bitrate_mean'])
            quic_times.append(stats['bitrate_time'])
            
    # Aggregate TCP
    tcp_means = []
    tcp_times = []
    for name, stats in all_stats.items():
        if "TCP" in name and stats['bitrate_mean']:
            tcp_means.append(stats['bitrate_mean'])
            tcp_times.append(stats['bitrate_time'])
            
    max_time = 0
    
    if quic_means:
        # We need to align time series to average them. 
        # For simplicity, let's assume they are roughly on the same grid or just plot the average of the means if they align.
        # But 'aggregate_bitrate' already aligns them.
        # So we can just re-aggregate the means?
        # Or better, we can just plot the "Mean of Means".
        
        # Let's use the first time array as reference (assuming they are similar enough due to common sampling)
        # Or re-bin.
        # Since 'aggregate_bitrate' returns a common time axis for a set of runs, 
        # and we used it for each algo, the time axes might differ slightly between algos.
        # But usually they are 0.5s bins.
        
        # Let's try to aggregate the 'means' lists.
        # We can use the 'aggregate_bitrate' function again but treating the means as single runs?
        # No, 'aggregate_bitrate' expects raw logs.
        
        # Let's just plot the average of the available means.
        # We need a common time axis.
        t_max = 0
        for t in quic_times:
            if len(t) > 0: t_max = max(t_max, t[-1])
        for t in tcp_times:
            if len(t) > 0: t_max = max(t_max, t[-1])
            
        max_time = max(max_time, t_max)
        
        # Create common time axis up to 60
        common_times = np.arange(0, min(60.0, t_max) + 0.5, 0.5)
        
        # Interpolate and Average QUIC
        quic_interp = []
        for t, m in zip(quic_times, quic_means):
            if len(t) > 0 and len(m) > 0:
                quic_interp.append(np.interp(common_times, t, m))
        
        if quic_interp:
            avg_quic = np.mean(quic_interp, axis=0)
            # Truncate and pad
            common_times, avg_quic = truncate_and_pad_data(common_times.tolist(), avg_quic.tolist(), max_time=60.0, end_time=63.0)
            ax.plot(common_times, [b/1e6 for b in avg_quic], 'r-', linewidth=3, label='QUIC (Combined)')
            
        # Interpolate and Average TCP
        tcp_interp = []
        common_times = np.arange(0, min(60.0, t_max) + 0.5, 0.5)
        for t, m in zip(tcp_times, tcp_means):
            if len(t) > 0 and len(m) > 0:
                tcp_interp.append(np.interp(common_times, t, m))
                
        if tcp_interp:
            avg_tcp = np.mean(tcp_interp, axis=0)
            # Truncate and pad
            common_times, avg_tcp = truncate_and_pad_data(common_times.tolist(), avg_tcp.tolist(), max_time=60.0, end_time=63.0)
            ax.plot(common_times, [b/1e6 for b in avg_tcp], 'b-', linewidth=3, label='TCP (Combined)')

    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Bitrate (Mbps)', fontsize=12)
    ax.set_title('Grand Combined Bitrate Comparison (QUIC vs TCP)', fontsize=14, fontweight='bold')
    ax.set_xlim(0, 60)
    ax.grid(True, alpha=0.3)
    ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
    filename = "bitrate_grand_combined.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def plot_playback_grid(all_stats, protocol_type, output_dir):
    """Create a grid of playback interruption plots for a protocol type."""
    # Filter algos
    algos = [name for name in all_stats.keys() if protocol_type in name and all_stats[name]['playback_runs']]
    if not algos:
        return

    num_plots = len(algos)
    cols = 2
    rows = (num_plots + 1) // cols
    
    fig, axes = plt.subplots(rows, cols, figsize=(15, 5 * rows))
    axes = axes.flatten()
    
    max_time = 0
    
    for i, name in enumerate(algos):
        ax = axes[i]
        stats = all_stats[name]
        
        times, probs = calculate_interruption_prob(stats['playback_runs'])
        probs = smooth_data(probs, window_size=5)
        
        # Truncate at T=60 and pad missing data with interruption probability = 1.0 using quantized bins
        times, probs = truncate_and_pad_playback_prob(times, probs, max_time=60.0, end_time=63.0, time_bin=1.0)
        
        playing_probs = [1.0 - p for p in probs]
        
        ax.fill_between(times, 0, playing_probs, color='green', alpha=0.5, label='Playing')
        ax.fill_between(times, playing_probs, 1.0, color='red', alpha=0.5, label='Interrupted')
        
        ax.set_title(name, fontsize=12, fontweight='bold')
        ax.set_ylim(0, 1.0)
        ax.set_xlabel('Time (s)', fontsize=10)
        ax.set_xlim(0, 60)
        ax.grid(True, alpha=0.3)
            
    # Hide unused subplots
    for i in range(num_plots, len(axes)):
        axes[i].axis('off')
    
    # Global legend
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, bbox_to_anchor=(1.05, 1), loc='upper left')
    
    fig.suptitle(f'{protocol_type}: Playback Status Grid', fontsize=16, fontweight='bold')
    # Remove shared labels since we have individual ones
    # fig.text(0.5, 0.04, 'Time (s)', ha='center', fontsize=12)
    fig.text(0.04, 0.5, 'Probability', va='center', rotation='vertical', fontsize=12)
    
    filename = f"playback_grid_{protocol_type}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved {filename}")

def main():
    parser = argparse.ArgumentParser(description='Plot bitrate and playback status from simulation logs')
    parser.add_argument('--client_nodes', type=str, default=None, 
                       help='Comma-separated list of client node IDs (e.g., "2,3,4") or range (e.g., "2:4" for 2,3,4). If provided, bitrate will be averaged across these users.')
    
    args = parser.parse_args()
    
    # Parse client_nodes if provided
    client_nodes = None
    if args.client_nodes:
        if ':' in args.client_nodes:
            # Range format: "2:4" means 2,3,4
            parts = args.client_nodes.split(':')
            if len(parts) == 2:
                start, end = int(parts[0]), int(parts[1])
                client_nodes = list(range(start, end + 1))
            else:
                print(f"Warning: Invalid range format '{args.client_nodes}'. Ignoring.")
        else:
            # Comma-separated format: "2,3,4"
            try:
                client_nodes = [int(x.strip()) for x in args.client_nodes.split(',')]
            except ValueError:
                print(f"Warning: Invalid client_nodes format '{args.client_nodes}'. Ignoring.")
    
    if client_nodes:
        print(f"Multi-user mode: Processing client nodes {client_nodes}")
    
    print("=" * 60)
    print("Starting bitrate and playback analysis...")
    print("=" * 60)
    import sys
    sys.stdout.flush()
    
    run_groups = find_all_runs()
    
    if not run_groups:
        print("No run directories found in Quic_artifacts or Tcp_artifacts.")
        return
        
    print(f"Found algorithms: {list(run_groups.keys())}")
    sys.stdout.flush()
    
    # Map logs
    print("\nMapping log files to runs...")
    log_mapping = map_logs_to_runs()
    sys.stdout.flush()
    
    all_stats = {}
    
    print(f"\nProcessing {len(run_groups)} algorithm groups...")
    sys.stdout.flush()
    
    for name, runs in run_groups.items():
        all_stats[name] = process_runs(runs, name, log_mapping, client_nodes=client_nodes)
        sys.stdout.flush()
        
    # Create output directory
    output_dir = "Analysis_artifacts"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")
        
    print("\nGenerating Plots...")
    sys.stdout.flush()
    
    # 1. Individual Plots
    for name, stats in all_stats.items():
        plot_single_algo(stats, name, output_dir)
        
    # 2. Combined QUIC Plots
    plot_combined_bitrate(all_stats, "QUIC", output_dir)
    plot_combined_playback(all_stats, "QUIC", output_dir)
    
    # 3. Combined TCP Plots
    plot_combined_bitrate(all_stats, "TCP", output_dir)
    plot_combined_playback(all_stats, "TCP", output_dir)

    # 4. Grand Combined Plots
    plot_grand_combined_bitrate(all_stats, output_dir)
    
    # 5. All Combined Plot (aggregates raw data from all runs)
    plot_all_combined_bitrate(run_groups, log_mapping, output_dir, client_nodes=client_nodes)
    
    # 6. Playback Grid Plots
    plot_playback_grid(all_stats, "QUIC", output_dir)
    plot_playback_grid(all_stats, "TCP", output_dir)
    
    print(f"\nAll plots saved to {output_dir}")
    print("=" * 60)
    print("Analysis complete!")
    print("=" * 60)
    sys.stdout.flush()

if __name__ == '__main__':
    main()
