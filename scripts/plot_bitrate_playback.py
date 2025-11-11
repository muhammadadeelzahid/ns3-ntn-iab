#!/usr/bin/env python
"""
Script to plot bitrate over time and video playback/interruption status
from zquiclogs.txt and ztcplogs.txt files.
"""

import re
import os
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
from collections import defaultdict

def parse_bitrate_log(filename):
    """Parse bitrate information from log file."""
    bitrate_data = []
    
    with open(filename, 'r') as f:
        for line in f:
            # Match pattern: time Node: node newBitRate: bitrate ...
            match = re.search(r'(\d+\.?\d*)\s+Node:\s+\d+\s+newBitRate:\s+(\d+)', line)
            if match:
                time = float(match.group(1))
                bitrate = float(match.group(2))
                bitrate_data.append((time, bitrate))
    
    return bitrate_data

def parse_playback_log(filename):
    """Parse playback status and resolution from log file."""
    playback_events = []
    current_resolution = None
    last_play_time = None
    resume_pending = False
    
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    for i, line in enumerate(lines):
        # Match PLAYING FRAME lines: time PLAYING FRAME: ... Res: resolution ...
        play_match = re.search(r'(\d+\.?\d*)\s+PLAYING FRAME:.*?Res:\s+(\d+)', line)
        if play_match:
            time = float(play_match.group(1))
            resolution = float(play_match.group(2))
            current_resolution = resolution
            last_play_time = time
            
            # If there was a pending resume, use this time for it
            if resume_pending:
                playback_events.append(('resumed', time, current_resolution))
                resume_pending = False
            
            playback_events.append(('playing', time, resolution))
        
        # Match "No frames to play" - interruption
        interrupt_match = re.search(r'(\d+\.?\d*)\s+No frames to play', line)
        if interrupt_match:
            time = float(interrupt_match.group(1))
            playback_events.append(('interrupted', time, current_resolution))
            last_play_time = time
            resume_pending = False
        
        # Match "Play resumed" - resuming playback
        # The actual time will be from the next PLAYING FRAME line
        resume_match = re.search(r'Play resumed', line)
        if resume_match:
            resume_pending = True
    
    return playback_events

def calculate_playback_stats(playback_events):
    """Calculate total playback and interruption times."""
    if not playback_events:
        return 0.0, 0.0, [], []
    
    # Sort events by time
    sorted_events = sorted(playback_events, key=lambda x: x[1])
    
    playback_time = 0.0
    interruption_time = 0.0
    playback_periods = []
    interruption_periods = []
    
    state = None  # 'playing', 'interrupted', or None
    state_start_time = None
    
    for event_type, time, resolution in sorted_events:
        if event_type == 'playing':
            if state == 'interrupted' and state_start_time is not None:
                # End of interruption period
                interruption_time += time - state_start_time
                interruption_periods.append((state_start_time, time))
            
            if state != 'playing':
                state_start_time = time
            state = 'playing'
        
        elif event_type == 'interrupted':
            if state == 'playing' and state_start_time is not None:
                # End of playback period
                playback_time += time - state_start_time
                playback_periods.append((state_start_time, time))
            
            if state != 'interrupted':
                state_start_time = time
            state = 'interrupted'
        
        elif event_type == 'resumed':
            if state == 'interrupted' and state_start_time is not None:
                # End of interruption period
                interruption_time += time - state_start_time
                interruption_periods.append((state_start_time, time))
            
            state = 'playing'
            state_start_time = time
    
    # Handle final state
    if state == 'playing' and state_start_time is not None:
        # Assume playback continues until last event time
        last_time = sorted_events[-1][1]
        playback_time += last_time - state_start_time
        playback_periods.append((state_start_time, last_time))
    elif state == 'interrupted' and state_start_time is not None:
        last_time = sorted_events[-1][1]
        interruption_time += last_time - state_start_time
        interruption_periods.append((state_start_time, last_time))
    
    return playback_time, interruption_time, playback_periods, interruption_periods

