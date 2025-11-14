#!/usr/bin/env python3
"""
Verify DASH trace files and compute expected vs actual statistics
"""

import os
import re
from collections import defaultdict

# DASH constants from mpeg-header.h
MPEG_FRAMES_PER_SEGMENT = 100
MPEG_TIME_BETWEEN_FRAMES = 20  # milliseconds
FRAME_RATE = 50  # fps (1000ms / 20ms)

# Header sizes (from source code)
MPEG_HEADER_SIZE = 32  # bytes (4+8+4+8+4+4)
HTTP_HEADER_SIZE = 28  # bytes (4+8+4+4+4+4)
TOTAL_HEADER_OVERHEAD = MPEG_HEADER_SIZE + HTTP_HEADER_SIZE  # 60 bytes per frame

# Simulation parameters from ntn-iab-quic-dash.cc
SIMULATION_TIME = 2.0  # seconds
TARGET_DT = 0.1  # seconds
BUFFER_SPACE = 1048576  # 1 MB
WINDOW = 1.0  # seconds

# Video bitrates (from dash-client.h)
VIDEO_BITRATES = [
    45000, 89000, 131000, 178000, 221000, 263000, 334000,
    396000, 522000, 595000, 791000, 1033000, 1245000, 1547000,
    2134000, 2484000, 3079000, 3527000, 3840000, 4220000, 9500000,
    15000000, 30000000, 66000000, 85000000
]

def parse_dash_client_rx(filename):
    """Parse DashClientRx trace file"""
    data = {'packets': [], 'total_packets': 0, 'total_bytes': 0, 'first_time': None, 'last_time': None}
    if not os.path.exists(filename):
        return data
    
    with open(filename, 'r') as f:
        lines = f.readlines()
        for line in lines[2:]:  # Skip header
            if line.strip() and not line.startswith('#'):
                parts = line.strip().split('\t')
                if len(parts) >= 4:
                    time = float(parts[0])
                    size = int(parts[1])
                    total_packets = int(parts[2])
                    total_bytes = int(parts[3])
                    
                    data['packets'].append({'time': time, 'size': size})
                    data['total_packets'] = total_packets
                    data['total_bytes'] = total_bytes
                    
                    if data['first_time'] is None:
                        data['first_time'] = time
                    data['last_time'] = time
    
    return data

def parse_dash_client_tx(filename):
    """Parse DashClientTx trace file"""
    data = {'requests': [], 'total_requests': 0, 'total_bytes': 0}
    if not os.path.exists(filename):
        return data
    
    with open(filename, 'r') as f:
        lines = f.readlines()
        for line in lines[2:]:  # Skip header
            if line.strip() and not line.startswith('#'):
                parts = line.strip().split('\t')
                if len(parts) >= 4:
                    time = float(parts[0])
                    size = int(parts[1])
                    total_requests = int(parts[2])
                    total_bytes = int(parts[3])
                    
                    data['requests'].append({'time': time, 'size': size})
                    data['total_requests'] = total_requests
                    data['total_bytes'] = total_bytes
    
    return data

def parse_dash_server_rx(filename):
    """Parse DashServerRx trace file"""
    data = {'requests': [], 'total_requests': 0, 'total_bytes': 0}
    if not os.path.exists(filename):
        return data
    
    with open(filename, 'r') as f:
        lines = f.readlines()
        for line in lines[2:]:  # Skip header
            if line.strip() and not line.startswith('#'):
                parts = line.strip().split('\t')
                if len(parts) >= 4:
                    time = float(parts[0])
                    size = int(parts[1])
                    total_requests = int(parts[2])
                    total_bytes = int(parts[3])
                    
                    data['requests'].append({'time': time, 'size': size})
                    data['total_requests'] = total_requests
                    data['total_bytes'] = total_bytes
    
    return data

def parse_quic_rx_data(filename):
    """Parse QUIC Rx data trace file"""
    data = {'packets': [], 'total_packets': 0, 'total_bytes': 0}
    if not os.path.exists(filename):
        return data
    
    with open(filename, 'r') as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                parts = line.strip().split('\t')
                if len(parts) >= 2:
                    time = float(parts[0])
                    size = int(parts[1])
                    data['packets'].append({'time': time, 'size': size})
                    data['total_packets'] += 1
                    data['total_bytes'] += size
    
    return data

