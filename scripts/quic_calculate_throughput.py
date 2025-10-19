#!/usr/bin/env python
"""
Simple QUIC Server Throughput Calculator (No plotting dependencies)
Compatible with Python 2.7 and Python 3.x

This script calculates user throughput from QUIC server receive data.
The input file should contain timestamp, packet size, and node information.
"""

from __future__ import print_function, division
import sys
import argparse
import glob
import os

def parse_data_file(filename):
    """
    Parse the data file and return lists with timestamp, packet_size, and node.
    
    Args:
        filename (str): Path to the data file
        
    Returns:
        tuple: (timestamps, packet_sizes, nodes)
    """
    try:
        timestamps = []
        packet_sizes = []
        nodes = []
        
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:  # Skip empty lines
                    continue
                
                parts = line.split('\t')
                if len(parts) >= 3:
                    timestamps.append(float(parts[0]))
                    packet_sizes.append(int(parts[1]))
                    nodes.append(parts[2])
        
        return timestamps, packet_sizes, nodes
        
    except IOError:
        print("Error: File '{}' not found.".format(filename))
        sys.exit(1)
    except Exception as e:
        print("Error reading file: {}".format(e))
        sys.exit(1)

def calculate_throughput(timestamps, packet_sizes, window_size=1.0):
    """
    Calculate throughput over time using a sliding window approach.
    
    Args:
        timestamps (list): List of timestamps
        packet_sizes (list): List of packet sizes
        window_size (float): Time window size in seconds for throughput calculation
        
    Returns:
        tuple: (window_times, throughputs_mbps, total_bytes_list, packet_counts)
    """
    # Sort data by timestamp
    data = list(zip(timestamps, packet_sizes))
    data.sort(key=lambda x: x[0])
    timestamps_sorted, packet_sizes_sorted = zip(*data)
    
    # Calculate throughput using sliding window
    window_times = []
    throughputs_mbps = []
    total_bytes_list = []
    packet_counts = []
    
    start_time = min(timestamps_sorted)
    end_time = max(timestamps_sorted)
    
    current_time = start_time
    while current_time <= end_time:
        window_start = current_time
        window_end = current_time + window_size
        
        # Get packets in current window
        total_bytes = 0
        packet_count = 0
        
        for i, (ts, size) in enumerate(zip(timestamps_sorted, packet_sizes_sorted)):
            if window_start <= ts < window_end:
                total_bytes += size
                packet_count += 1
        
        # Calculate throughput in Mbps
        throughput_mbps = (total_bytes * 8) / (window_size * 1e6)
        
        window_times.append(current_time)
        throughputs_mbps.append(throughput_mbps)
        total_bytes_list.append(total_bytes)
        packet_counts.append(packet_count)
        
        current_time += window_size
    
    return window_times, throughputs_mbps, total_bytes_list, packet_counts

def calculate_statistics(timestamps, packet_sizes, throughputs_mbps):
    """
    Calculate various statistics about the data and throughput.
    
    Args:
        timestamps (list): List of timestamps
        packet_sizes (list): List of packet sizes
        throughputs_mbps (list): List of throughput values
        
    Returns:
        dict: Dictionary containing various statistics
    """
    stats = {}
    
    # Basic packet statistics
    stats['total_packets'] = len(packet_sizes)
    stats['total_bytes'] = sum(packet_sizes)
    stats['duration_seconds'] = max(timestamps) - min(timestamps)
    stats['average_packet_size'] = sum(packet_sizes) / len(packet_sizes)
    stats['min_packet_size'] = min(packet_sizes)
    stats['max_packet_size'] = max(packet_sizes)
    
    # Throughput statistics
    stats['average_throughput_mbps'] = sum(throughputs_mbps) / len(throughputs_mbps)
    stats['peak_throughput_mbps'] = max(throughputs_mbps)
    stats['min_throughput_mbps'] = min(throughputs_mbps)
    
    # Overall average throughput
    stats['overall_throughput_mbps'] = (stats['total_bytes'] * 8) / (stats['duration_seconds'] * 1e6)
    
    return stats

def find_server_files(directory="."):
    """
    Find all serverQUIC-rx-dataX.txt files in the specified directory.
    
    Args:
        directory (str): Directory to search for files
        
    Returns:
        list: List of matching file paths
    """
    pattern = os.path.join(directory, "serverQUIC-rx-data*.txt")
    files = glob.glob(pattern)
    # Sort files naturally by number
    files.sort(key=lambda x: int(os.path.basename(x).split('data')[1].split('.')[0]) if 'data' in x and x.split('data')[1].split('.')[0].isdigit() else 0)
    return files