def create_playback_timeline(playback_events, time_range):
    """Create timeline data for playback status with continuous periods."""
    if not playback_events:
        return []
    
    # Sort events by time
    sorted_events = sorted(playback_events, key=lambda x: x[1])
    
    # Build periods: (start_time, end_time, status, resolution)
    periods = []
    current_status = None  # 'playing' or 'interrupted'
    current_resolution = None
    period_start = None
    
    for event_type, time, resolution in sorted_events:
        if event_type == 'playing':
            if current_status == 'interrupted' and period_start is not None:
                # End interruption period
                periods.append((period_start, time, 'interrupted', current_resolution))
            
            if current_status != 'playing':
                period_start = time
            current_status = 'playing'
            current_resolution = resolution
        
        elif event_type == 'interrupted':
            if current_status == 'playing' and period_start is not None:
                # End playback period
                periods.append((period_start, time, 'playing', current_resolution))
            
            if current_status != 'interrupted':
                period_start = time
            current_status = 'interrupted'
        
        elif event_type == 'resumed':
            if current_status == 'interrupted' and period_start is not None:
                # End interruption period
                periods.append((period_start, time, 'interrupted', current_resolution))
            
            period_start = time
            current_status = 'playing'
    
    # Handle final period
    if period_start is not None and sorted_events:
        last_time = sorted_events[-1][1]
        periods.append((period_start, last_time, current_status, current_resolution))
    
    return periods

def get_bitrate_for_period(bitrate_data, start_time, end_time):
    """Get average bitrate for a time period."""
    if not bitrate_data:
        return None
    
    # Find bitrates within the period
    period_bitrates = []
    for time, bitrate in bitrate_data:
        if start_time <= time <= end_time:
            period_bitrates.append(bitrate)
    
    if period_bitrates:
        return sum(period_bitrates) / len(period_bitrates)
    return None