def calculate_expected_stats():
    """Calculate expected statistics based on simulation parameters"""
    stats = {}
    
    # Expected segment duration (in simulation time)
    segment_duration = MPEG_FRAMES_PER_SEGMENT * MPEG_TIME_BETWEEN_FRAMES / 1000.0  # 2.0 seconds
    
    # Maximum number of segments that can be requested in simulation time
    max_segments = int(SIMULATION_TIME / segment_duration) + 1  # +1 for initial request
    
    # Expected number of requests (depends on adaptation algorithm and throughput)
    # At minimum bitrate (45 kbps), segment size would be:
    min_bitrate_bps = VIDEO_BITRATES[0]  # 45 kbps
    min_segment_size_bytes = (min_bitrate_bps * segment_duration) / 8
    
    # At maximum bitrate (85 Mbps), segment size would be:
    max_bitrate_bps = VIDEO_BITRATES[-1]  # 85 Mbps
    max_segment_size_bytes = (max_bitrate_bps * segment_duration) / 8
    
    # Expected frames per segment
    expected_frames_per_segment = MPEG_FRAMES_PER_SEGMENT
    
    stats['segment_duration_sec'] = segment_duration
    stats['max_segments_possible'] = max_segments
    stats['frames_per_segment'] = expected_frames_per_segment
    stats['min_segment_size_bytes'] = min_segment_size_bytes
    stats['max_segment_size_bytes'] = max_segment_size_bytes
    stats['min_bitrate_bps'] = min_bitrate_bps
    stats['max_bitrate_bps'] = max_bitrate_bps
    stats['simulation_time'] = SIMULATION_TIME
    stats['target_dt'] = TARGET_DT
    stats['buffer_space'] = BUFFER_SPACE
    stats['window'] = WINDOW
    
    return stats

