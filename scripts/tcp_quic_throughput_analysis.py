#!/usr/bin/env python3
"""
TCP/QUIC Data Analysis Script

This script reads TCP and/or QUIC trace files and plots:
1. Data rate over time (500ms intervals) comparison
2. Congestion window over time (500ms intervals) comparison
3. RTT over time (500ms intervals) comparison
4. Calculates final throughput for each protocol
5. Writes analysis results to CSV file (appends, doesn't overwrite)

Supports both server/client mode (for ntn-iab-*.cc) and p2p mode (for wns3-*-one-path.cc).
When analyzing both protocols, data is overlaid on the same graphs for easy comparison.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import argparse
import sys
import csv
from datetime import datetime
import os

def read_server_data(file_path):
    """
    Read server data file
    Format TCP: timestamp, packet_size, protocol_tag (optional)
    Format QUIC: timestamp, packet_size, node_id
    """
    try:
        # Read first to determine number of columns
        df = pd.read_csv(file_path, sep='\t', header=None)
        
        if df.shape[1] == 2:
            # TCP format: timestamp, packet_size (with optional protocol tag in value)
            df.columns = ['timestamp', 'packet_size']
            df['node_id'] = 'N/A'  # Add placeholder
        elif df.shape[1] >= 3:
            # QUIC format: timestamp, packet_size, node_id
            df = df.iloc[:, :3]  # Take first 3 columns
            df.columns = ['timestamp', 'packet_size', 'node_id']
        else:
            print(f"Unexpected number of columns in {file_path}: {df.shape[1]}")
            return None
        
        # Remove any empty rows
        df = df.dropna()
        return df
    except Exception as e:
        print(f"Error reading server data from {file_path}: {e}")
        return None

def read_client_data(file_path):
    """
    Read client QUIC congestion window data file
    Format: timestamp, old_cwnd, new_cwnd
    """
    try:
        df = pd.read_csv(file_path, sep='\t', header=None, names=['timestamp', 'old_cwnd', 'new_cwnd'])
        # Remove any empty rows
        df = df.dropna()
        return df
    except Exception as e:
        print(f"Error reading client data from {file_path}: {e}")
        return None

def read_rtt_data(file_path):
    """
    Read RTT data file
    Format: timestamp, old_rtt, new_rtt
    """
    try:
        df = pd.read_csv(file_path, sep='\t', header=None, names=['timestamp', 'old_rtt', 'new_rtt'])
        # Remove any empty rows
        df = df.dropna()
        return df
    except Exception as e:
        print(f"Error reading RTT data from {file_path}: {e}")
        return None

def read_packet_loss_data(file_path):
    """
    Read packet loss data file
    Format: Time, PacketNumber, PacketSize, PathId, NodeId
    """
    try:
        df = pd.read_csv(file_path, sep='\t', header=0)
        # Remove any empty rows
        df = df.dropna()
        return df
    except Exception as e:
        print(f"Error reading packet loss data from {file_path}: {e}")
        return None

def calculate_data_rate(df, time_interval=0.5):
    """
    Calculate data rate for every time_interval seconds
    """
    if df is None or df.empty:
        return None, None
    
    # Get time range
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    
    # Create time bins
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    data_rates = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        
        # Filter data in this time interval
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            # Calculate total bytes received in this interval
            total_bytes = interval_data['packet_size'].sum()
            # Calculate data rate in Mbps
            data_rate_mbps = (total_bytes * 8) / (time_interval * 1e6)
            data_rates.append(data_rate_mbps)
            time_points.append(start_time + time_interval/2)  # Center of interval
    
    return time_points, data_rates

def calculate_cwnd_over_time(df, time_interval=0.5):
    """
    Calculate congestion window over time for every time_interval seconds
    """
    if df is None or df.empty:
        return None, None
    
    # Get time range
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    
    # Create time bins
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    cwnd_values = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        
        # Filter data in this time interval
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            # Use the last congestion window value in this interval
            last_cwnd = interval_data['new_cwnd'].iloc[-1]
            cwnd_values.append(last_cwnd)
            time_points.append(start_time + time_interval/2)  # Center of interval
    
    return time_points, cwnd_values

def calculate_rtt_over_time(df, time_interval=0.5):
    """
    Calculate RTT over time for every time_interval seconds
    """
    if df is None or df.empty:
        return None, None
    
    # Get time range
    min_time = df['timestamp'].min()
    max_time = df['timestamp'].max()
    
    # Create time bins
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    rtt_values = []
    time_points = []
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        
        # Filter data in this time interval
        mask = (df['timestamp'] >= start_time) & (df['timestamp'] < end_time)
        interval_data = df[mask]
        
        if not interval_data.empty:
            # Use the last RTT value in this interval
            last_rtt = interval_data['new_rtt'].iloc[-1]
            rtt_values.append(last_rtt)
            time_points.append(start_time + time_interval/2)  # Center of interval
    
    return time_points, rtt_values

def calculate_packet_loss_over_time(df, time_interval=0.5):
    """
    Calculate cumulative packet loss count over time for every time_interval seconds
    """
    if df is None or df.empty:
        return None, None
    
    # Get time range
    min_time = df['Time'].min()
    max_time = df['Time'].max()
    
    # Create time bins
    time_bins = np.arange(min_time, max_time + time_interval, time_interval)
    
    packet_loss_cumulative = []
    time_points = []
    
    cumulative_loss = 0
    
    for i in range(len(time_bins) - 1):
        start_time = time_bins[i]
        end_time = time_bins[i + 1]
        
        # Filter data in this time interval
        mask = (df['Time'] >= start_time) & (df['Time'] < end_time)
        interval_data = df[mask]
        
        # Add lost packets in this interval to cumulative count
        cumulative_loss += len(interval_data)
        packet_loss_cumulative.append(cumulative_loss)
        time_points.append(start_time + time_interval/2)  # Center of interval
    
    return time_points, packet_loss_cumulative

def calculate_total_throughput(df):
    """
    Calculate total throughput from server data
    """
    if df is None or df.empty:
        return 0
    
    total_bytes = df['packet_size'].sum()
    total_time = df['timestamp'].max() - df['timestamp'].min()
    
    if total_time > 0:
        throughput_mbps = (total_bytes * 8) / (total_time * 1e6)
        return throughput_mbps
    return 0

def write_to_csv(results_dict, csv_file='analysis_results.csv'):
    """
    Write analysis results to CSV file (append mode)
    """
    # Define base CSV columns
    base_fieldnames = [
        'timestamp',
        'protocol',
        'receiver_file',
        'sender_file',
        'rtt_file',
        'total_throughput_mbps',
        'total_bytes_received',
        'simulation_duration_seconds',
        'node_id',
        'initial_cwnd_bytes',
        'final_cwnd_bytes',
        'max_cwnd_bytes',
        'min_cwnd_bytes',
        'initial_rtt_seconds',
        'final_rtt_seconds',
        'max_rtt_seconds',
        'min_rtt_seconds',
        'average_rtt_seconds'
    ]
    
    # Check if file exists
    file_exists = os.path.isfile(csv_file)
    
    # If file exists, read existing headers to preserve column order
    if file_exists:
        with open(csv_file, 'r', newline='') as f:
            reader = csv.reader(f)
            existing_header = next(reader, None)
            if existing_header:
                # Merge base fieldnames with existing header, preserving order
                fieldnames = existing_header
                # Add any new fields from results_dict that aren't in existing header
                for key in results_dict.keys():
                    if key not in fieldnames:
                        fieldnames.append(key)
            else:
                # File exists but is empty, use base fieldnames
                fieldnames = base_fieldnames.copy()
                # Add any extra fields from results_dict
                for key in results_dict.keys():
                    if key not in fieldnames:
                        fieldnames.append(key)
    else:
        # New file, use base fieldnames and add any extra fields
        fieldnames = base_fieldnames.copy()
        for key in results_dict.keys():
            if key not in fieldnames:
                fieldnames.append(key)
    
    # Open file in append mode
    with open(csv_file, 'a', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        
        # Write header if file is new
        if not file_exists:
            writer.writeheader()
        
        # Ensure all fieldnames are in results_dict (set to empty string if missing)
        complete_dict = {}
        for field in fieldnames:
            complete_dict[field] = results_dict.get(field, '')
        
        # Write data row
        writer.writerow(complete_dict)
    
    print(f"\nAnalysis data appended to: {csv_file}")

def process_protocol_data(protocol_name, server_node, client_node, use_p2p=False):
    """
    Process data for a single protocol and return metrics
    """
    # Set file paths based on protocol and mode
    if use_p2p:
        # For p2p connection analysis wns3-tcp-one-path.cc and wns3-mpquic-one-path.cc
        receiver_file = f"receiver{protocol_name}-rx-data{client_node}.txt"
        sender_file = f"sender{protocol_name}-cwnd-change{server_node}.txt"
        rtt_file = f"sender{protocol_name}-rtt{server_node}.txt"
    else:
        # For ntn-iab-tcp.cc and ntn-iab-quic.cc
        if protocol_name == "QUIC":
            receiver_file = f"clientQUIC-rx-data{client_node}.txt"
            sender_file = f"serverQUIC-cwnd-change{server_node}.txt"
            rtt_file = f"serverQUIC-rtt{server_node}.txt"
        else:  # TCP
            receiver_file = f"clientTCP-rx-data{client_node}.txt"
            sender_file = f"serverTCP-cwnd-change{server_node}.txt"
            rtt_file = f"serverTCP-rtt{server_node}.txt"
    
    print(f"{protocol_name} Data Analysis")
    print(f"  Reading server data from: {receiver_file}")
    print(f"  Reading client data from: {sender_file}")
    print(f"  Reading RTT data from: {rtt_file}")
    
    # Read data
    server_df = read_server_data(receiver_file)
    client_df = read_client_data(sender_file)
    rtt_df = read_rtt_data(rtt_file)
    
    # Check if we have valid data
    if server_df is None or server_df.empty:
        print(f"  Warning: Could not read server data from {receiver_file}")
        return None
    
    if client_df is None or client_df.empty:
        print(f"  Warning: Could not read client data from {sender_file}")
        return None
    
    if rtt_df is None or rtt_df.empty:
        print(f"  Warning: Could not read RTT data from {rtt_file}")
        return None
    
    # Calculate metrics
    server_time, data_rates = calculate_data_rate(server_df, 0.5)
    client_time, cwnd_values = calculate_cwnd_over_time(client_df, 0.5)
    rtt_time, rtt_values = calculate_rtt_over_time(rtt_df, 0.5)
    total_throughput = calculate_total_throughput(server_df)
    
    return {
        'protocol': protocol_name,
        'receiver_file': receiver_file,
        'sender_file': sender_file,
        'rtt_file': rtt_file,
        'server_df': server_df,
        'client_df': client_df,
        'rtt_df': rtt_df,
        'server_time': server_time,
        'data_rates': data_rates,
        'client_time': client_time,
        'cwnd_values': cwnd_values,
        'rtt_time': rtt_time,
        'rtt_values': rtt_values,
        'total_throughput': total_throughput
    }

def main():
    """
    Main function
    """
    # Parse command-line arguments
    parser = argparse.ArgumentParser(description='Analyze TCP and/or QUIC simulation data')
    parser.add_argument('protocols', nargs='+', choices=['tcp', 'quic', 'both'], 
                       help='Protocol(s) to analyze: tcp, quic, or both')
    parser.add_argument('--server_node', type=int, default=5, 
                       help='Server node ID (default: 5)')
    parser.add_argument('--client_node', type=int, default=2, 
                       help='Client node ID (default: 2)')
    parser.add_argument('--csv_file', type=str, default='analysis_results.csv',
                       help='CSV file to write analysis results (default: analysis_results.csv)')
    parser.add_argument('--cc_algorithm', type=str, default='',
                       help='Congestion control algorithm used (optional, for CSV record)')
    parser.add_argument('--p2p', action='store_true',
                       help='Use p2p file naming (receiver/sender instead of server/client)')
    parser.add_argument('--plot_file', type=str, default='',
                       help='Output plot filename (default: auto-generated based on protocols)')
    
    args = parser.parse_args()
    
    # Adjust defaults for p2p mode if not explicitly set
    if args.p2p and args.server_node == 5 and args.client_node == 2:
        # User didn't specify custom nodes, use p2p defaults
        args.server_node = 4  # sender
        args.client_node = 5  # receiver
    
    # Determine which protocols to process
    protocols_to_process = []
    if 'both' in args.protocols:
        protocols_to_process = ['TCP', 'QUIC']
    else:
        protocols_to_process = [p.upper() for p in args.protocols if p in ['tcp', 'quic']]
    
    if not protocols_to_process:
        print("Error: No valid protocols specified")
        return
    
    print("TCP/QUIC Data Analysis Script")
    print("=" * 70)
    if args.p2p:
        print("Mode: P2P (receiver/sender)")
    else:
        print("Mode: Server/Client")
    print(f"Protocols: {', '.join(protocols_to_process)}")
    print("=" * 70)
    print()
    
    # Process data for each protocol
    protocol_data = []
    for protocol_name in protocols_to_process:
        data = process_protocol_data(protocol_name, args.server_node, args.client_node, args.p2p)
        if data is not None:
            protocol_data.append(data)
        print()
    
    if not protocol_data:
        print("Error: No valid data to process")
        return
    
    # Create plots with all protocols on the same graphs
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(20, 12))
    
    # Color and marker mapping for each protocol
    protocol_styles = {
        'TCP': {'color': 'blue', 'marker': 'o', 'label': 'TCP'},
        'QUIC': {'color': 'red', 'marker': 's', 'label': 'QUIC'}
    }
    
    # Plot data rate
    for data in protocol_data:
        if data['server_time'] and data['data_rates']:
            style = protocol_styles.get(data['protocol'], {'color': 'blue', 'marker': 'o'})
            ax1.plot(data['server_time'], data['data_rates'], 
                    f"{style['marker']}-", color=style['color'], linewidth=2, markersize=4,
                    label=style['label'])
    ax1.set_ylabel('Data Rate (Mbps)', fontsize=12)
    ax1.set_title('Data Rate', fontsize=14, pad=8)
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc='upper right')
    ax1.tick_params(axis='x', labelsize=10)
    
    # Plot congestion window
    for data in protocol_data:
        if data['client_time'] and data['cwnd_values']:
            style = protocol_styles.get(data['protocol'], {'color': 'blue', 'marker': 'o'})
            ax2.plot(data['client_time'], data['cwnd_values'], 
                    f"{style['marker']}-", color=style['color'], linewidth=2, markersize=4,
                    label=style['label'])
    ax2.set_ylabel('Congestion Window (bytes)', fontsize=12)
    ax2.set_title('Congestion Window', fontsize=14, pad=8)
    ax2.grid(True, alpha=0.3)
    ax2.legend(loc='upper right')
    ax2.tick_params(axis='x', labelsize=10)
    
    # Plot RTT
    for data in protocol_data:
        if data['rtt_time'] and data['rtt_values']:
            style = protocol_styles.get(data['protocol'], {'color': 'blue', 'marker': 'o'})
            ax3.plot(data['rtt_time'], data['rtt_values'], 
                    f"{style['marker']}-", color=style['color'], linewidth=2, markersize=4,
                    label=style['label'])
    ax3.set_xlabel('Time (seconds)', fontsize=12, labelpad=10)
    ax3.set_ylabel('RTT (seconds)', fontsize=12)
    ax3.set_title('Round Trip Time', fontsize=14, pad=8)
    ax3.grid(True, alpha=0.3)
    ax3.legend(loc='upper right')
    ax3.tick_params(axis='x', labelsize=10)
    
    # Adjust subplot spacing to maximize horizontal space and reduce vertical spacing
    plt.subplots_adjust(hspace=0.3, top=0.95, bottom=0.08, left=0.06, right=0.98)
    
    # Determine output filename
    if args.plot_file:
        plot_filename = args.plot_file
    else:
        # Auto-generate filename based on protocols
        protocol_str = '_'.join(protocols_to_process).lower()
        plot_filename = f'tcp_quic_analysis_{protocol_str}.png'
    
    # Save plot to disk
    plt.savefig(plot_filename, dpi=300, bbox_inches='tight')
    print(f"\nPlot saved to: {plot_filename}")
    
    plt.show()
    
    # Print statistics for each protocol
    for data in protocol_data:
        print(f"\n{'=' * 70}")
        print(f"{data['protocol']} Statistics")
        print(f"{'=' * 70}")
        print(f"{data['receiver_file']}:")
        print(f"  Total throughput: {data['total_throughput']:.2f} Mbps")
        print(f"  Total bytes received: {data['server_df']['packet_size'].sum():,}")
        print(f"  Simulation duration: {data['server_df']['timestamp'].max() - data['server_df']['timestamp'].min():.2f} seconds")
        print(f"  Node ID: {data['server_df']['node_id'].iloc[0] if not data['server_df'].empty else 'Unknown'}")
        
        print(f"\n{data['sender_file']}:")
        print(f"  Initial congestion window: {data['client_df']['new_cwnd'].iloc[0]:,} bytes")
        print(f"  Final congestion window: {data['client_df']['new_cwnd'].iloc[-1]:,} bytes")
        print(f"  Max congestion window: {data['client_df']['new_cwnd'].max():,} bytes")
        print(f"  Min congestion window: {data['client_df']['new_cwnd'].min():,} bytes")
        
        print(f"\n{data['rtt_file']}:")
        print(f"  Initial RTT: {data['rtt_df']['new_rtt'].iloc[0]:.6f} seconds")
        print(f"  Final RTT: {data['rtt_df']['new_rtt'].iloc[-1]:.6f} seconds")
        print(f"  Max RTT: {data['rtt_df']['new_rtt'].max():.6f} seconds")
        print(f"  Min RTT: {data['rtt_df']['new_rtt'].min():.6f} seconds")
        print(f"  Average RTT: {data['rtt_df']['new_rtt'].mean():.6f} seconds")
    
    # Write to CSV for each protocol
    for data in protocol_data:
        node_id_str = str(data['server_df']['node_id'].iloc[0]) if not data['server_df'].empty else 'Unknown'
        
        results_dict = {
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'protocol': data['protocol'],
            'receiver_file': data['receiver_file'],
            'sender_file': data['sender_file'],
            'rtt_file': data['rtt_file'],
            'total_throughput_mbps': f"{data['total_throughput']:.2f}",
            'total_bytes_received': f"{data['server_df']['packet_size'].sum():,}",
            'simulation_duration_seconds': f"{data['server_df']['timestamp'].max() - data['server_df']['timestamp'].min():.2f}",
            'node_id': node_id_str,
            'initial_cwnd_bytes': f"{data['client_df']['new_cwnd'].iloc[0]:,}",
            'final_cwnd_bytes': f"{data['client_df']['new_cwnd'].iloc[-1]:,}",
            'max_cwnd_bytes': f"{data['client_df']['new_cwnd'].max():,}",
            'min_cwnd_bytes': f"{data['client_df']['new_cwnd'].min():,}",
            'initial_rtt_seconds': f"{data['rtt_df']['new_rtt'].iloc[0]:.6f}",
            'final_rtt_seconds': f"{data['rtt_df']['new_rtt'].iloc[-1]:.6f}",
            'max_rtt_seconds': f"{data['rtt_df']['new_rtt'].max():.6f}",
            'min_rtt_seconds': f"{data['rtt_df']['new_rtt'].min():.6f}",
            'average_rtt_seconds': f"{data['rtt_df']['new_rtt'].mean():.6f}"
        }
        
        # Add CC algorithm if provided
        if args.cc_algorithm:
            results_dict['cc_algorithm'] = args.cc_algorithm
        
        # Write to CSV
        write_to_csv(results_dict, args.csv_file)
    
    print("\nAnalysis complete!")

if __name__ == "__main__":
    main()
