#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
UDP Throughput Calculator
Calculates throughput from UdpServerRx.txt file based on packet reception data.
Compatible with Python 2.7
"""

import sys
import math
from collections import defaultdict

def parse_time_ns(time_str):
    """Parse time string in nanoseconds format to seconds."""
    # Remove 'ns' suffix and '+' prefix, convert to seconds
    time_ns = float(time_str.replace('ns', '').replace('+', ''))
    return time_ns / 1e9  # Convert nanoseconds to seconds

def calculate_throughput(data_file, time_window=1.0, output_file=None):
    """
    Calculate UDP throughput from the data file.
    
    Args:
        data_file (str): Path to UdpServerRx.txt file
        time_window (float): Time window in seconds for throughput calculation
        output_file (str): Optional output file to save results
    """
    
    try:
        # Read the data file
        print("Reading data from {}...".format(data_file))
        
        packets = []
        with open(data_file, 'r') as f:
            lines = f.readlines()
            
            # Skip header line
            for line in lines[1:]:
                line = line.strip()
                if line:  # Skip empty lines
                    # Split by spaces, but handle multiple spaces
                    parts = line.split()
                    if len(parts) >= 10:  # Ensure we have all required columns
                        packet = {
                            'size': int(parts[0]),
                            'from_ip': parts[1],
                            'seq_num': int(parts[2]),
                            'uid': int(parts[3]),
                            'txtime': parts[5],  # This is the transmit time
                            'rxtime': parts[6],  # This is the receive time  
                            'delay': parts[7],   # This is the delay
                            'socket': parts[8],
                            'node_id': int(parts[9])
                        }
                        packets.append(packet)
        
        print("Loaded {} packets".format(len(packets)))
        
        if not packets:
            print("No packets found in the file")
            return None
        
        # Parse time columns
        for packet in packets:
            packet['txtime_sec'] = parse_time_ns(packet['txtime'])
            packet['rxtime_sec'] = parse_time_ns(packet['rxtime'])
            packet['delay_sec'] = parse_time_ns(packet['delay'])
        
        # Calculate total bytes received
        total_bytes = sum(packet['size'] for packet in packets)
        
        # Calculate time span
        rxtimes = [packet['rxtime_sec'] for packet in packets]
        start_time = min(rxtimes)
        end_time = max(rxtimes)
        total_time = end_time - start_time
        
        # Calculate overall throughput
        overall_throughput_bps = total_bytes * 8 / total_time  # bits per second
        overall_throughput_mbps = overall_throughput_bps / 1e6  # Mbps
        
        print("\n=== Overall Throughput Statistics ===")
        print("Total packets received: {}".format(len(packets)))
        print("Total bytes received: {:,} bytes".format(total_bytes))
        print("Time span: {:.6f} seconds".format(total_time))
        print("Overall throughput: {:.2f} bps".format(overall_throughput_bps))
        print("Overall throughput: {:.6f} Mbps".format(overall_throughput_mbps))
        
        # Calculate throughput in time windows
        print("\n=== Throughput in {}s windows ===".format(time_window))
        
        # Create time bins
        time_bins = []
        current_time = start_time
        while current_time < end_time:
            time_bins.append(current_time)
            current_time += time_window
        time_bins.append(end_time)
        
        # Calculate throughput for each time window
        window_throughput = []
        for i in range(len(time_bins) - 1):
            bin_start = time_bins[i]
            bin_end = time_bins[i + 1]
            
            # Find packets in this time window
            window_packets = [p for p in packets if bin_start <= p['rxtime_sec'] < bin_end]
            
            if window_packets:
                window_bytes = sum(p['size'] for p in window_packets)
                window_time = bin_end - bin_start
                window_throughput_bps = window_bytes * 8 / window_time
                window_throughput_mbps = window_throughput_bps / 1e6
                
                window_data = {
                    'time_start': bin_start,
                    'time_end': bin_end,
                    'packets': len(window_packets),
                    'bytes': window_bytes,
                    'throughput_bps': window_throughput_bps,
                    'throughput_mbps': window_throughput_mbps
                }
                window_throughput.append(window_data)
                
                print("Window {:2d}: {:8.3f}s - {:8.3f}s | "
                      "Packets: {:4d} | "
                      "Bytes: {:6d} | "
                      "Throughput: {:8.4f} Mbps".format(
                      i+1, bin_start, bin_end, len(window_packets), 
                      window_bytes, window_throughput_mbps))
        
        # Calculate statistics
        if window_throughput:
            throughputs_mbps = [w['throughput_mbps'] for w in window_throughput]
            avg_throughput = sum(throughputs_mbps) / len(throughputs_mbps)
            min_throughput = min(throughputs_mbps)
            max_throughput = max(throughputs_mbps)
            
            # Calculate median
            sorted_throughputs = sorted(throughputs_mbps)
            n = len(sorted_throughputs)
            if n % 2 == 0:
                median_throughput = (sorted_throughputs[n//2-1] + sorted_throughputs[n//2]) / 2.0
            else:
                median_throughput = sorted_throughputs[n//2]
            
            # Calculate standard deviation
            variance = sum((x - avg_throughput) ** 2 for x in throughputs_mbps) / len(throughputs_mbps)
            std_throughput = math.sqrt(variance)
            
            print("\n=== Throughput Statistics ===")
            print("Average throughput: {:.6f} Mbps".format(avg_throughput))
            print("Median throughput:  {:.6f} Mbps".format(median_throughput))
            print("Min throughput:     {:.6f} Mbps".format(min_throughput))
            print("Max throughput:     {:.6f} Mbps".format(max_throughput))
            print("Std deviation:      {:.6f} Mbps".format(std_throughput))
        
        # Calculate packet rate
        packet_rate = len(packets) / total_time
        print("\n=== Packet Statistics ===")
        print("Packet rate: {:.2f} packets/second".format(packet_rate))
        
        # Calculate packet size statistics
        packet_sizes = [p['size'] for p in packets]
        avg_packet_size = sum(packet_sizes) / len(packet_sizes)
        min_packet_size = min(packet_sizes)
        max_packet_size = max(packet_sizes)
        
        # Calculate packet size standard deviation
        size_variance = sum((x - avg_packet_size) ** 2 for x in packet_sizes) / len(packet_sizes)
        size_std = math.sqrt(size_variance)
        
        print("Average packet size: {:.2f} bytes".format(avg_packet_size))
        print("Min packet size:     {} bytes".format(min_packet_size))
        print("Max packet size:     {} bytes".format(max_packet_size))
        print("Packet size std dev: {:.2f} bytes".format(size_std))
        
        # Calculate delay statistics
        delays = [p['delay_sec'] for p in packets]
        avg_delay = sum(delays) / len(delays)
        min_delay = min(delays)
        max_delay = max(delays)
        
        # Calculate delay standard deviation
        delay_variance = sum((x - avg_delay) ** 2 for x in delays) / len(delays)
        delay_std = math.sqrt(delay_variance)
        
        print("\n=== Delay Statistics ===")
        print("Average delay: {:.2f} ms".format(avg_delay * 1000))
        print("Min delay:     {:.2f} ms".format(min_delay * 1000))
        print("Max delay:     {:.2f} ms".format(max_delay * 1000))
        print("Delay std dev: {:.2f} ms".format(delay_std * 1000))
        
        # Save results to file if specified
        if output_file:
            with open(output_file, 'w') as f:
                f.write('time_start,time_end,packets,bytes,throughput_bps,throughput_mbps\n')
                for w in window_throughput:
                    f.write('{},{},{},{},{},{}\n'.format(
                        w['time_start'], w['time_end'], w['packets'], 
                        w['bytes'], w['throughput_bps'], w['throughput_mbps']))
            print("\nResults saved to {}".format(output_file))
        
        return {
            'total_packets': len(packets),
            'total_bytes': total_bytes,
            'total_time': total_time,
            'overall_throughput_mbps': overall_throughput_mbps,
            'packet_rate': packet_rate,
            'avg_delay_ms': avg_delay * 1000
        }
        
    except IOError as e:
        print("Error: File '{}' not found.".format(data_file))
        return None
    except Exception as e:
        print("Error processing file: {}".format(e))
        return None

def main():
    # Directly use UdpServerRx.txt file
    data_file = 'UdpServerRx.txt'
    time_window = 1.0  # 1 second time windows
    output_file = 'throughput_results.csv'  # Optional output file
    
    print("UDP Throughput Calculator")
    print("=" * 50)
    print("Analyzing: {}".format(data_file))
    print("Time window: {} seconds".format(time_window))
    print()
    
    results = calculate_throughput(data_file, time_window, output_file)
    
    if results:
        print("\nSummary: {} packets, "
              "{:.4f} Mbps, "
              "{:.1f} pps, "
              "{:.2f} ms avg delay".format(
              results['total_packets'],
              results['overall_throughput_mbps'],
              results['packet_rate'],
              results['avg_delay_ms']))
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()