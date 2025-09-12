#!/usr/bin/env python3
"""
UDP Server Throughput Calculator with Node Location Visualization
Calculates throughput metrics from NS3 UDP server packet data and plots node locations
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
import re
from math import sqrt

def parse_ns3_time(time_str):
    """Parse NS3 time format (e.g., +200000000.0ns) to seconds"""
    # Remove the '+' and 'ns' and convert to seconds
    time_ns = float(time_str.replace('+', '').replace('ns', ''))
    return time_ns / 1e9  # Convert nanoseconds to seconds

def load_udp_data(filename):
    """Load UDP server data from file"""
    print(f"Loading data from {filename}...")
    
    # Read the data, skipping the header
    data = pd.read_csv(filename, sep='\s+', skiprows=1, header=None)
    
    # Assign column names based on the actual data structure
    # There are 10 columns in the data
    columns = ['Packet_Size', 'from', 'Sequence_Number', 'Uid', 'Txtime', 
               'Rxtime', 'Delay', 'socket', 'socket2', 'node_id']
    data.columns = columns
    
    # Parse time columns - convert to string first to handle the time format
    data['Txtime_sec'] = data['Txtime'].astype(str).apply(parse_ns3_time)
    data['Rxtime_sec'] = data['Rxtime'].astype(str).apply(parse_ns3_time)
    data['Delay_sec'] = data['Delay'].astype(str).apply(parse_ns3_time)
    
    # Convert packet size to bytes
    data['Packet_Size_Bytes'] = data['Packet_Size'].astype(float)
    
    print(f"Loaded {len(data)} packets")
    print(f"Time range: {data['Rxtime_sec'].min():.2f}s to {data['Rxtime_sec'].max():.2f}s")
    print(f"Total duration: {data['Rxtime_sec'].max() - data['Rxtime_sec'].min():.2f}s")
    
    return data

def extract_node_coordinates(zlogs_file):
    """Extract node coordinates from zlogs.txt file"""
    print(f"Extracting node coordinates from {zlogs_file}...")
    
    node_coords = {}
    enb_position = None
    
    try:
        # Read the file as binary and decode as string
        with open(zlogs_file, 'rb') as f:
            content = f.read().decode('utf-8', errors='ignore')
        
        # Find the node coordinates section
        coord_section = re.search(r'=== Node Coordinates ===(.*?)=======================', 
                                 content, re.DOTALL)
        
        if coord_section:
            coord_text = coord_section.group(1)
            
            # Parse each line
            for line in coord_text.strip().split('\n'):
                line = line.strip()
                if not line:
                    continue
                
                # Parse ENB node
                enb_match = re.search(r'ENB Node ID: (\d+), Position=\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)', line)
                if enb_match:
                    node_id = int(enb_match.group(1))
                    x = float(enb_match.group(2))
                    y = float(enb_match.group(3))
                    z = float(enb_match.group(4))
                    enb_position = (x, y, z)
                    node_coords[node_id] = {'x': x, 'y': y, 'z': z, 'type': 'ENB'}
                    print(f"ENB Node {node_id}: ({x}, {y}, {z})")
                    continue
                
                # Parse UE nodes
                ue_match = re.search(r'UE Node ID: (\d+), Position=\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)', line)
                if ue_match:
                    node_id = int(ue_match.group(1))
                    x = float(ue_match.group(2))
                    y = float(ue_match.group(3))
                    z = float(ue_match.group(4))
                    node_coords[node_id] = {'x': x, 'y': y, 'z': z, 'type': 'UE'}
                    print(f"UE Node {node_id}: ({x}, {y}, {z})")
        
        print(f"Extracted coordinates for {len(node_coords)} nodes")
        return node_coords, enb_position
        
    except Exception as e:
        print(f"Error reading coordinates: {e}")
        return {}, None

def calculate_distance_2d(pos1, pos2):
    """Calculate 2D distance between two positions (x, y only)"""
    return sqrt((pos1[0] - pos2[0])**2 + (pos1[1] - pos2[1])**2)

def calculate_overall_throughput(data):
    """Calculate overall throughput for the entire simulation"""
    total_bytes = data['Packet_Size_Bytes'].sum()
    total_time = data['Rxtime_sec'].max() - data['Rxtime_sec'].min()
    
    throughput_bps = total_bytes * 8 / total_time  # bits per second
    throughput_mbps = throughput_bps / 1e6  # Mbps
    
    print(f"\n=== Overall Throughput ===")
    print(f"Total bytes received: {total_bytes:,} bytes")
    print(f"Total time: {total_time:.2f} seconds")
    print(f"Throughput: {throughput_bps:,.0f} bps ({throughput_mbps:.2f} Mbps)")
    
    return throughput_bps, throughput_mbps

def calculate_per_node_throughput(data):
    """Calculate throughput per node"""
    print(f"\n=== Per-Node Throughput ===")
    
    node_throughputs = {}
    
    for node_id in data['node_id'].unique():
        node_data = data[data['node_id'] == node_id]
        
        if len(node_data) > 0:
            total_bytes = node_data['Packet_Size_Bytes'].sum()
            node_start = node_data['Rxtime_sec'].min()
            node_end = node_data['Rxtime_sec'].max()
            node_duration = node_end - node_start
            
            if node_duration > 0:
                node_throughput = (total_bytes * 8) / node_duration  # bps
                node_throughputs[node_id] = {
                    'throughput_bps': node_throughput,
                    'throughput_mbps': node_throughput / 1e6,
                    'packets': len(node_data),
                    'bytes': total_bytes,
                    'duration': node_duration
                }
    
    # Sort by node_id for consistent output
    for node_id in sorted(node_throughputs.keys()):
        info = node_throughputs[node_id]
        print(f"Node {node_id}: {info['throughput_bps']:,.0f} bps ({info['throughput_mbps']:.2f} Mbps), "
              f"{info['packets']} packets, {info['bytes']:,} bytes, {info['duration']:.2f}s")
    
    return node_throughputs


def rectangles_overlap(rect1, rect2):
    """Check if two rectangles overlap"""
    # rect format: (x, y, width, height)
    return not (rect1[0] + rect1[2] < rect2[0] or  # rect1 is to the left of rect2
                rect2[0] + rect2[2] < rect1[0] or  # rect2 is to the left of rect1
                rect1[1] + rect1[3] < rect2[1] or  # rect1 is below rect2
                rect2[1] + rect2[3] < rect1[1])    # rect2 is below rect1

def optimize_label_positions(node_positions, label_texts, ax, padding=15):
    """Optimize label positions to avoid overlaps"""
    
    # Convert positions to display coordinates
    display_positions = []
    for pos in node_positions:
        display_pos = ax.transData.transform(pos)
        display_positions.append(display_pos)
    
    # Define label offsets (8 positions around each node)
    offsets = [
        (0, padding),      # Above
        (0, -padding),     # Below
        (padding, 0),      # Right
        (-padding, 0),     # Left
        (padding, padding), # Top-right
        (-padding, padding), # Top-left
        (padding, -padding), # Bottom-right
        (-padding, -padding) # Bottom-left
    ]
    
    optimized_positions = []
    used_rectangles = []
    
    for i, (pos, text) in enumerate(zip(display_positions, label_texts)):
        # Estimate text size (approximate)
        text_width = len(text) * 5  # Approximate pixels per character
        text_height = 25  # Approximate height in pixels
        
        best_offset = offsets[0]  # Default to above
        best_score = float('inf')
        
        # Try each offset position
        for offset in offsets:
            test_pos = (pos[0] + offset[0], pos[1] + offset[1])
            test_rect = (test_pos[0] - text_width/2, test_pos[1] - text_height/2, text_width, text_height)
            
            # Check overlap with existing labels
            overlap_score = 0
            for existing_rect in used_rectangles:
                if rectangles_overlap(test_rect, existing_rect):
                    overlap_score += 1
            
            # Check if label goes outside plot bounds
            bounds_penalty = 0
            if (test_pos[0] - text_width/2 < 0 or 
                test_pos[0] + text_width/2 > ax.get_window_extent().width or
                test_pos[1] - text_height/2 < 0 or 
                test_pos[1] + text_height/2 > ax.get_window_extent().height):
                bounds_penalty = 10
            
            total_score = overlap_score + bounds_penalty
            
            if total_score < best_score:
                best_score = total_score
                best_offset = offset
        
        # Convert back to data coordinates
        best_display_pos = (pos[0] + best_offset[0], pos[1] + best_offset[1])
        best_data_pos = ax.transData.inverted().transform(best_display_pos)
        optimized_positions.append(best_data_pos)
        
        # Add to used rectangles
        best_rect = (best_display_pos[0] - text_width/2, best_display_pos[1] - text_height/2, text_width, text_height)
        used_rectangles.append(best_rect)
    
    return optimized_positions

def plot_nodes_with_throughput(node_coords, node_throughputs, enb_position, output_file=None):
    """Create a comprehensive plot showing node positions with throughput values and distances"""
    print("Creating node location visualization with throughput values and distances...")
    
    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 8))
    
    # Separate ENB and UE nodes
    enb_nodes = {k: v for k, v in node_coords.items() if v['type'] == 'ENB'}
    ue_nodes = {k: v for k, v in node_coords.items() if v['type'] == 'UE'}
    
    # Plot 1: Node locations with throughput values and distances
    ax1.set_title('Node Locations with Throughput and Distance from ENB', fontsize=14, fontweight='bold')
    
    # Plot ENB nodes
    if enb_nodes:
        for node_id, coords in enb_nodes.items():
            ax1.scatter(coords['x'], coords['y'], s=300, c='red', marker='^', 
                       label='ENB' if node_id == list(enb_nodes.keys())[0] else "", zorder=5)
            ax1.annotate(f'ENB {node_id}', (coords['x'], coords['y']),
                        xytext=(10, 10), textcoords='offset points',
                        fontsize=12, fontweight='bold', ha='center')
    
    # Plot UE nodes with throughput values and distances
    if ue_nodes:
        throughput_values = []
        node_positions = []
        node_ids = []
        label_texts = []
        
        for node_id, coords in ue_nodes.items():
            throughput_data = node_throughputs.get(node_id, {})
            throughput_mbps = throughput_data.get('throughput_mbps', 0)
            throughput_values.append(throughput_mbps)
            node_positions.append((coords['x'], coords['y']))
            node_ids.append(node_id)
            
            # Calculate distance from ENB
            if enb_position:
                distance = calculate_distance_2d((coords['x'], coords['y']), 
                                              (enb_position[0], enb_position[1]))
                if throughput_mbps > 0:
                    label_texts.append(f'Node {node_id}\n{throughput_mbps:.1f} Mbps\n{distance:.1f}m')
                else:
                    label_texts.append(f'Node {node_id}\nNo Data\n{distance:.1f}m')
            else:
                if throughput_mbps > 0:
                    label_texts.append(f'Node {node_id}\n{throughput_mbps:.1f} Mbps')
                else:
                    label_texts.append(f'Node {node_id}\nNo Data')
        
        if throughput_values:
            # Filter out nodes with zero throughput for color mapping
            non_zero_indices = [i for i, val in enumerate(throughput_values) if val > 0]
            non_zero_throughputs = [throughput_values[i] for i in non_zero_indices]
            
            # Create color map based on non-zero throughput values
            if non_zero_throughputs:
                max_throughput = max(non_zero_throughputs)
                colors = []
                for i, throughput in enumerate(throughput_values):
                    if throughput > 0:
                        colors.append(plt.cm.viridis(throughput / max_throughput))
                    else:
                        colors.append('gray')  # Gray for zero throughput
            else:
                colors = ['gray'] * len(throughput_values)
            
            # Plot UE nodes
            for i, (node_id, coords) in enumerate(ue_nodes.items()):
                throughput_mbps = throughput_values[i]
                color = colors[i] if throughput_mbps > 0 else 'gray'
                
                ax1.scatter(coords['x'], coords['y'], s=150, c=[color], 
                           label='UE' if i == 0 else "", zorder=3)
            
            # Optimize label positions to avoid overlaps
            optimized_positions = optimize_label_positions(node_positions, label_texts, ax1)
            
            # Add labels with optimized positions
            for i, (node_id, coords) in enumerate(ue_nodes.items()):
                opt_pos = optimized_positions[i]
                ax1.annotate(label_texts[i], 
                           (coords['x'], coords['y']),
                           xytext=(opt_pos[0] - coords['x'], opt_pos[1] - coords['y']),
                           textcoords='offset points',
                           fontsize=8, ha='center', va='center',
                           bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9),
                           arrowprops=dict(arrowstyle='->', connectionstyle='arc3,rad=0.2', alpha=0.7))
    
    # Add distance circles around ENB
    if enb_position and ue_nodes:
        enb_x, enb_y = enb_position[0], enb_position[1]
        max_distance = max([calculate_distance_2d((coords['x'], coords['y']), (enb_x, enb_y)) 
                           for coords in ue_nodes.values()])
        
        # Draw distance circles
        for radius in [25, 50, 75, 100]:
            if radius <= max_distance:
                circle = plt.Circle((enb_x, enb_y), radius, fill=False, 
                                  linestyle='--', alpha=0.3, color='gray')
                ax1.add_patch(circle)
                ax1.text(enb_x + radius, enb_y, f'{radius}m', 
                        fontsize=8, ha='left', va='center')
    
    ax1.set_xlabel('X Position (m)', fontsize=12)
    ax1.set_ylabel('Y Position (m)', fontsize=12)
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax1.axis('equal')
    
    # Plot 2: Throughput vs Distance from ENB
    if ue_nodes and enb_position:
        ax2.set_title('Throughput vs Distance from ENB', fontsize=14, fontweight='bold')
        
        distances = []
        throughputs = []
        node_labels = []
        
        for node_id, coords in ue_nodes.items():
            distance = calculate_distance_2d((coords['x'], coords['y']), 
                                          (enb_position[0], enb_position[1]))
            throughput_data = node_throughputs.get(node_id, {})
            throughput_mbps = throughput_data.get('throughput_mbps', 0)
            
            distances.append(distance)
            throughputs.append(throughput_mbps)
            node_labels.append(f'Node {node_id}')
        
        # Create scatter plot
        scatter = ax2.scatter(distances, throughputs, c=throughputs, cmap='viridis', s=100)
        
        # Add node labels with distance information
        for i, label in enumerate(node_labels):
            distance = distances[i]
            throughput = throughputs[i]
            if throughput > 0:
                label_text = f'{label}\n{distance:.1f}m\n{throughput:.1f}Mbps'
            else:
                label_text = f'{label}\n{distance:.1f}m\nNo Data'
            
            ax2.annotate(label_text, (distances[i], throughputs[i]),
                        xytext=(5, 5), textcoords='offset points',
                        fontsize=8, ha='left',
                        bbox=dict(boxstyle='round,pad=0.2', facecolor='white', alpha=0.8))
        
        ax2.set_xlabel('Distance from ENB (m)', fontsize=12)
        ax2.set_ylabel('Throughput (Mbps)', fontsize=12)
        ax2.grid(True, alpha=0.3)
        
        # Add colorbar
        cbar = plt.colorbar(scatter, ax=ax2, shrink=0.8)
        cbar.set_label('Throughput (Mbps)', fontsize=12)
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Node location plot saved to {output_file}")
    
    plt.show()

def create_comprehensive_csv(node_coords, node_throughputs, enb_position, output_file='node_throughput_locations.csv'):
    """Create comprehensive CSV with node coordinates, distance, and throughput"""
    print(f"Creating comprehensive CSV: {output_file}")
    
    csv_data = []
    
    for node_id in sorted(node_coords.keys()):
        coords = node_coords[node_id]
        throughput_data = node_throughputs.get(node_id, {})
        
        # Calculate distance from ENB (2D only)
        distance_from_enb = None
        if enb_position and coords['type'] == 'UE':
            distance_from_enb = calculate_distance_2d((coords['x'], coords['y']), 
                                                    (enb_position[0], enb_position[1]))
        
        row = {
            'Node_ID': node_id,
            'Node_Type': coords['type'],
            'X_Position': coords['x'],
            'Y_Position': coords['y'],
            'Z_Position': coords['z'],
            'Distance_From_ENB_2D': distance_from_enb,
            'Total_Packets': throughput_data.get('packets', 0),
            'Total_Bytes': throughput_data.get('bytes', 0),
            'Duration_s': throughput_data.get('duration', 0),
            'Throughput_bps': throughput_data.get('throughput_bps', 0),
            'Throughput_Mbps': throughput_data.get('throughput_mbps', 0)
        }
        csv_data.append(row)
    
    df = pd.DataFrame(csv_data)
    df.to_csv(output_file, index=False)
    print(f"CSV file created with {len(df)} nodes")
    
    return df

def main():
    # Hard-coded file names
    udp_file = "UdpServerRx.txt"
    zlogs_file = "zlogs.txt"
    window_size = 1.0  # seconds
    
    # Load UDP data
    data = load_udp_data(udp_file)
    
    # Extract node coordinates
    node_coords, enb_position = extract_node_coordinates(zlogs_file)
    
    # Calculate overall throughput
    overall_bps, overall_mbps = calculate_overall_throughput(data)
    
    # Calculate per-node throughput
    node_throughputs = calculate_per_node_throughput(data)
    
    # Additional statistics
    print(f"\n=== Additional Statistics ===")
    print(f"Total packets received: {len(data):,}")
    print(f"Average packet size: {data['Packet_Size_Bytes'].mean():.0f} bytes")
    print(f"Average delay: {data['Delay_sec'].mean()*1000:.2f} ms")
    print(f"Packet loss rate: {(data['Sequence_Number'].max() - len(data)) / data['Sequence_Number'].max()*100:.2f}%")
    
    # Create comprehensive CSV with node locations and throughput
    if node_coords:
        comprehensive_df = create_comprehensive_csv(node_coords, node_throughputs, enb_position)
        
        # Print summary with location information
        print(f"\n=== Node Location and Throughput Summary ===")
        for node_id in sorted(node_coords.keys()):
            coords = node_coords[node_id]
            throughput_data = node_throughputs.get(node_id, {})
            throughput_mbps = throughput_data.get('throughput_mbps', 0)
            
            if coords['type'] == 'ENB':
                print(f"ENB Node {node_id}: Position ({coords['x']:.1f}, {coords['y']:.1f}, {coords['z']:.1f})")
            else:
                distance = comprehensive_df[comprehensive_df['Node_ID'] == node_id]['Distance_From_ENB_2D'].iloc[0]
                print(f"UE Node {node_id}: Position ({coords['x']:.1f}, {coords['y']:.1f}, {coords['z']:.1f}), "
                      f"Distance from ENB: {distance:.1f}m, Throughput: {throughput_mbps:.2f} Mbps")
        
    # Generate node location plot with throughput values
    if node_coords:
        plot_nodes_with_throughput(node_coords, node_throughputs, enb_position, 
                                 'node_locations_with_throughput.png')
    
    # Save results to CSV
    results_df = pd.DataFrame({
        'Time': windows,
        'Throughput_bps': throughputs,
        'Throughput_Mbps': throughputs / 1e6
    })
    results_df.to_csv('throughput_results.csv', index=False)
    print(f"\nDetailed results saved to throughput_results.csv")
    
    if node_coords:
        print(f"Node location and throughput data saved to node_throughput_locations.csv")

if __name__ == "__main__":
    main() 