#!/usr/bin/env python3
"""
TCP/QUIC Data Analysis Script
Supports averaging across multiple runs from Slurm job arrays.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import argparse
import sys
import csv
from datetime import datetime
import os
import glob
import re
from collections import defaultdict

def read_server_data(file_path):
    try:
        df = pd.read_csv(file_path, sep='\t', header=None)
        if df.shape[1] == 2:
            df.columns = ['timestamp', 'packet_size']
            df['node_id'] = 'N/A'
        elif df.shape[1] >= 3:
            df = df.iloc[:, :3]
            df.columns = ['timestamp', 'packet_size', 'node_id']
        else:
            return None
        return df.dropna()
    except Exception:
        return None

def read_client_data(file_path):
    try:
        df = pd.read_csv(file_path, sep='\t', header=None, names=['timestamp', 'old_cwnd', 'new_cwnd'])
        return df.dropna()
    except Exception:
        return None

def read_rtt_data(file_path):
    try:
        df = pd.read_csv(file_path, sep='\t', header=None, names=['timestamp', 'old_rtt', 'new_rtt'])
        return df.dropna()
    except Exception:
        return None

def calculate_data_rate(df, time_interval=0.5):
    if df is None or df.empty:
        return None, None
    
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    data_rates = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            total_bytes = interval_data['packet_size'].sum()
            data_rate_mbps = (total_bytes * 8) / (time_interval * 1e6)
            data_rates.append(data_rate_mbps)
            time_points.append(start_time + time_interval/2)
    
    return time_points, data_rates

def calculate_cwnd_over_time(df, time_interval=0.5):
    if df is None or df.empty:
        return None, None
    
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    cwnd_values = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            last_cwnd = interval_data['new_cwnd'].iloc[-1]
            cwnd_values.append(last_cwnd)
            time_points.append(start_time + time_interval/2)
            
    return time_points, cwnd_values

def calculate_rtt_over_time(df, time_interval=0.5):
    if df is None or df.empty:
        return None, None
    
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    rtt_values = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            last_rtt = interval_data['new_rtt'].iloc[-1]
            rtt_values.append(last_rtt)
            time_points.append(start_time + time_interval/2)
            
    return time_points, rtt_values

def calculate_total_throughput(df):
    if df is None or df.empty:
        return 0
    total_bytes = df['packet_size'].sum()
    total_time = df['timestamp'].max() - df['timestamp'].min()
    if total_time > 0:
        return (total_bytes * 8) / (total_time * 1e6)
    return 0

def process_protocol_data(protocol_name, server_node, client_node, use_p2p=False, base_dir='.'):
    """Process data for a single protocol in a specific directory."""
    if use_p2p:
        receiver_file = f"receiver{protocol_name}-rx-data{client_node}.txt"
        sender_file = f"sender{protocol_name}-cwnd-change{server_node}.txt"
        rtt_file = f"sender{protocol_name}-rtt{server_node}.txt"
    else:
        if protocol_name == "QUIC":
            receiver_file = f"clientQUIC-rx-data{client_node}.txt"
            sender_file = f"serverQUIC-cwnd-change{server_node}.txt"
            rtt_file = f"serverQUIC-rtt{server_node}.txt"
        else:
            receiver_file = f"clientTCP-rx-data{client_node}.txt"
            sender_file = f"serverTCP-cwnd-change{server_node}.txt"
            rtt_file = f"serverTCP-rtt{server_node}.txt"
    
    receiver_path = os.path.join(base_dir, receiver_file)
    sender_path = os.path.join(base_dir, sender_file)
    rtt_path = os.path.join(base_dir, rtt_file)
    
    server_df = read_server_data(receiver_path)
    client_df = read_client_data(sender_path)
    rtt_df = read_rtt_data(rtt_path)
    
    if server_df is None or client_df is None or rtt_df is None:
        return None
        
    server_time, data_rates = calculate_data_rate(server_df, 0.5)
    client_time, cwnd_values = calculate_cwnd_over_time(client_df, 0.5)
    rtt_time, rtt_values = calculate_rtt_over_time(rtt_df, 0.5)
    total_throughput = calculate_total_throughput(server_df)
    
    return {
        'protocol': protocol_name,
        'server_time': server_time,
        'data_rates': data_rates,
        'client_time': client_time,
        'cwnd_values': cwnd_values,
        'rtt_time': rtt_time,
        'rtt_values': rtt_values,
        'total_throughput': total_throughput,
        'server_df': server_df,
        'client_df': client_df,
        'rtt_df': rtt_df
    }

def process_protocol_data_multi_user(protocol_name, server_node, client_nodes, use_p2p=False, base_dir='.'):
    """
    Process data for multiple client nodes and aggregate/average them.
    client_nodes: list of client node IDs (e.g., [2, 3, 4])
    Returns aggregated data averaged across all users.
    """
    all_user_data = []
    
    for client_node in client_nodes:
        user_data = process_protocol_data(protocol_name, server_node, client_node, use_p2p, base_dir)
        if user_data is not None:
            all_user_data.append(user_data)
    
    if not all_user_data:
        return None
    
    # If only one user, return it directly
    if len(all_user_data) == 1:
        return all_user_data[0]
    
    # Aggregate data across multiple users using the existing aggregate_series function
    # For server data (receiver data), we aggregate across users
    agg_rate = aggregate_series(all_user_data, 'server_time', 'data_rates', time_bin=0.5)
    agg_cwnd = aggregate_series(all_user_data, 'client_time', 'cwnd_values', time_bin=0.5)
    agg_rtt = aggregate_series(all_user_data, 'rtt_time', 'rtt_values', time_bin=0.5)
    
    # Average total throughput across users
    avg_throughput = sum(d['total_throughput'] for d in all_user_data) / len(all_user_data)
    
    # Combine all server dataframes for potential future use
    combined_server_df = pd.concat([d['server_df'] for d in all_user_data], ignore_index=True)
    combined_client_df = pd.concat([d['client_df'] for d in all_user_data], ignore_index=True)
    combined_rtt_df = pd.concat([d['rtt_df'] for d in all_user_data], ignore_index=True)
    
    return {
        'protocol': protocol_name,
        'server_time': agg_rate[0],
        'data_rates': agg_rate[1],
        'client_time': agg_cwnd[0],
        'cwnd_values': agg_cwnd[1],
        'rtt_time': agg_rtt[0],
        'rtt_values': agg_rtt[1],
        'total_throughput': avg_throughput,
        'server_df': combined_server_df,
        'client_df': combined_client_df,
        'rtt_df': combined_rtt_df,
        'user_count': len(all_user_data)
    }

def aggregate_series(all_runs_data, time_key, value_key, time_bin=0.5):
    """Aggregate time series data from multiple runs."""
    # Find global time range
    min_time = float('inf')
    max_time = float('-inf')
    
    valid_runs = [r for r in all_runs_data if r[time_key] and r[value_key]]
    if not valid_runs:
        return [], [], []
        
    for r in valid_runs:
        min_time = min(min_time, min(r[time_key]))
        max_time = max(max_time, max(r[time_key]))
        
    bins = np.arange(min_time, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin/2
    
    binned_values = defaultdict(list)
    
    for r in valid_runs:
        times = r[time_key]
        values = r[value_key]
        
        curr_idx = 0
        for i, bin_end in enumerate(bins[1:]):
            vals_in_bin = []
            while curr_idx < len(times) and times[curr_idx] <= bin_end:
                vals_in_bin.append(values[curr_idx])
                curr_idx += 1
            
            if vals_in_bin:
                binned_values[i].append(sum(vals_in_bin)/len(vals_in_bin))
            
    means = []
    stds = []
    
    for i in range(len(bin_centers)):
        vals = binned_values[i]
        if vals:
            means.append(sum(vals)/len(vals))
            stds.append(np.std(vals))
        else:
            means.append(None) # Gap
            stds.append(None)
            
    # Filter Nones
    final_times = []
    final_means = []
    final_stds = []
    for t, m, s in zip(bin_centers, means, stds):
        if m is not None:
            final_times.append(t)
            final_means.append(m)
            final_stds.append(s)
            
    return final_times, final_means, final_stds

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
                    # Use relative paths
                    run_groups[name].extend(runs)

    # Check Tcp_artifacts
    if os.path.exists("Tcp_artifacts"):
        for algo in os.listdir("Tcp_artifacts"):
            algo_path = os.path.join("Tcp_artifacts", algo)
            if os.path.isdir(algo_path):
                # Find run_* directories
                runs = glob.glob(os.path.join(algo_path, "run_*"))
                if runs:
                    name = f"TCP {algo}"
                    # Use relative paths
                    run_groups[name].extend(runs)
                    
    return run_groups

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

def plot_metric(results, metric_key, ylabel, title, filename, clean=False, manual_colors=None):
    """Generate and save a plot for a specific metric comparing all algorithms."""
    plt.figure(figsize=(12, 8))
    
    # Define a color palette
    if manual_colors:
        # Create a list of colors matching the order of results items
        colors = [manual_colors.get(algo, 'black') for algo in results.keys()]
    else:
        colors = plt.cm.tab10(np.linspace(0, 1, len(results)))
    
    for (algo, res), color in zip(results.items(), colors):
        t, m, s = res[metric_key]
        if t:
            # Save original t for std processing
            t_orig = t
            # Truncate at T=60 and pad missing data with zeros using quantized bins
            t, m = truncate_and_pad_data(t, m, max_time=60.0, end_time=63.0)
            if s:
                # Use original t array for std, ensuring same length
                t_std, s = truncate_and_pad_data(t_orig, s, max_time=60.0, end_time=63.0)
                # Ensure t and t_std match (they should, but verify)
                if len(t) != len(t_std):
                    # If lengths don't match, use t and pad/truncate s accordingly
                    if len(s) < len(t):
                        # Pad s with zeros
                        s = list(s) + [0.0] * (len(t) - len(s))
                    elif len(s) > len(t):
                        # Truncate s
                        s = s[:len(t)]
            
            user_info = f", {res.get('user_count', 1)} users" if res.get('user_count', 1) > 1 else ""
            lbl = f"{algo} ({res['count']} runs{user_info})"
            plt.plot(t, m, label=lbl, linewidth=2, color=color)
            if not clean and res['count'] > 1 and s:
                plt.fill_between(t, [v-e for v,e in zip(m,s)], [v+e for v,e in zip(m,s)], color=color, alpha=0.1)
    
    plt.xlabel('Time (s)', fontsize=12)
    plt.ylabel(ylabel, fontsize=12)
    plt.title(title, fontsize=14)
    plt.xlim(0, 60)
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"Saved {filename}")
    plt.close()

def main():
    parser = argparse.ArgumentParser(description='Analyze TCP/QUIC simulation data (Multi-run supported)')
    parser.add_argument('--server_node', type=int, default=5, help='Server node ID (default: 5)')
    parser.add_argument('--client_node', type=int, default=2, help='Client node ID (default: 2). If --client_nodes is provided, this is ignored.')
    parser.add_argument('--client_nodes', type=str, default=None, help='Comma-separated list of client node IDs (e.g., "2,3,4") or range (e.g., "2:4" for 2,3,4). If provided, data will be averaged across these users.')
    parser.add_argument('--p2p', action='store_true', help='Use p2p file naming')
    
    args = parser.parse_args()
    
    # Parse client_nodes if provided
    client_nodes = [args.client_node]  # Default to single node
    if args.client_nodes:
        if ':' in args.client_nodes:
            # Range format: "2:4" means 2,3,4
            parts = args.client_nodes.split(':')
            if len(parts) == 2:
                start, end = int(parts[0]), int(parts[1])
                client_nodes = list(range(start, end + 1))
            else:
                print(f"Warning: Invalid range format '{args.client_nodes}'. Using default client_node={args.client_node}")
                client_nodes = [args.client_node]
        else:
            # Comma-separated format: "2,3,4"
            try:
                client_nodes = [int(x.strip()) for x in args.client_nodes.split(',')]
            except ValueError:
                print(f"Warning: Invalid client_nodes format '{args.client_nodes}'. Using default client_node={args.client_node}")
                client_nodes = [args.client_node]
    
    print(f"Processing client nodes: {client_nodes}")
    if len(client_nodes) > 1:
        print(f"Multi-user mode: Data will be averaged across {len(client_nodes)} users")
    
    print("=" * 60)
    print("Starting TCP/QUIC throughput analysis...")
    print("=" * 60)
    import sys
    sys.stdout.flush()
    
    if args.p2p and args.server_node == 5 and client_nodes == [2]:
        args.server_node = 4
        client_nodes = [5]
        
    # Find runs
    run_groups = find_all_runs()
    
    if not run_groups:
        print("No run directories found in Quic_artifacts, Tcp_artifacts, or current directory.")
        return
        
    print(f"Found algorithms: {list(run_groups.keys())}")
    sys.stdout.flush()
    
    # Map logs to runs
    print("\nMapping log files to runs...")
    log_mapping = map_logs_to_runs()
    sys.stdout.flush()
        
    results = {}
    
    print(f"\nProcessing {len(run_groups)} algorithm groups...")
    sys.stdout.flush()
    
    for algo, runs in run_groups.items():
        print(f"\nProcessing {algo} ({len(runs)} runs)...")
        run_data_list = []
        bitrate_avgs = []
        
        # Determine protocol for file naming
        proto = "QUIC" if "QUIC" in algo else "TCP"
        
        for d in runs:
            # Use multi-user processing if multiple client nodes are specified
            if len(client_nodes) > 1:
                data = process_protocol_data_multi_user(proto, args.server_node, client_nodes, args.p2p, d)
            else:
                data = process_protocol_data(proto, args.server_node, client_nodes[0], args.p2p, d)
            if data:
                run_data_list.append(data)
                
            # Process bitrate from log
            # Use relative path match
            # Normalize path to remove ./ if present
            d_rel = os.path.normpath(d)
            if d_rel in log_mapping:
                bitrate_data = parse_bitrate_log(log_mapping[d_rel])
                if bitrate_data:
                    avg_bitrate = calculate_average_bitrate(bitrate_data)
                    bitrate_avgs.append(avg_bitrate)
                
        if not run_data_list:
            continue
            
        # Aggregate
        agg_rate = aggregate_series(run_data_list, 'server_time', 'data_rates')
        agg_cwnd = aggregate_series(run_data_list, 'client_time', 'cwnd_values')
        agg_rtt = aggregate_series(run_data_list, 'rtt_time', 'rtt_values')
        
        avg_throughput = sum(d['total_throughput'] for d in run_data_list) / len(run_data_list)
        
        # Average Bitrate
        avg_bitrate_algo = 0.0
        if bitrate_avgs:
            avg_bitrate_algo = sum(bitrate_avgs) / len(bitrate_avgs) / 1e6  # Convert to Mbps
        
        results[algo] = {
            'rate': agg_rate,
            'cwnd': agg_cwnd,
            'rtt': agg_rtt,
            'throughput': avg_throughput,
            'bitrate': avg_bitrate_algo,
            'count': len(run_data_list)
        }
        
        print(f"  Average Throughput: {avg_throughput:.2f} Mbps")
        print(f"  Average Bitrate: {avg_bitrate_algo:.2f} Mbps")
        sys.stdout.flush()

    if not results:
        print("No results to plot.")
        return
    
    # Generate Plots
    print("\nGenerating Plots...")
    
    output_dir = "Analysis_artifacts"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")
    
    # 1. Throughput Comparison
    plot_metric(results, 'rate', 'Data Rate (Mbps)', 'Throughput Comparison', os.path.join(output_dir, 'throughput_comparison.png'))
    
    # 2. CWND Comparison
    plot_metric(results, 'cwnd', 'CWND (bytes)', 'Congestion Window Comparison', os.path.join(output_dir, 'cwnd_comparison.png'))
    
    # 3. RTT Comparison
    plot_metric(results, 'rtt', 'RTT (s)', 'RTT Comparison', os.path.join(output_dir, 'rtt_comparison.png'))
    
    # 4. Clean versions (no confidence intervals)
    # 4. Clean versions (no confidence intervals)
    plot_metric(results, 'rate', 'Data Rate (Mbps)', 'Throughput Comparison', os.path.join(output_dir, 'throughput_comparison_clean.png'), clean=True)
    plot_metric(results, 'cwnd', 'CWND (bytes)', 'Congestion Window Comparison', os.path.join(output_dir, 'cwnd_comparison_clean.png'), clean=True)
    plot_metric(results, 'rtt', 'RTT (s)', 'RTT Comparison', os.path.join(output_dir, 'rtt_comparison_clean.png'), clean=True)

    # 5. Grand Average Plots (QUIC vs TCP)
    print("\nGenerating Grand Average Plots...")
    
    # Aggregate all QUIC and TCP runs
    all_quic_runs = []
    all_tcp_runs = []
    
    for algo, runs in run_groups.items():
        proto = "QUIC" if "QUIC" in algo else "TCP"
        
        # We need to re-process or store the processed data.
        # Since we didn't store individual run data in `results`, we need to re-process or change logic.
        # Re-processing is safer to avoid memory issues if we kept everything.
        # But we can just iterate again.
        
        print(f"  Collecting runs for {algo}...")
        for d in runs:
            # Use multi-user processing if multiple client nodes are specified
            if len(client_nodes) > 1:
                data = process_protocol_data_multi_user(proto, args.server_node, client_nodes, args.p2p, d)
            else:
                data = process_protocol_data(proto, args.server_node, client_nodes[0], args.p2p, d)
            if data:
                if proto == "QUIC":
                    all_quic_runs.append(data)
                else:
                    all_tcp_runs.append(data)
                    
    combined_results = {}
    
    if all_quic_runs:
        combined_results['QUIC (Combined)'] = {
            'rate': aggregate_series(all_quic_runs, 'server_time', 'data_rates'),
            'cwnd': aggregate_series(all_quic_runs, 'client_time', 'cwnd_values'),
            'rtt': aggregate_series(all_quic_runs, 'rtt_time', 'rtt_values'),
            'count': len(all_quic_runs)
        }
        
    if all_tcp_runs:
        combined_results['TCP (Combined)'] = {
            'rate': aggregate_series(all_tcp_runs, 'server_time', 'data_rates'),
            'cwnd': aggregate_series(all_tcp_runs, 'client_time', 'cwnd_values'),
            'rtt': aggregate_series(all_tcp_runs, 'rtt_time', 'rtt_values'),
            'count': len(all_tcp_runs)
        }
        
    if combined_results:
        # Define colors: QUIC=Red, TCP=Blue
        combined_colors = {'QUIC (Combined)': 'red', 'TCP (Combined)': 'blue'}
        
        # Plot Combined Average (Clean)
        plot_metric(combined_results, 'rate', 'Data Rate (Mbps)', 'Combined Average Throughput (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_throughput.png'), clean=True, manual_colors=combined_colors)
        plot_metric(combined_results, 'cwnd', 'CWND (bytes)', 'Combined Average CWND (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_cwnd.png'), clean=True, manual_colors=combined_colors)
        plot_metric(combined_results, 'rtt', 'RTT (s)', 'Combined Average RTT (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_rtt.png'), clean=True, manual_colors=combined_colors)
        
        # Plot Combined Average (Shaded)
        plot_metric(combined_results, 'rate', 'Data Rate (Mbps)', 'Combined Average Throughput (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_throughput_shaded.png'), clean=False, manual_colors=combined_colors)
        plot_metric(combined_results, 'cwnd', 'CWND (bytes)', 'Combined Average CWND (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_cwnd_shaded.png'), clean=False, manual_colors=combined_colors)
        plot_metric(combined_results, 'rtt', 'RTT (s)', 'Combined Average RTT (QUIC vs TCP)', os.path.join(output_dir, 'combined_avg_rtt_shaded.png'), clean=False, manual_colors=combined_colors)

    # 6. Average Throughput Bar Chart
    plot_average_throughput_bar(results, output_dir)
    
    # 7. Average Bitrate Bar Chart
    plot_average_bitrate_bar(results, output_dir)
    
    print("\n" + "=" * 60)
    print("Analysis complete! All plots saved to", output_dir)
    print("=" * 60)
    sys.stdout.flush()

def map_logs_to_runs():
    """
    Scan all .out files in the current directory to map them to run directories.
    Returns dict: { run_dir_rel_path: log_file_rel_path }
    """
    mapping = {}
    log_files = glob.glob("*.out")
    
    print(f"Scanning {len(log_files)} log files for run mapping...")
    
    for log_file in log_files:
        try:
            with open(log_file, 'r') as f:
                first_line = f.readline()
                # Look for: Running <Algo> Run <ID> (Task <ID>) in <Dir>
                match = re.search(r"Running .* in (.*)", first_line)
                if match:
                    run_dir_rel = match.group(1).strip()
                    # Use relative paths, normalize to remove ./ or trailing slashes
                    run_dir_norm = os.path.normpath(run_dir_rel)
                    log_norm = os.path.normpath(log_file)
                    mapping[run_dir_norm] = log_norm
        except Exception:
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

def calculate_average_bitrate(bitrate_data, duration=65.0):
    """Calculate time-weighted average bitrate."""
    if not bitrate_data:
        return 0.0
        
    total_bits = 0.0
    last_time = bitrate_data[0][0]
    last_rate = bitrate_data[0][1]
    
    for t, rate in bitrate_data[1:]:
        dt = t - last_time
        total_bits += last_rate * dt
        last_time = t
        last_rate = rate
        
    # Add last segment
    if duration > last_time:
        dt = duration - last_time
        total_bits += last_rate * dt
        
    return total_bits / duration

def plot_average_throughput_bar(results, output_dir):
    """Generate a bar chart of average throughputs."""
    algos = list(results.keys())
    throughputs = [results[a]['throughput'] for a in algos]
    
    plt.figure(figsize=(12, 8))
    bars = plt.bar(algos, throughputs)
    
    # Color bars based on protocol
    for bar, algo in zip(bars, algos):
        if "QUIC" in algo:
            bar.set_color('red')
            bar.set_alpha(0.7)
        else:
            bar.set_color('blue')
            bar.set_alpha(0.7)
            
    plt.xlabel('Algorithm', fontsize=12)
    plt.ylabel('Average Throughput (Mbps)', fontsize=12)
    plt.title('Average Throughput Comparison', fontsize=14, fontweight='bold')
    plt.xticks(rotation=45, ha='right')
    plt.grid(axis='y', alpha=0.3)
    
    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.2f}',
                 ha='center', va='bottom')
                 
    plt.tight_layout()
    filename = os.path.join(output_dir, 'average_throughput_bar.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"Saved {filename}")
    plt.close()

def plot_average_bitrate_bar(results, output_dir):
    """Generate a bar chart of average bitrates."""
    algos = list(results.keys())
    # Check if bitrate data exists
    if 'bitrate' not in results[algos[0]]:
        print("No bitrate data available for plotting.")
        return

    bitrates = [results[a]['bitrate'] for a in algos]
    
    plt.figure(figsize=(12, 8))
    bars = plt.bar(algos, bitrates)
    
    # Color bars based on protocol
    for bar, algo in zip(bars, algos):
        if "QUIC" in algo:
            bar.set_color('red')
            bar.set_alpha(0.7)
        else:
            bar.set_color('blue')
            bar.set_alpha(0.7)
            
    plt.xlabel('Algorithm', fontsize=12)
    plt.ylabel('Average Bitrate (Mbps)', fontsize=12)
    plt.title('Average Bitrate Comparison', fontsize=14, fontweight='bold')
    plt.xticks(rotation=45, ha='right')
    plt.grid(axis='y', alpha=0.3)
    
    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.2f}',
                 ha='center', va='bottom')
                 
    plt.tight_layout()
    filename = os.path.join(output_dir, 'average_bitrate_bar.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"Saved {filename}")
    plt.close()

if __name__ == "__main__":
    main()