def process_single_file(filename, window_size=1.0, verbose=False):
    """
    Process a single file and return statistics.
    
    Args:
        filename (str): Path to the data file
        window_size (float): Time window size for throughput calculation
        verbose (bool): Whether to print verbose information
        
    Returns:
        dict: Statistics dictionary
    """
    if verbose:
        print("Processing file: {}".format(filename))
    
    # Parse data file
    timestamps, packet_sizes, nodes = parse_data_file(filename)
    
    if verbose:
        print("  Loaded {} packets".format(len(timestamps)))
        print("  Time range: {:.3f} to {:.3f} seconds".format(min(timestamps), max(timestamps)))
        print("  Packet size range: {} to {} bytes".format(min(packet_sizes), max(packet_sizes)))
        print("  Unique nodes: {}".format(set(nodes)))
    
    # Calculate throughput
    window_times, throughputs_mbps, total_bytes_list, packet_counts = calculate_throughput(
        timestamps, packet_sizes, window_size)
    
    # Calculate statistics
    stats = calculate_statistics(timestamps, packet_sizes, throughputs_mbps)
    stats['filename'] = os.path.basename(filename)
    
    return stats

def print_file_statistics(stats):
    """
    Print statistics for a single file.
    
    Args:
        stats (dict): Statistics dictionary
    """
    print("\nFile: {}".format(stats['filename']))
    print("-" * (len(stats['filename']) + 7))
    print("Total packets: {:,}".format(stats['total_packets']))
    print("Total bytes: {:,}".format(stats['total_bytes']))
    print("Duration: {:.3f} seconds".format(stats['duration_seconds']))
    print("Average packet size: {:.1f} bytes".format(stats['average_packet_size']))
    print("Packet size range: {} - {} bytes".format(stats['min_packet_size'], stats['max_packet_size']))
    print("Overall average throughput: {:.3f} Mbps".format(stats['overall_throughput_mbps']))
    print("Windowed average throughput: {:.3f} Mbps".format(stats['average_throughput_mbps']))
    print("Peak throughput: {:.3f} Mbps".format(stats['peak_throughput_mbps']))
    print("Minimum throughput: {:.3f} Mbps".format(stats['min_throughput_mbps']))

def print_summary_statistics(all_stats):
    """
    Print summary statistics across all files.
    
    Args:
        all_stats (list): List of statistics dictionaries
    """
    if len(all_stats) <= 1:
        return
    
    print("\n" + "=" * 60)
    print("SUMMARY STATISTICS ACROSS ALL FILES")
    print("=" * 60)
    
    total_packets = sum(stats['total_packets'] for stats in all_stats)
    total_bytes = sum(stats['total_bytes'] for stats in all_stats)
    total_duration = sum(stats['duration_seconds'] for stats in all_stats)
    
    # Calculate weighted averages
    weighted_throughput = sum(stats['overall_throughput_mbps'] * stats['duration_seconds'] for stats in all_stats) / total_duration
    avg_throughput = sum(stats['average_throughput_mbps'] for stats in all_stats) / len(all_stats)
    peak_throughput = max(stats['peak_throughput_mbps'] for stats in all_stats)
    min_throughput = min(stats['min_throughput_mbps'] for stats in all_stats)
    
    print("Files processed: {}".format(len(all_stats)))
    print("Total packets: {:,}".format(total_packets))
    print("Total bytes: {:,}".format(total_bytes))
    print("Total duration: {:.3f} seconds".format(total_duration))
    print("Overall weighted average throughput: {:.3f} Mbps".format(weighted_throughput))
    print("Average windowed throughput: {:.3f} Mbps".format(avg_throughput))
    print("Peak throughput across all files: {:.3f} Mbps".format(peak_throughput))
    print("Minimum throughput across all files: {:.3f} Mbps".format(min_throughput))

def main():
    parser = argparse.ArgumentParser(description='Calculate QUIC server throughput from data files (simple version)')
    parser.add_argument('--input-file', help='Specific input data file path (if not provided, auto-detect serverQUIC-rx-data*.txt files)')
    parser.add_argument('--directory', default='.', help='Directory to search for serverQUIC-rx-data*.txt files (default: current directory)')
    parser.add_argument('--window-size', type=float, default=1.0, 
                       help='Time window size for throughput calculation (seconds)')
    parser.add_argument('--output-csv', help='Output CSV file for throughput data')
    parser.add_argument('--verbose', action='store_true', help='Print detailed information')
    
    args = parser.parse_args()
    
    print("QUIC Server Throughput Calculator (Simple)")
    print("=" * 45)
    
    # Determine which files to process
    if args.input_file:
        # Process specific file
        files_to_process = [args.input_file]
        print("Processing specific file: {}".format(args.input_file))
    else:
        # Auto-detect serverQUIC-rx-data*.txt files
        files_to_process = find_server_files(args.directory)
        if not files_to_process:
            print("No serverQUIC-rx-data*.txt files found in directory: {}".format(args.directory))
            print("Available files in directory:")
            all_files = glob.glob(os.path.join(args.directory, "*.txt"))
            for f in sorted(all_files):
                print("  {}".format(f))
            sys.exit(1)
        
        print("Found {} serverQUIC-rx-data*.txt files:".format(len(files_to_process)))
        for f in files_to_process:
            print("  {}".format(os.path.basename(f)))
    
    # Process each file
    all_stats = []
    for filename in files_to_process:
        try:
            stats = process_single_file(filename, args.window_size, args.verbose)
            all_stats.append(stats)
            print_file_statistics(stats)
        except Exception as e:
            print("Error processing file {}: {}".format(filename, e))
            continue
    
    # Print summary statistics if multiple files
    print_summary_statistics(all_stats)

if __name__ == "__main__":
    main()
