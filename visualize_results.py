#!/usr/bin/env python3
"""
Visualization script for TCP and QUIC protocol analysis results.
Creates comprehensive plots comparing different congestion control algorithms.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle
from matplotlib.gridspec import GridSpec

# Set style for better-looking plots
plt.style.use('default')
plt.rcParams['figure.facecolor'] = 'white'
plt.rcParams['axes.grid'] = True
plt.rcParams['grid.alpha'] = 0.3

def read_and_clean_data(filename='analysis_results.csv'):
    """Read CSV and clean the data, handling comma-separated numbers."""
    df = pd.read_csv(filename)
    
    # Remove rows where timestamp is empty
    df = df[df['timestamp'].notna()]
    
    # Convert columns with commas to numeric values
    numeric_columns = ['total_throughput_mbps', 'total_bytes_received', 
                      'simulation_duration_seconds', 'initial_cwnd_bytes', 
                      'final_cwnd_bytes', 'max_cwnd_bytes', 'min_cwnd_bytes',
                      'initial_rtt_seconds', 'final_rtt_seconds', 
                      'max_rtt_seconds', 'min_rtt_seconds', 'average_rtt_seconds']
    
    for col in numeric_columns:
        if col in df.columns:
            df[col] = df[col].astype(str).str.replace(',', '').astype(float)
    
    return df

def create_comprehensive_visualization(df):
    """Create a visualization with separate bar charts for each metric."""
    
    # Create figure with custom grid layout
    fig = plt.figure(figsize=(18, 10))
    gs = GridSpec(2, 2, figure=fig, hspace=0.4, wspace=0.3)
    
    # Color maps for protocols
    protocol_colors = {'TCP': '#2E86AB', 'QUIC': '#A23B72'}
    
    # ========== Separate Bar Charts for Each Metric ==========
    # Create unique algorithm-protocol combinations
    # Group by algorithm and protocol, taking first occurrence of each combination
    algo_protocol_combos = []
    seen = set()
    for _, row in df.iterrows():
        combo = (row['cc_algorithm'], row['protocol'])
        if combo not in seen:
            seen.add(combo)
            algo_protocol_combos.append(row)
    
    # Define custom order to put NewReno side by side
    custom_order = [
        ('newreno', 'TCP'),
        ('newreno', 'QUIC'),
        ('cubic', 'TCP'),
        ('highspeed', 'TCP'),
        ('olia', 'QUIC')
    ]
    
    # Sort according to custom order
    ordered_combos = []
    for algo, protocol in custom_order:
        for combo_row in algo_protocol_combos:
            if combo_row['cc_algorithm'] == algo and combo_row['protocol'] == protocol:
                ordered_combos.append(combo_row)
                break
    
    # Extract data for plotting
    labels = [f'{row["cc_algorithm"]}\n({row["protocol"]})' for row in ordered_combos]
    protocols = [row['protocol'] for row in ordered_combos]
    throughput_values = [row['total_throughput_mbps'] for row in ordered_combos]
    rtt_values = [row['average_rtt_seconds'] * 1000 for row in ordered_combos]  # Convert to ms
    cwnd_values = [row['final_cwnd_bytes'] / 1024 for row in ordered_combos]  # Convert to KB
    duration_values = [row['simulation_duration_seconds'] for row in ordered_combos]
    
    # Plot 1: Throughput
    ax1 = fig.add_subplot(gs[0, 0])
    bars1 = ax1.bar(range(len(ordered_combos)), throughput_values, 
                   color=[protocol_colors[protocol] for protocol in protocols],
                   alpha=0.8, edgecolor='black', linewidth=1.5)
    ax1.set_xlabel('Algorithm', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Throughput (Mbps)', fontsize=12, fontweight='bold')
    ax1.set_title('Average Throughput', fontsize=13, fontweight='bold')
    ax1.set_xticks(range(len(ordered_combos)))
    ax1.set_xticklabels(labels, rotation=0, ha='center', fontsize=10)
    ax1.grid(axis='y', alpha=0.3)
    # Add padding at top for labels
    ax1.set_ylim(top=max(throughput_values) * 1.15)
    # Add value labels on bars
    for i, (bar, val) in enumerate(zip(bars1, throughput_values)):
        ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                f'{val:.1f}', ha='center', va='bottom', fontweight='bold', fontsize=10)
    
    # Plot 2: Average RTT
    ax2 = fig.add_subplot(gs[0, 1])
    bars2 = ax2.bar(range(len(ordered_combos)), rtt_values,
                   color=[protocol_colors[protocol] for protocol in protocols],
                   alpha=0.8, edgecolor='black', linewidth=1.5)
    ax2.set_xlabel('Algorithm', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Average RTT (ms)', fontsize=12, fontweight='bold')
    ax2.set_title('Average Round Trip Time', fontsize=13, fontweight='bold')
    ax2.set_xticks(range(len(ordered_combos)))
    ax2.set_xticklabels(labels, rotation=0, ha='center', fontsize=10)
    ax2.grid(axis='y', alpha=0.3)
    # Add padding at top for labels
    ax2.set_ylim(top=max(rtt_values) * 1.15)
    # Add value labels on bars
    for i, (bar, val) in enumerate(zip(bars2, rtt_values)):
        ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                f'{val:.3f}', ha='center', va='bottom', fontweight='bold', fontsize=10)
    
    # Plot 3: Final Congestion Window
    ax3 = fig.add_subplot(gs[1, 0])
    bars3 = ax3.bar(range(len(ordered_combos)), cwnd_values,
                   color=[protocol_colors[protocol] for protocol in protocols],
                   alpha=0.8, edgecolor='black', linewidth=1.5)
    ax3.set_xlabel('Algorithm', fontsize=12, fontweight='bold')
    ax3.set_ylabel('Final CWND (KB)', fontsize=12, fontweight='bold')
    ax3.set_title('Final Congestion Window Size', fontsize=13, fontweight='bold')
    ax3.set_xticks(range(len(ordered_combos)))
    ax3.set_xticklabels(labels, rotation=0, ha='center', fontsize=10)
    ax3.grid(axis='y', alpha=0.3)
    # Add padding at top for labels
    ax3.set_ylim(top=max(cwnd_values) * 1.15)
    # Add value labels on bars
    for i, (bar, val) in enumerate(zip(bars3, cwnd_values)):
        ax3.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 10,
                f'{val:.1f}', ha='center', va='bottom', fontweight='bold', fontsize=10)
    
    # Plot 4: Simulation Duration
    ax4 = fig.add_subplot(gs[1, 1])
    bars4 = ax4.bar(range(len(ordered_combos)), duration_values,
                   color=[protocol_colors[protocol] for protocol in protocols],
                   alpha=0.8, edgecolor='black', linewidth=1.5)
    ax4.set_xlabel('Algorithm', fontsize=12, fontweight='bold')
    ax4.set_ylabel('Duration (seconds)', fontsize=12, fontweight='bold')
    ax4.set_title('Simulation Duration', fontsize=13, fontweight='bold')
    ax4.set_xticks(range(len(ordered_combos)))
    ax4.set_xticklabels(labels, rotation=0, ha='center', fontsize=10)
    ax4.grid(axis='y', alpha=0.3)
    # Add padding at top for labels
    ax4.set_ylim(top=max(duration_values) * 1.15)
    # Add value labels on bars
    for i, (bar, val) in enumerate(zip(bars4, duration_values)):
        ax4.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                f'{val:.2f}', ha='center', va='bottom', fontweight='bold', fontsize=10)
    
    # Add main title
    fig.suptitle('TCP vs QUIC Protocol Performance Analysis', 
                 fontsize=16, fontweight='bold', y=0.98)
    
    plt.savefig('protocol_analysis_comprehensive.png', dpi=300, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    print("✓ Saved comprehensive visualization: protocol_analysis_comprehensive.png")
    
    return fig

def create_summary_statistics(df):
    """Print and save summary statistics."""
    print("\n" + "="*80)
    print("SUMMARY STATISTICS")
    print("="*80)
    
    # Group by protocol and algorithm
    for protocol in df['protocol'].unique():
        print(f"\n{protocol} Protocol:")
        print("-" * 80)
        protocol_df = df[df['protocol'] == protocol]
        
        for algo in protocol_df['cc_algorithm'].unique():
            algo_df = protocol_df[protocol_df['cc_algorithm'] == algo]
            print(f"\n{algo.upper()} Algorithm:")
            print(f"  Average Throughput: {algo_df['total_throughput_mbps'].mean():.2f} Mbps")
            print(f"  Average RTT: {algo_df['average_rtt_seconds'].mean():.4f} seconds")
            print(f"  Final CWND: {algo_df['final_cwnd_bytes'].mean():.0f} bytes ({algo_df['final_cwnd_bytes'].mean()/1024:.2f} KB)")
            print(f"  Simulation Duration: {algo_df['simulation_duration_seconds'].mean():.2f} seconds")

def main():
    """Main function to run the visualization."""
    print("Reading analysis_results.csv...")
    df = read_and_clean_data('analysis_results.csv')
    
    print(f"Loaded {len(df)} data points")
    print(f"Protocols: {df['protocol'].unique()}")
    print(f"Algorithms: {df['cc_algorithm'].unique()}")
    
    # Create comprehensive visualization
    print("\nGenerating comprehensive visualization...")
    fig = create_comprehensive_visualization(df)
    
    # Print summary statistics
    create_summary_statistics(df)
    
    print("\n" + "="*80)
    print("Visualization complete! Check 'protocol_analysis_comprehensive.png'")
    print("="*80 + "\n")
    
    # Optionally show the plot
    # plt.show()

if __name__ == '__main__':
    main()