def main():
    print("=" * 80)
    print("DASH TRACE VERIFICATION AND STATISTICS")
    print("=" * 80)
    print()
    
    # Calculate expected statistics
    expected = calculate_expected_stats()
    
    print("EXPECTED SIMULATION PARAMETERS:")
    print("-" * 80)
    print(f"Simulation Time:           {expected['simulation_time']:.2f} seconds")
    print(f"Target Buffering Time:     {expected['target_dt']:.2f} seconds")
    print(f"Buffer Space:              {expected['buffer_space']:,} bytes ({expected['buffer_space']/1024/1024:.2f} MB)")
    print(f"Throughput Window:         {expected['window']:.2f} seconds")
    print(f"Segment Duration:          {expected['segment_duration_sec']:.2f} seconds")
    print(f"Frames per Segment:         {expected['frames_per_segment']} frames")
    print(f"Frame Rate:                 {FRAME_RATE} fps")
    print(f"Max Segments Possible:      {expected['max_segments_possible']} segments")
    print(f"Min Video Bitrate:          {expected['min_bitrate_bps']/1000:.0f} kbps")
    print(f"Max Video Bitrate:          {expected['max_bitrate_bps']/1000000:.0f} Mbps")
    print(f"Min Segment Size:           {expected['min_segment_size_bytes']:.0f} bytes ({expected['min_segment_size_bytes']/1024:.2f} KB)")
    print(f"Max Segment Size:           {expected['max_segment_size_bytes']:.0f} bytes ({expected['max_segment_size_bytes']/1024/1024:.2f} MB)")
    print()
    
    # Find all trace files
    ue_ids = []
    for i in range(10):  # Check up to 10 UEs
        if os.path.exists(f"DashClientRx_UE_{i}.txt"):
            ue_ids.append(i)
    
    if not ue_ids:
        print("ERROR: No DashClientRx trace files found!")
        return
    
    print(f"FOUND {len(ue_ids)} UE(s)")
    print()
    
    # Analyze each UE
    total_client_tx_bytes = 0
    total_client_rx_bytes = 0
    total_server_rx_bytes = 0
    
    for ue_id in ue_ids:
        print("=" * 80)
        print(f"UE {ue_id} ANALYSIS")
        print("=" * 80)
        
        # Parse trace files
        client_rx_file = f"DashClientRx_UE_{ue_id}.txt"
        client_tx_file = f"DashClientTx_UE_{ue_id}.txt"
        server_rx_file = "DashServerRx.txt"
        quic_client_rx = f"clientQUIC-rx-data{ue_id + 2}.txt"  # Node ID offset
        quic_server_rx = f"serverQUIC-rx-data{ue_id + 5}.txt"  # Node ID offset
        
        client_rx = parse_dash_client_rx(client_rx_file)
        client_tx = parse_dash_client_tx(client_tx_file)
        server_rx = parse_dash_server_rx(server_rx_file)
        quic_client = parse_quic_rx_data(quic_client_rx)
        quic_server = parse_quic_rx_data(quic_server_rx)
        
        # DASH Client Statistics
        print("\nDASH CLIENT (on Remote Host):")
        print("-" * 80)
        print(f"  Segment Requests Sent:")
        print(f"    Total Requests:     {client_tx['total_requests']}")
        print(f"    Total Bytes:         {client_tx['total_bytes']:,} bytes")
        if client_tx['requests']:
            times = [r['time'] for r in client_tx['requests']]
            print(f"    First Request:       {times[0]:.6f} s")
            print(f"    Last Request:        {times[-1]:.6f} s")
            if len(times) > 1:
                intervals = [times[i+1] - times[i] for i in range(len(times)-1)]
                print(f"    Avg Interval:        {sum(intervals)/len(intervals):.6f} s")
        
        print(f"\n  Video Segments Received:")
        print(f"    Total Frames:        {client_rx['total_packets']} frames")
        print(f"    Total Bytes:         {client_rx['total_bytes']:,} bytes ({client_rx['total_bytes']/1024:.2f} KB)")
        if client_rx['packets']:
            print(f"    First Frame:          {client_rx['first_time']:.6f} s")
            print(f"    Last Frame:           {client_rx['last_time']:.6f} s")
            duration = client_rx['last_time'] - client_rx['first_time']
            print(f"    Reception Duration:   {duration:.6f} s")
            if duration > 0:
                avg_throughput = (client_rx['total_bytes'] * 8.0) / duration / 1000000.0
                print(f"    Average Throughput:   {avg_throughput:.2f} Mbps")
            
            # Analyze frame sizes
            sizes = [p['size'] for p in client_rx['packets']]
            avg_frame_size = sum(sizes) / len(sizes)
            print(f"    Avg Frame Size:       {avg_frame_size:.0f} bytes")
            print(f"    Min Frame Size:       {min(sizes):,} bytes")
            print(f"    Max Frame Size:       {max(sizes):,} bytes")
            
            # Check if we got complete segments
            expected_frames = client_tx['total_requests'] * expected['frames_per_segment']
            print(f"\n  Segment Completeness:")
            print(f"    Expected Frames:     {expected_frames} frames ({client_tx['total_requests']} segments × {expected['frames_per_segment']} frames)")
            print(f"    Actual Frames:         {client_rx['total_packets']} frames")
            completeness = (client_rx['total_packets'] / expected_frames * 100) if expected_frames > 0 else 0
            print(f"    Frame Completeness:    {completeness:.1f}%")
            
            # Calculate expected bytes with header overhead
            # Each frame has payload + headers
            avg_payload_size = avg_frame_size - TOTAL_HEADER_OVERHEAD
            expected_segment_bytes_min = (expected['min_bitrate_bps'] * expected['segment_duration_sec'] / 8) + (expected['frames_per_segment'] * TOTAL_HEADER_OVERHEAD)
            expected_segment_bytes_max = (expected['max_bitrate_bps'] * expected['segment_duration_sec'] / 8) + (expected['frames_per_segment'] * TOTAL_HEADER_OVERHEAD)
            
            print(f"\n  Segment Size Analysis:")
            print(f"    Header Overhead:      {TOTAL_HEADER_OVERHEAD} bytes/frame ({MPEG_HEADER_SIZE} MPEG + {HTTP_HEADER_SIZE} HTTP)")
            print(f"    Avg Payload Size:     {avg_payload_size:.0f} bytes/frame")
            print(f"    Expected Seg Size (min): {expected_segment_bytes_min:.0f} bytes")
            print(f"    Expected Seg Size (max): {expected_segment_bytes_max:.0f} bytes")
            print(f"    Actual Total Bytes:      {client_rx['total_bytes']:,} bytes")
            
            # Estimate bitrate from received data
            if client_rx['total_packets'] > 0:
                estimated_total_payload = client_rx['total_bytes'] - (client_rx['total_packets'] * TOTAL_HEADER_OVERHEAD)
                estimated_bitrate_bps = (estimated_total_payload * 8.0) / (client_tx['total_requests'] * expected['segment_duration_sec'])
                print(f"\n    Estimated Bitrate:    {estimated_bitrate_bps/1000:.0f} kbps")
                
                # Find closest match in video bitrates
                closest_bitrate = min(VIDEO_BITRATES, key=lambda x: abs(x - estimated_bitrate_bps))
                print(f"    Closest Match:         {closest_bitrate/1000:.0f} kbps")
            
            # Calculate bytes per segment
            if client_tx['total_requests'] > 0:
                avg_bytes_per_segment = client_rx['total_bytes'] / client_tx['total_requests']
                print(f"    Avg Bytes/Segment:      {avg_bytes_per_segment:.0f} bytes ({avg_bytes_per_segment/1024:.2f} KB)")
            
            # Check for missing frames
            missing_frames = expected_frames - client_rx['total_packets']
            if missing_frames > 0:
                print(f"\n  ⚠ WARNING:")
                print(f"    Missing Frames:        {missing_frames} frames ({missing_frames * 100.0 / expected_frames:.1f}%)")
                print(f"    Possible Reasons:")
                print(f"      - Player buffer filled up (1MB) before PlayFrame started consuming")
                print(f"      - Initial buffering delay (1s) too long - frames accumulate faster than consumed")
                print(f"      - Simulation ended before all segments completed")
                print(f"      - Network delays preventing completion")
                print(f"    RECOMMENDATION:")
                print(f"      - Reduce initial buffering delay in mpeg-player.cc (line 151)")
                print(f"      - OR increase buffer space (currently {expected['buffer_space']/1024/1024:.1f} MB)")
            elif missing_frames == 0:
                print(f"\n  ✓ All expected frames received!")
        
        # DASH Server Statistics
        print(f"\nDASH SERVER (on UE {ue_id}):")
        print("-" * 80)
        print(f"  Segment Requests Received:")
        print(f"    Total Requests:     {server_rx['total_requests']}")
        print(f"    Total Bytes:         {server_rx['total_bytes']:,} bytes")
        if server_rx['requests']:
            times = [r['time'] for r in server_rx['requests']]
            print(f"    First Request:       {times[0]:.6f} s")
            print(f"    Last Request:        {times[-1]:.6f} s")
        
        # QUIC Layer Statistics
        print(f"\nQUIC LAYER STATISTICS:")
        print("-" * 80)
        print(f"  Client (Remote Host) QUIC Rx:")
        print(f"    Total Packets:       {quic_client['total_packets']}")
        print(f"    Total Bytes:         {quic_client['total_bytes']:,} bytes")
        
        print(f"\n  Server (UE {ue_id}) QUIC Rx:")
        print(f"    Total Packets:       {quic_server['total_packets']}")
        print(f"    Total Bytes:         {quic_server['total_bytes']:,} bytes")
        
        # Verification
        print(f"\nVERIFICATION:")
        print("-" * 80)
        requests_match = (client_tx['total_requests'] == server_rx['total_requests'])
        print(f"  Requests Match:        {'✓ PASS' if requests_match else '✗ FAIL'}")
        print(f"    Client Sent:         {client_tx['total_requests']}")
        print(f"    Server Received:     {server_rx['total_requests']}")
        
        if client_rx['total_bytes'] > 0:
            # Check if client received reasonable amount of data
            # Should receive at least min_segment_size_bytes per request
            min_expected_bytes = client_tx['total_requests'] * (expected['min_segment_size_bytes'] * 0.5)  # 50% tolerance
            received_enough = client_rx['total_bytes'] >= min_expected_bytes
            print(f"  Sufficient Data Rx:    {'✓ PASS' if received_enough else '✗ FAIL'}")
            print(f"    Expected Min:       {min_expected_bytes:.0f} bytes")
            print(f"    Actual Received:     {client_rx['total_bytes']:,} bytes")
        
        total_client_tx_bytes += client_tx['total_bytes']
        total_client_rx_bytes += client_rx['total_bytes']
        total_server_rx_bytes += server_rx['total_bytes']
        
        print()
    
    # Summary
    print("=" * 80)
    print("OVERALL SUMMARY")
    print("=" * 80)
    print(f"Total Client Tx (Requests): {total_client_tx_bytes:,} bytes")
    print(f"Total Server Rx (Requests): {total_server_rx_bytes:,} bytes")
    print(f"Total Client Rx (Video):    {total_client_rx_bytes:,} bytes ({total_client_rx_bytes/1024:.2f} KB)")
    print()
    print(f"Overall Throughput:        {(total_client_rx_bytes * 8.0) / SIMULATION_TIME / 1000000.0:.2f} Mbps")
    print()

if __name__ == "__main__":
    main()