def plot_comparison(quic_bitrate, tcp_bitrate, quic_playback, tcp_playback):
    """Create comparison plots."""
    # Create 3-row layout: bitrate combined, QUIC playback full width, TCP playback full width
    fig = plt.figure(figsize=(16, 12))
    gs = gridspec.GridSpec(3, 1, height_ratios=[1, 1, 1], hspace=0.3)
    fig.suptitle('Bitrate and Playback Status Comparison: QUIC vs TCP', fontsize=16, fontweight='bold')
    
    # Extract time ranges
    all_times = []
    if quic_bitrate:
        all_times.extend([t for t, _ in quic_bitrate])
    if tcp_bitrate:
        all_times.extend([t for t, _ in tcp_bitrate])
    if quic_playback:
        all_times.extend([t for _, t, _ in quic_playback])
    if tcp_playback:
        all_times.extend([t for _, t, _ in tcp_playback])
    
    if not all_times:
        print("No data found in log files!")
        return
    
    time_min = min(all_times)
    time_max = max(all_times)
    
    # Plot 1: Combined QUIC and TCP Bitrate
    ax1 = fig.add_subplot(gs[0, 0])
    has_data = False
    if quic_bitrate:
        quic_times, quic_bitrates = zip(*quic_bitrate)
        ax1.plot(quic_times, [b/1e6 for b in quic_bitrates], 'b-', linewidth=2, label='QUIC')
        has_data = True
    if tcp_bitrate:
        tcp_times, tcp_bitrates = zip(*tcp_bitrate)
        ax1.plot(tcp_times, [b/1e6 for b in tcp_bitrates], 'r-', linewidth=2, label='TCP')
        has_data = True
    
    if has_data:
        ax1.set_xlabel('Time (s)', fontsize=12)
        ax1.set_ylabel('Bitrate (Mbps)', fontsize=12)
        ax1.set_title('Bitrate Over Time: QUIC vs TCP', fontsize=13, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        ax1.legend()
        ax1.set_xlim(time_min, time_max)
        ax1.tick_params(labelleft=False)  # Remove y-axis tick values
    else:
        ax1.text(0.5, 0.5, 'No bitrate data', ha='center', va='center', transform=ax1.transAxes)
        ax1.set_xlabel('Time (s)', fontsize=12)
        ax1.set_title('Bitrate Over Time: QUIC vs TCP', fontsize=13, fontweight='bold')
        ax1.tick_params(labelleft=False)  # Remove y-axis tick values
    
    # Plot 2: QUIC Playback Status (full width)
    ax2 = fig.add_subplot(gs[1, 0])
    if quic_playback:
        quic_periods = create_playback_timeline(quic_playback, (time_min, time_max))
        
        if quic_periods:
            # Collect all resolutions for normalization
            all_resolutions = [res for _, _, _, res in quic_periods if res is not None and res > 0]
            max_res = max(all_resolutions) if all_resolutions else 1
            
            # Plot periods
            first_play = True
            first_interrupt = True
            for start_time, end_time, status, resolution in quic_periods:
                if status == 'playing':
                    # Plot as filled area for playing period
                    label = 'Playing' if first_play else ''
                    ax2.fill_between([start_time, end_time], [0, 0], [1, 1], 
                                    alpha=0.6, color='green', label=label)
                    first_play = False
                else:  # interrupted
                    label = 'Interrupted' if first_interrupt else ''
                    ax2.fill_between([start_time, end_time], [0, 0], [1, 1], 
                                    alpha=0.4, color='red', label=label)
                    first_interrupt = False
            
            ax2.set_ylim(-0.1, 1.1)
            ax2.set_xlabel('Time (s)', fontsize=12)
            ax2.set_ylabel('Status', fontsize=12)
            ax2.set_title('QUIC: Playback Status', fontsize=13, fontweight='bold')
            ax2.grid(True, alpha=0.3)
            ax2.set_xlim(time_min, time_max)
            ax2.sharex(ax1)  # Share x-axis with top plot for alignment
            ax2.tick_params(labelleft=False)  # Remove y-axis tick values
            # Remove duplicate labels
            handles, labels = ax2.get_legend_handles_labels()
            by_label = dict(zip(labels, handles))
            ax2.legend(by_label.values(), by_label.keys(), loc='upper right', fontsize=9)
        else:
            ax2.text(0.5, 0.5, 'No QUIC playback data', ha='center', va='center', transform=ax2.transAxes)
            ax2.set_xlabel('Time (s)', fontsize=12)
            ax2.set_title('QUIC: Playback Status', fontsize=13, fontweight='bold')
            ax2.set_xlim(time_min, time_max)
            ax2.sharex(ax1)
            ax2.tick_params(labelleft=False)
    else:
        ax2.text(0.5, 0.5, 'No QUIC playback data', ha='center', va='center', transform=ax2.transAxes)
        ax2.set_xlabel('Time (s)', fontsize=12)
        ax2.set_title('QUIC: Playback Status', fontsize=13, fontweight='bold')
        ax2.set_xlim(time_min, time_max)
        ax2.sharex(ax1)
        ax2.tick_params(labelleft=False)
    
    # Plot 3: TCP Playback Status (full width)
    ax3 = fig.add_subplot(gs[2, 0])
    if tcp_playback:
        tcp_periods = create_playback_timeline(tcp_playback, (time_min, time_max))
        
        if tcp_periods:
            # Collect all resolutions for normalization
            all_resolutions = [res for _, _, _, res in tcp_periods if res is not None and res > 0]
            max_res = max(all_resolutions) if all_resolutions else 1
            
            # Plot periods
            first_play = True
            first_interrupt = True
            for start_time, end_time, status, resolution in tcp_periods:
                if status == 'playing':
                    # Plot as filled area for playing period
                    label = 'Playing' if first_play else ''
                    ax3.fill_between([start_time, end_time], [0, 0], [1, 1], 
                                    alpha=0.6, color='green', label=label)
                    first_play = False
                else:  # interrupted
                    label = 'Interrupted' if first_interrupt else ''
                    ax3.fill_between([start_time, end_time], [0, 0], [1, 1], 
                                    alpha=0.4, color='red', label=label)
                    first_interrupt = False
            
            ax3.set_ylim(-0.1, 1.1)
            ax3.set_xlabel('Time (s)', fontsize=12)
            ax3.set_ylabel('Status', fontsize=12)
            ax3.set_title('TCP: Playback Status', fontsize=13, fontweight='bold')
            ax3.grid(True, alpha=0.3)
            ax3.set_xlim(time_min, time_max)
            ax3.sharex(ax1)  # Share x-axis with top plot for alignment
            ax3.tick_params(labelleft=False)  # Remove y-axis tick values
            # Remove duplicate labels
            handles, labels = ax3.get_legend_handles_labels()
            by_label = dict(zip(labels, handles))
            ax3.legend(by_label.values(), by_label.keys(), loc='upper right', fontsize=9)
        else:
            ax3.text(0.5, 0.5, 'No TCP playback data', ha='center', va='center', transform=ax3.transAxes)
            ax3.set_xlabel('Time (s)', fontsize=12)
            ax3.set_title('TCP: Playback Status', fontsize=13, fontweight='bold')
            ax3.set_xlim(time_min, time_max)
            ax3.sharex(ax1)
            ax3.tick_params(labelleft=False)
    else:
        ax3.text(0.5, 0.5, 'No TCP playback data', ha='center', va='center', transform=ax3.transAxes)
        ax3.set_xlabel('Time (s)', fontsize=12)
        ax3.set_title('TCP: Playback Status', fontsize=13, fontweight='bold')
        ax3.set_xlim(time_min, time_max)
        ax3.sharex(ax1)
        ax3.tick_params(labelleft=False)
    
    plt.tight_layout()
    return fig

def main():
    # File paths - determine script location and find log files
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)  # Go up one level from scripts/
    
    # Try multiple possible locations
    possible_quic_paths = [
        os.path.join(project_root, 'zquiclogs.txt'),
        os.path.join(os.getcwd(), 'zquiclogs.txt'),
        'zquiclogs.txt'
    ]
    possible_tcp_paths = [
        os.path.join(project_root, 'ztcplogs.txt'),
        os.path.join(os.getcwd(), 'ztcplogs.txt'),
        'ztcplogs.txt'
    ]
    
    quic_log = None
    tcp_log = None
    
    for path in possible_quic_paths:
        if os.path.exists(path):
            quic_log = path
            break
    
    for path in possible_tcp_paths:
        if os.path.exists(path):
            tcp_log = path
            break
    
    if not quic_log:
        print(f"Error: Could not find zquiclogs.txt in any of these locations:")
        for p in possible_quic_paths:
            print(f"  - {p}")
        return
    
    if not tcp_log:
        print(f"Error: Could not find ztcplogs.txt in any of these locations:")
        for p in possible_tcp_paths:
            print(f"  - {p}")
        return
    
    print(f"Using QUIC log: {quic_log}")
    print(f"Using TCP log: {tcp_log}")
    
    print("Parsing QUIC log file...")
    quic_bitrate = parse_bitrate_log(quic_log)
    quic_playback = parse_playback_log(quic_log)
    
    print("Parsing TCP log file...")
    tcp_bitrate = parse_bitrate_log(tcp_log)
    tcp_playback = parse_playback_log(tcp_log)
    
    print(f"\nQUIC Statistics:")
    print(f"  Bitrate entries: {len(quic_bitrate)}")
    print(f"  Playback events: {len(quic_playback)}")
    
    print(f"\nTCP Statistics:")
    print(f"  Bitrate entries: {len(tcp_bitrate)}")
    print(f"  Playback events: {len(tcp_playback)}")
    
    # Calculate playback statistics
    quic_play_time, quic_inter_time, quic_play_periods, quic_inter_periods = calculate_playback_stats(quic_playback)
    tcp_play_time, tcp_inter_time, tcp_play_periods, tcp_inter_periods = calculate_playback_stats(tcp_playback)
    
    print(f"\n{'='*60}")
    print(f"QUIC Playback Statistics:")
    print(f"  Total Playback Time: {quic_play_time:.2f} seconds")
    print(f"  Total Interruption Time: {quic_inter_time:.2f} seconds")
    print(f"  Number of Playback Periods: {len(quic_play_periods)}")
    print(f"  Number of Interruption Periods: {len(quic_inter_periods)}")
    
    print(f"\nTCP Playback Statistics:")
    print(f"  Total Playback Time: {tcp_play_time:.2f} seconds")
    print(f"  Total Interruption Time: {tcp_inter_time:.2f} seconds")
    print(f"  Number of Playback Periods: {len(tcp_play_periods)}")
    print(f"  Number of Interruption Periods: {len(tcp_inter_periods)}")
    print(f"{'='*60}\n")
    
    # Create plots
    print("Generating plots...")
    fig = plot_comparison(quic_bitrate, tcp_bitrate, quic_playback, tcp_playback)
    
    # Save figure
    output_file = 'bitrate_playback_comparison.png'
    fig.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to: {output_file}")
    
    plt.show()

if __name__ == '__main__':
    main()

