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

def main():
    parser = argparse.ArgumentParser(description='Analyze TCP/QUIC simulation data (Multi-run supported)')
    parser.add_argument('protocols', nargs='+', choices=['tcp', 'quic', 'both'], help='Protocol(s) to analyze')
    parser.add_argument('--server_node', type=int, default=5, help='Server node ID (default: 5)')
    parser.add_argument('--client_node', type=int, default=2, help='Client node ID (default: 2)')
    parser.add_argument('--p2p', action='store_true', help='Use p2p file naming')
    parser.add_argument('--plot_file', type=str, default='', help='Output plot filename')
    
    args = parser.parse_args()
    
    if args.p2p and args.server_node == 5 and args.client_node == 2:
        args.server_node = 4
        args.client_node = 5
        
    protocols = ['TCP', 'QUIC'] if 'both' in args.protocols else [p.upper() for p in args.protocols]
    
    # Find runs
    run_dirs = glob.glob("run_*")
    # Sort numerically
    run_dirs.sort(key=lambda x: int(x.split('_')[1]) if '_' in x and x.split('_')[1].isdigit() else 0)
    
    if not run_dirs:
        print("No run_* directories found. Checking current directory...")
        run_dirs = ['.']
    else:
        print(f"Found {len(run_dirs)} runs: {run_dirs}")
        
    results = {}
    
    for proto in protocols:
        print(f"\nProcessing {proto}...")
        run_data_list = []
        
        for d in run_dirs:
            data = process_protocol_data(proto, args.server_node, args.client_node, args.p2p, d)
            if data:
                run_data_list.append(data)
                
        if not run_data_list:
            print(f"No valid data found for {proto}")
            continue
            
        # Aggregate
        agg_rate = aggregate_series(run_data_list, 'server_time', 'data_rates')
        agg_cwnd = aggregate_series(run_data_list, 'client_time', 'cwnd_values')
        agg_rtt = aggregate_series(run_data_list, 'rtt_time', 'rtt_values')
        
        avg_throughput = sum(d['total_throughput'] for d in run_data_list) / len(run_data_list)
        
        results[proto] = {
            'rate': agg_rate,
            'cwnd': agg_cwnd,
            'rtt': agg_rtt,
            'throughput': avg_throughput,
            'count': len(run_data_list)
        }
        
        print(f"  Processed {len(run_data_list)} runs.")
        print(f"  Average Throughput: {avg_throughput:.2f} Mbps")

    if not results:
        print("No results to plot.")
        return
    
    # Create plots with all protocols on the same graphs
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(20, 12), sharex=True)
    
    styles = {'TCP': {'color': 'blue', 'marker': 'o'}, 'QUIC': {'color': 'red', 'marker': 's'}}
    
    for proto, res in results.items():
        style = styles.get(proto, {'color': 'black', 'marker': 'x'})
        lbl = f"{proto} (Avg {res['count']} runs)"
        
        # Rate
        t, m, s = res['rate']
        if t:
            ax1.plot(t, m, color=style['color'], label=lbl, linewidth=2)
            if res['count'] > 1:
                ax1.fill_between(t, [v-e for v,e in zip(m,s)], [v+e for v,e in zip(m,s)], color=style['color'], alpha=0.2)
            
        # CWND
        t, m, s = res['cwnd']
        if t:
            ax2.plot(t, m, color=style['color'], label=lbl, linewidth=2)
            if res['count'] > 1:
                ax2.fill_between(t, [v-e for v,e in zip(m,s)], [v+e for v,e in zip(m,s)], color=style['color'], alpha=0.2)
            
        # RTT
        t, m, s = res['rtt']
        if t:
            ax3.plot(t, m, color=style['color'], label=lbl, linewidth=2)
            if res['count'] > 1:
                ax3.fill_between(t, [v-e for v,e in zip(m,s)], [v+e for v,e in zip(m,s)], color=style['color'], alpha=0.2)
            
    ax1.set_ylabel('Data Rate (Mbps)', fontsize=12)
    ax1.set_title('Average Data Rate', fontsize=14)
    ax1.legend(loc='upper right')
    ax1.grid(True, alpha=0.3)
    
    ax2.set_ylabel('CWND (bytes)', fontsize=12)
    ax2.set_title('Average Congestion Window', fontsize=14)
    ax2.legend(loc='upper right')
    ax2.grid(True, alpha=0.3)
    
    ax3.set_ylabel('RTT (s)', fontsize=12)
    ax3.set_xlabel('Time (s)', fontsize=12)
    ax3.set_title('Average RTT', fontsize=14)
    ax3.legend(loc='upper right')
    ax3.grid(True, alpha=0.3)
    
    plt.subplots_adjust(hspace=0.3)
    
    fname = args.plot_file if args.plot_file else f"tcp_quic_analysis_{'_'.join(protocols).lower()}.png"
    plt.savefig(fname, dpi=300, bbox_inches='tight')
    print(f"\nPlot saved to {fname}")

if __name__ == "__main__":
    main()
