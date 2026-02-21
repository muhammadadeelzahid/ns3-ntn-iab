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
import csv
from collections import Counter

def get_base_dir():
    """
    Determine base directory for artifacts/logs.
    Prefer current working directory, but fall back to repo root
    (parent of this script) if artifacts are not found.
    """
    cwd = os.getcwd()
    script_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    
    for candidate in (cwd, script_root):
        if (os.path.exists(os.path.join(candidate, "Quic_artifacts")) or
            os.path.exists(os.path.join(candidate, "Tcp_artifacts"))):
            return candidate
    
    return cwd

def find_all_runs(base_dir):
    """
    Find all runs in Quic_artifacts and Tcp_artifacts.
    Returns a dict: { 'Algorithm Name': [list of run directories] }
    """
    run_groups = defaultdict(list)
    project_root = base_dir
    
    # Check Quic_artifacts
    quic_root = os.path.join(project_root, "Quic_artifacts")
    if os.path.exists(quic_root):
        for algo in os.listdir(quic_root):
            algo_path = os.path.join(quic_root, algo)
            if os.path.isdir(algo_path):
                # Find run_* directories
                runs = glob.glob(os.path.join(algo_path, "run_*"))
                if runs:
                    name = f"QUIC {algo}"
                    # Store absolute paths
                    run_groups[name].extend([os.path.abspath(r) for r in runs])

    # Check Tcp_artifacts
    tcp_root = os.path.join(project_root, "Tcp_artifacts")
    if os.path.exists(tcp_root):
        for algo in os.listdir(tcp_root):
            algo_path = os.path.join(tcp_root, algo)
            if os.path.isdir(algo_path):
                # Find run_* directories
                runs = glob.glob(os.path.join(algo_path, "run_*"))
                if runs:
                    name = f"TCP {algo}"
                    # Store absolute paths
                    run_groups[name].extend([os.path.abspath(r) for r in runs])
                    
    return run_groups

def map_logs_to_runs(base_dir):
    """
    Scan all .out files in the current directory to map them to run directories.
    Returns dict: { run_dir_abs_path: log_file_abs_path }
    """
    mapping = {}
    log_files = glob.glob(os.path.join(base_dir, "*.out"))
    project_root = base_dir
    
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
                    run_dir_abs = os.path.abspath(os.path.join(project_root, run_dir_rel))
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
        
    bitrate_pattern = re.compile(r'(\d*\.\d+|\d+)\s+(?:Node|ue-id):\s+(\d+)\s+newBitRate:\s+(\d+)', re.IGNORECASE)
    
    with open(filename, 'r') as f:
        for line in f:
            match = bitrate_pattern.search(line)
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
        
        interrupt_match = re.search(r'No frames to play at t=([0-9.]+)s\..*Player ID=(\d+)', line)
        if interrupt_match:
            time = float(interrupt_match.group(1))
            playback_events.append(('interrupted', time, current_resolution))
        
        resume_match = re.search(r'Play resumed at t=([0-9.]+)s\..*Player ID=(\d+)', line)
        if resume_match:
            time = float(resume_match.group(1))
            playback_events.append(('resumed', time, current_resolution))
            
    playback_events.sort(key=lambda x: x[1])
    return playback_events

def grep_lines(filename, pattern):
    """Run grep to extract matching lines from a large log file."""
    if not filename or not os.path.exists(filename):
        return []
    cmd = ["grep", "-a", "-E", pattern, filename]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        if proc.returncode not in (0, 1):  # 1 means no matches
            return []
        return proc.stdout.splitlines()
    except Exception:
        return []

def parse_ue_nodes_and_distances(err_file):
    """
    Parse UE node IDs (in install order) and closest-eNB distances from .err file.
    Returns (num_users, ue_node_ids, distances).
    """
    num_users = None
    ue_node_ids = []
    distances = []
    
    pattern = r"Install UE with IMSI|Actually created .* UE nodes|Number of UEs to create|Closest eNB found at distance"
    lines = grep_lines(err_file, pattern)
    
    for line in lines:
        created_match = re.search(r"Actually created\s+(\d+)\s+UE nodes", line)
        if created_match:
            num_users = int(created_match.group(1))
            continue
        
        create_match = re.search(r"Number of UEs to create:\s*(\d+)", line)
        if create_match:
            num_users = int(create_match.group(1))
            continue
        
        ue_match = re.search(r"Install UE with IMSI\s+\d+\s+on Node ID\s+(\d+)", line)
        if ue_match:
            ue_node_ids.append(int(ue_match.group(1)))
            continue
        
        dist_match = re.search(r"Closest eNB found at distance:\s*([0-9.]+)", line)
        if dist_match:
            distances.append(float(dist_match.group(1)))
    
    if num_users is None and ue_node_ids:
        num_users = len(ue_node_ids)
    
    return num_users, ue_node_ids, distances

def parse_playback_log_per_user(filename):
    """
    Parse playback events grouped by PlayerId from a log file.
    Returns dict: { player_id: [events] }
    """
    events_by_player = defaultdict(list)
    if not filename or not os.path.exists(filename):
        return events_by_player
    
    pattern = r"PLAYING FRAME|No frames to play|Play resumed"
    lines = grep_lines(filename, pattern)
    
    for line in lines:
        play_match = re.search(r'(\d+\.?\d*)\s+PLAYING FRAME:.*?PlayerId:\s*(\d+).*?Res:\s*(\d+)', line)
        if play_match:
            time = float(play_match.group(1))
            player_id = int(play_match.group(2))
            resolution = float(play_match.group(3))
            events_by_player[player_id].append(('playing', time, resolution))
            continue
        
        interrupt_match = re.search(r'No frames to play at t=([0-9.]+)s\.\s+Player ID=(\d+)', line)
        if interrupt_match:
            time = float(interrupt_match.group(1))
            player_id = int(interrupt_match.group(2))
            events_by_player[player_id].append(('interrupted', time, None))
            continue
        
        resume_match = re.search(r'Play resumed at t=([0-9.]+)s\.\s+Player ID=(\d+)', line)
        if resume_match:
            time = float(resume_match.group(1))
            player_id = int(resume_match.group(2))
            events_by_player[player_id].append(('resumed', time, None))
            continue
    
    # Sort events for each player
    for player_id, events in events_by_player.items():
        events.sort(key=lambda x: x[1])
    
    return events_by_player

def parse_bitrate_log_per_user(filename, node_ids=None):
    """
    Parse bitrate info from .out logs, grouped by node/ue-id.
    Returns dict: { node_id: [(time, bitrate_kbps)] }
    """
    bitrate_by_node = defaultdict(list)
    if not filename or not os.path.exists(filename):
        return bitrate_by_node
    
    pattern = re.compile(r'(\d*\.\d+|\d+)\s+(?:Node|ue-id):\s+(\d+)\s+newBitRate:\s+(\d+)', re.IGNORECASE)
    
    with open(filename, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                time = float(match.group(1))
                node_id = int(match.group(2))
                bitrate_kbps = float(match.group(3)) / 1e3
                if node_ids is None or node_id in node_ids:
                    bitrate_by_node[node_id].append((time, bitrate_kbps))
    
    # Ensure each series is sorted by time
    for node_id, series in bitrate_by_node.items():
        series.sort(key=lambda x: x[0])
    
    return bitrate_by_node

def compute_playback_metrics(playback_events):
    """Compute playback metrics for a single user."""
    if not playback_events:
        return {
            "avg_playback_bitrate_kbps": 0.0,
            "playback_time": 0.0,
            "interruption_time": 0.0,
            "rebuffer_ratio": 0.0,
            "stall_count": 0,
            "bitrate_switch_count": 0
        }
    
    playback_time, interruption_time, _, _ = calculate_playback_stats(playback_events)
    
    stall_count = sum(1 for evt in playback_events if evt[0] == 'interrupted')
    total_time = playback_time + interruption_time
    rebuffer_ratio = (interruption_time / total_time) if total_time > 0 else 0.0
    
    return {
        "playback_time": playback_time,
        "interruption_time": interruption_time,
        "rebuffer_ratio": rebuffer_ratio,
        "stall_count": stall_count,
        "bitrate_switch_count": 0
    }

def compute_mean_throughput_kbps(rx_data):
    """Compute mean throughput in Kbps from rx data samples."""
    if not rx_data or len(rx_data) < 2:
        return 0.0
    total_bytes = sum(size for _, size in rx_data)
    duration = rx_data[-1][0] - rx_data[0][0]
    if duration <= 0:
        return 0.0
    return (total_bytes * 8) / (duration * 1e3)

def get_user_rx_file(run_dir, protocol_name, node_id, player_id=None):
    """Resolve rx data file for a user."""
    if protocol_name == "QUIC":
        prefix = "clientQUIC-rx-data"
    else:
        prefix = "clientTCP-rx-data"
    
    if node_id is not None:
        candidate = os.path.join(run_dir, f"{prefix}{node_id}.txt")
        if os.path.exists(candidate):
            return candidate
    
    if player_id is not None:
        candidate = os.path.join(run_dir, f"{prefix}{player_id}.txt")
        if os.path.exists(candidate):
            return candidate
    
    return None

def get_user_rtt_file(run_dir, protocol_name, node_id):
    if protocol_name == "QUIC":
        filename = f"clientQUIC-rtt{node_id}.txt"
    else:
        filename = f"clientTCP-rtt{node_id}.txt"
    file_path = os.path.join(run_dir, filename)
    return file_path if os.path.exists(file_path) else None

def get_user_cwnd_file(run_dir, protocol_name, node_id):
    if protocol_name == "QUIC":
        filename = f"clientQUIC-cwnd-change{node_id}.txt"
    else:
        filename = f"clientTCP-cwnd-change{node_id}.txt"
    file_path = os.path.join(run_dir, filename)
    return file_path if os.path.exists(file_path) else None

def generate_user_summary_plot(user_row, output_dir, title_prefix):
    """Create a per-user figure with bitrate/throughput/rtt/cwnd over time."""
    fig, axes = plt.subplots(3, 2, figsize=(12, 10))
    axes = axes.flatten()
    
    plots = [
        ("Bitrate over time (Kbps)", "bitrate_series", "Kbps"),
        ("Throughput over time (Kbps)", "throughput_series", "Kbps"),
        ("RTT over time", "rtt_series", "RTT"),
        ("CWND over time", "cwnd_series", "CWND"),
        ("Playback state", "playback_state_series", "State")
    ]
    
    for ax, (title, key, ylabel) in zip(axes, plots):
        series = user_row.get(key, [])
        if series:
            times = [t for t, _ in series]
            values = [v for _, v in series]
            if key == "playback_state_series":
                ax.fill_between(times, 0, values, step="post", alpha=0.3, color="green")
                ax.fill_between(times, values, 1, step="post", alpha=0.3, color="red")
            else:
                ax.step(times, values, where="post", color="steelblue", linewidth=1.3)
            ax.set_xlabel("Time (s)", fontsize=9)
            ax.set_ylabel(ylabel, fontsize=9)
        else:
            ax.text(0.5, 0.5, "No data", ha="center", va="center")
        ax.set_title(title, fontsize=10, fontweight="bold", pad=6)
        ax.set_xlim(0, 60)
        ax.grid(True, alpha=0.3)
    
    # Hide any unused axes
    for ax in axes[len(plots):]:
        ax.axis("off")
    
    fig.suptitle(f"{title_prefix} (Node {user_row['node_id']})", fontsize=12, fontweight="bold", y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    filename = f"user_summary_{user_row['algo'].replace(' ', '_')}_{user_row['run_id']}_player{user_row['player_id']}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches="tight")
    plt.close(fig)

def generate_all_users_summary_plot(rows, output_dir, title_prefix):
    """Create combined plots for bitrate/throughput/rtt/cwnd over time."""
    if not rows:
        return
    
    fig, axes = plt.subplots(3, 2, figsize=(14, 12))
    axes = axes.flatten()
    
    plots = [
        ("Bitrate over time (Kbps)", "bitrate_series", "Kbps"),
        ("Throughput over time (Kbps)", "throughput_series", "Kbps"),
        ("RTT over time", "rtt_series", "RTT"),
        ("CWND over time", "cwnd_series", "CWND"),
        ("Playback state (1=play, 0=stall)", "playback_state_series", "State")
    ]
    
    for ax, (title, key, ylabel) in zip(axes, plots):
        for r in rows:
            series = r.get(key, [])
            if series:
                times = [t for t, _ in series]
                values = [v for _, v in series]
                if key == "playback_state_series":
                    ax.fill_between(times, 0, values, step="post", alpha=0.2, color="green")
                    ax.fill_between(times, values, 1, step="post", alpha=0.2, color="red")
                else:
                    ax.step(times, values, where="post", linewidth=1.1, label=f"P{r['player_id']} (N{r['node_id']})")
        ax.set_title(title, fontsize=11, fontweight="bold", pad=6)
        ax.set_xlabel("Time (s)", fontsize=9)
        ax.set_ylabel(ylabel, fontsize=9)
        ax.set_xlim(0, 60)
        ax.grid(True, alpha=0.3)
        if key != "playback_state_series":
            ax.legend(fontsize=7, ncol=2)
    
    # Hide any unused axes
    for ax in axes[len(plots):]:
        ax.axis("off")
    
    fig.suptitle(title_prefix, fontsize=14, fontweight="bold", y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    filename = f"users_summary_{rows[0]['algo'].replace(' ', '_')}_{rows[0]['run_id']}.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches="tight")
    plt.close(fig)


def _average_series_on_grid(series_list, grid):
    """Interpolate each series onto grid and return mean across series."""
    if not series_list or not grid.size:
        return None
    interp_values = []
    for series in series_list:
        if not series:
            continue
        times = np.array([t for t, _ in series])
        values = np.array([v for _, v in series])
        if times.size == 0:
            continue
        v = np.interp(grid, times, values)
        interp_values.append(v)
    if not interp_values:
        return None
    return np.mean(interp_values, axis=0)


def _plot_aggregated_user_summaries(rows, output_dir):
    """
    Average time series across runs and users per algorithm.
    Combined plot: bitrate, throughput, rtt, cwnd with both algorithms.
    Separate plots: playback state per algorithm.
    """
    grid = np.arange(0, 60.5, 0.5)
    time_grid = grid.tolist()

    # Group by algo
    by_algo = defaultdict(list)
    for r in rows:
        by_algo[r["algo"]].append(r)

    if not by_algo:
        return

    # Combined figure: 4 shared subplots (bitrate, throughput, rtt, cwnd) + 1 playback state per algorithm
    n_algos = len(by_algo)
    n_axes = 4 + n_algos  # 4 metrics + playback per algo
    n_cols = 2
    n_rows = (n_axes + n_cols - 1) // n_cols
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(14, 4 * n_rows))
    axes = axes.flatten()
    algo_list = sorted(by_algo.keys())
    colors = {algo: f"C{i % 10}" for i, algo in enumerate(algo_list)}

    # Metrics shared by both algos
    metric_plots = [
        ("Bitrate over time (Kbps)", "bitrate_series", "Kbps"),
        ("Throughput over time (Kbps)", "throughput_series", "Kbps"),
        ("RTT over time", "rtt_series", "RTT"),
        ("CWND over time", "cwnd_series", "CWND"),
    ]
    for ax, (title, key, ylabel) in zip(axes[:4], metric_plots):
        for algo in algo_list:
            series_list = [r.get(key, []) for r in by_algo[algo]]
            avg = _average_series_on_grid(series_list, grid)
            if avg is not None:
                ax.step(time_grid, avg, where="post", linewidth=1.5, label=algo, color=colors[algo])
        ax.set_title(title, fontsize=11, fontweight="bold", pad=6)
        ax.set_xlabel("Time (s)", fontsize=9)
        ax.set_ylabel(ylabel, fontsize=9)
        ax.set_xlim(0, 60)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)

    # Separate playback state subplot per algorithm
    for i, algo in enumerate(algo_list):
        ax = axes[4 + i]
        series_list = [r.get("playback_state_series", []) for r in by_algo[algo]]
        avg = _average_series_on_grid(series_list, grid)
        if avg is not None:
            ax.fill_between(time_grid, 0, avg, step="post", alpha=0.3, color="green")
            ax.fill_between(time_grid, avg, 1, step="post", alpha=0.3, color="red")
        ax.set_title(f"Playback state (1=play, 0=stall) – {algo}", fontsize=11, fontweight="bold", pad=6)
        ax.set_xlabel("Time (s)", fontsize=9)
        ax.set_ylabel("State")
        ax.set_xlim(0, 60)
        ax.set_ylim(0, 1)
        ax.grid(True, alpha=0.3)

    for ax in axes[n_axes:]:
        ax.axis("off")
    fig.suptitle("User Summary (avg across runs)", fontsize=14, fontweight="bold", y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(os.path.join(output_dir, "users_summary_combined.png"), dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("Saved users_summary_combined.png")


def calculate_playback_stats(playback_events, end_time=None):
    """
    Calculate total playback and interruption times.
    If end_time is provided (and > last event time), extends the final state to end_time.
    """
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
            
    # Handle final segment
    last_event_time = sorted_events[-1][1]
    final_time = last_event_time
    
    # Extend to end_time if provided and greater than last event
    if end_time is not None and end_time > last_event_time:
        final_time = end_time
        
    if state == 'playing' and state_start_time is not None:
        playback_time += final_time - state_start_time
        playback_periods.append((state_start_time, final_time))
    elif state == 'interrupted' and state_start_time is not None:
        interruption_time += final_time - state_start_time
        interruption_periods.append((state_start_time, final_time))
        
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

def parse_rtt_data(file_path):
    """Parse RTT data files (timestamp, old_rtt, new_rtt)."""
    data = []
    if not file_path or not os.path.exists(file_path):
        return data
    try:
        with open(file_path, 'r') as f:
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 3:
                    try:
                        timestamp = float(parts[0])
                        new_rtt = float(parts[2])
                        data.append((timestamp, new_rtt))
                    except ValueError:
                        continue
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return data

def parse_cwnd_data(file_path):
    """Parse cwnd data files (timestamp, old_cwnd, new_cwnd)."""
    data = []
    if not file_path or not os.path.exists(file_path):
        return data
    try:
        with open(file_path, 'r') as f:
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 3:
                    try:
                        timestamp = float(parts[0])
                        new_cwnd = float(parts[2])
                        data.append((timestamp, new_cwnd))
                    except ValueError:
                        continue
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return data

def calculate_data_rate_kbps(rx_data, time_interval=0.5):
    """Calculate throughput (Kbps) over time bins from rx data."""
    if not rx_data:
        return []
    rx_data = sorted(rx_data, key=lambda x: x[0])
    min_time = rx_data[0][0]
    max_time = rx_data[-1][0]
    bins = np.arange(min_time, max_time + time_interval, time_interval)
    series = []
    idx = 0
    for i in range(len(bins) - 1):
        start_time = bins[i]
        end_time = bins[i + 1]
        total_bytes = 0
        while idx < len(rx_data) and rx_data[idx][0] < end_time:
            if rx_data[idx][0] >= start_time:
                total_bytes += rx_data[idx][1]
            idx += 1
        if total_bytes > 0:
            kbps = (total_bytes * 8) / (time_interval * 1e3)
            series.append((start_time + time_interval / 2, kbps))
    return series

def calculate_value_over_time(data, time_interval=0.5):
    """Calculate last value per time bin (e.g., RTT, CWND)."""
    if not data:
        return []
    data = sorted(data, key=lambda x: x[0])
    min_time = data[0][0]
    max_time = data[-1][0]
    bins = np.arange(min_time, max_time + time_interval, time_interval)
    series = []
    idx = 0
    last_val = None
    for i in range(len(bins) - 1):
        start_time = bins[i]
        end_time = bins[i + 1]
        while idx < len(data) and data[idx][0] < end_time:
            if data[idx][0] >= start_time:
                last_val = data[idx][1]
            idx += 1
        if last_val is not None:
            series.append((start_time + time_interval / 2, last_val))
    return series

def calculate_playback_state_over_time(playback_events, time_interval=0.5, max_time=60.0):
    """
    Build a time series of playback state (1=playing, 0=interrupted) over time bins.
    """
    if not playback_events:
        return []
    
    # Ensure events are sorted
    events = sorted(playback_events, key=lambda x: x[1])
    bins = np.arange(0, max_time + time_interval, time_interval)
    series = []
    
    # Track state across time
    state = 0
    evt_idx = 0
    for i in range(len(bins) - 1):
        t_center = bins[i] + time_interval / 2
        # Advance events up to this time
        while evt_idx < len(events) and events[evt_idx][1] <= t_center:
            evt_type = events[evt_idx][0]
            if evt_type in ("playing", "resumed"):
                state = 1
            elif evt_type == "interrupted":
                state = 0
            evt_idx += 1
        series.append((t_center, state))
    
    return series

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

def aggregate_bitrate_percentiles(all_runs_bitrate_by_node, time_bin=1.0, global_max_time=None):
    """
    For each run, compute per-time percentile across users (nodes),
    then aggregate across runs by taking the median of each percentile curve.
    Returns (time_points, p50, p25, p75).
    """
    if not all_runs_bitrate_by_node:
        return [], [], [], []
    
    # Determine global time range
    min_time = float('inf')
    max_time = float('-inf')
    for run_nodes in all_runs_bitrate_by_node:
        for series in run_nodes.values():
            if series:
                min_time = min(min_time, series[0][0])
                max_time = max(max_time, series[-1][0])
    
    if global_max_time is not None:
        max_time = max(max_time, global_max_time)
    
    if min_time == float('inf'):
        return [], [], [], []
    
    bins = np.arange(min_time, max_time + time_bin, time_bin)
    bin_centers = bins[:-1] + time_bin / 2
    
    run_p50 = []
    run_p25 = []
    run_p75 = []
    
    for run_nodes in all_runs_bitrate_by_node:
        # Pre-sort each node series
        node_series = {}
        for node_id, series in run_nodes.items():
            if series:
                node_series[node_id] = sorted(series, key=lambda x: x[0])
        
        if not node_series:
            continue
        
        # Track current index per node
        node_idx = {nid: 0 for nid in node_series}
        node_last = {nid: 0 for nid in node_series}
        
        p50_vals = []
        p25_vals = []
        p75_vals = []
        
        for bin_end in bins[1:]:
            values = []
            for nid, series in node_series.items():
                idx = node_idx[nid]
                while idx < len(series) and series[idx][0] <= bin_end:
                    node_last[nid] = series[idx][1]
                    idx += 1
                node_idx[nid] = idx
                values.append(node_last[nid])
            
            if values:
                p50_vals.append(float(np.percentile(values, 50)))
                p25_vals.append(float(np.percentile(values, 25)))
                p75_vals.append(float(np.percentile(values, 75)))
            else:
                p50_vals.append(0.0)
                p25_vals.append(0.0)
                p75_vals.append(0.0)
        
        run_p50.append(p50_vals)
        run_p25.append(p25_vals)
        run_p75.append(p75_vals)
    
    if not run_p50:
        return [], [], [], []
    
    run_p50 = np.array(run_p50)
    run_p25 = np.array(run_p25)
    run_p75 = np.array(run_p75)
    
    median_p50 = np.median(run_p50, axis=0).tolist()
    median_p25 = np.median(run_p25, axis=0).tolist()
    median_p75 = np.median(run_p75, axis=0).tolist()
    
    return bin_centers, median_p50, median_p25, median_p75

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
    if not stats['bitrate_mean'] and not stats.get('bitrate_p50'):
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
    # Prefer percentile-based typical time series if available
    if stats.get('bitrate_p50'):
        times = stats['bitrate_p_time']
        p50 = [b/1e6 for b in stats['bitrate_p50']]
        p25 = [b/1e6 for b in stats['bitrate_p25']]
        p75 = [b/1e6 for b in stats['bitrate_p75']]
    else:
        times = stats['bitrate_time']
        p50 = [b/1e6 for b in stats['bitrate_mean']]
        p25 = [b/1e6 for b in stats['bitrate_mean']]
        p75 = [b/1e6 for b in stats['bitrate_mean']]
    
    # Truncate at T=60 and pad missing data with zeros using quantized bins
    # Save original times for stds processing
    times_orig = times
    times, p50 = truncate_and_pad_data(times, p50, max_time=60.0, end_time=63.0)
    times_p25, p25 = truncate_and_pad_data(times_orig, p25, max_time=60.0, end_time=63.0)
    times_p75, p75 = truncate_and_pad_data(times_orig, p75, max_time=60.0, end_time=63.0)
    
    # Ensure lengths match - use times from means and interpolate/pad stds if needed
    times = np.array(times)
    p50 = np.array(p50)
    p25 = np.array(p25)
    p75 = np.array(p75)
    
    if len(times) != len(times_p25) or not np.allclose(times, times_p25):
        if len(p25) > 0 and len(times_p25) > 0:
            p25 = np.interp(times, times_p25, p25)
        else:
            p25 = np.zeros(len(times))
    if len(times) != len(times_p75) or not np.allclose(times, times_p75):
        if len(p75) > 0 and len(times_p75) > 0:
            p75 = np.interp(times, times_p75, p75)
        else:
            p75 = np.zeros(len(times))
    
    # Convert back to lists for consistency
    times = times.tolist()
    p50 = p50.tolist()
    p25 = p25.tolist()
    p75 = p75.tolist()
    
    ax.plot(times, p50, 'b-', linewidth=2, label=f"{algo_name} (Median)")
    ax.fill_between(times, p25, p75, color='blue', alpha=0.2)
    
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
        if protocol_type in name and (stats['bitrate_mean'] or stats.get('bitrate_p50')):
            has_data = True
            if stats.get('bitrate_p50'):
                times = stats['bitrate_p_time']
                means = [b/1e6 for b in stats['bitrate_p50']]
            else:
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
                
                interrupt_match = re.search(r'No frames to play at t=([0-9.]+)s\..*Player ID=(\d+)', content)
                if interrupt_match:
                    time = float(interrupt_match.group(1))
                    results[filename].append(('interrupted', time, None))
                    continue
                
                resume_match = re.search(r'Play resumed at t=([0-9.]+)s\..*Player ID=(\d+)', content)
                if resume_match:
                    time = float(resume_match.group(1))
                    results[filename].append(('resumed', time, None))
                    
        except Exception as e:
            print(f"Error in grep batch: {e}")
            
    # Post-process to handle 'resumed' logic and sorting
    final_results = {}
    for filename, events in results.items():
        events.sort(key=lambda x: x[1])
        final_results[filename] = events
        
    return final_results

def process_runs(runs, protocol_name, log_mapping, client_nodes=None):
    """
    Process all runs for a protocol and return aggregated stats.
    client_nodes: Optional list of client node IDs. If provided, bitrate will be aggregated across these nodes.
    """
    all_bitrates = []
    all_bitrates_by_node = []
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
        bitrate_by_node = {}
        playback = []
        
        if log_file:
            # Parse bitrate with optional node filtering
            bitrate = parse_bitrate_log(log_file, node_ids=client_nodes)
            bitrate_by_node = parse_bitrate_log_per_user(log_file, node_ids=client_nodes)
            
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
        all_bitrates_by_node.append(bitrate_by_node)
        all_playback.append(playback)
        
        # Throughput from DashClientRx files in run dir
        prefix = "DashClientRx_UE_" if "QUIC" in protocol_name else "DashClientRx_TCP_UE_"
        tp = parse_dash_throughput(run_dir, prefix)
        all_throughputs.extend(tp)
            
    # Calculate global max time
    # Initialize to 60.0 to ensure interruptions are calculated up to simulation end
    global_max_time = 60.0
    
    for run in all_playback:
        if run:
            global_max_time = max(global_max_time, run[-1][1])
            
    # Check bitrate logs as well
    for run in all_bitrates:
        if run:
            global_max_time = max(global_max_time, run[-1][0])
            
    # Stats
    for playback in all_playback:
         pt, it, _, _ = calculate_playback_stats(playback, end_time=global_max_time)
         total_play_time.append(pt)
         total_inter_time.append(it)
            
    # Aggregate Bitrate
    b_time, b_mean, b_std = aggregate_bitrate(all_bitrates, global_max_time=global_max_time)
    p_time, p50, p25, p75 = aggregate_bitrate_percentiles(
        all_bitrates_by_node,
        global_max_time=global_max_time
    )
    
    return {
        'count': len(runs),
        'bitrate_time': b_time,
        'bitrate_mean': b_mean,
        'bitrate_std': b_std,
        'bitrate_p_time': p_time,
        'bitrate_p50': p50,
        'bitrate_p25': p25,
        'bitrate_p75': p75,
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
        probs = smooth_data(probs, window_size=15) # Increased smoothing
        
        # Truncate at T=60 and pad missing data with interruption probability = 1.0 using quantized bins
        times, probs = truncate_and_pad_playback_prob(times, probs, max_time=60.0, end_time=63.0, time_bin=0.1) # Finer granularity
        
        playing_probs = [1.0 - p for p in probs]
        
        ax.fill_between(times, 0, playing_probs, color='green', alpha=0.3, label='Playing') # Softer alpha
        ax.fill_between(times, playing_probs, 1.0, color='red', alpha=0.3, label='Interrupted') # Softer alpha
        
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

def plot_interruption_duration_boxplot(all_stats, output_dir):
    """
    Create a box plot showing total interruption duration for each protocol-CC combination.
    Y-axis: Total interruption duration (seconds)
    X-axis: Protocol-CC combinations (e.g., QUIC-BBR, TCP-Cubic)
    Prints detailed statistics (mean, median, IQR, etc.) for each protocol-CC.
    """
    print("\n" + "=" * 80)
    print("INTERRUPTION DURATION STATISTICS BY PROTOCOL-CC")
    print("=" * 80)
    
    # Collect interruption durations grouped by protocol-CC
    interruption_data = defaultdict(list)
    
    print("\nStep 1: Collecting interruption data from all runs...")
    for algo_name, stats in all_stats.items():
        if 'inter_times' not in stats or not stats['inter_times']:
            continue
        
        # Parse algorithm name to extract protocol and CC
        # Format is typically "QUIC BBR" or "TCP Cubic"
        parts = algo_name.split()
        if len(parts) >= 2:
            protocol = parts[0]  # QUIC or TCP
            cc = ' '.join(parts[1:])  # BBR, Cubic, etc.
            protocol_cc = f"{protocol}-{cc}"
        else:
            # Fallback: use full name
            protocol_cc = algo_name.replace(' ', '-')
        
        # Add interruption durations for this protocol-CC
        num_runs = len(stats['inter_times'])
        interruption_data[protocol_cc].extend(stats['inter_times'])
        print(f"  {protocol_cc}: Added {num_runs} runs (total: {len(interruption_data[protocol_cc])} samples)")
    
    if not interruption_data:
        print("No interruption data available for box plot.")
        return
    
    # Prepare data for box plot and calculate statistics
    labels = []
    data = []
    statistics = {}
    
    print("\nStep 2: Calculating statistics for each protocol-CC...")
    print("-" * 80)
    
    # Sort labels for consistent ordering (QUIC first, then TCP, alphabetically)
    sorted_keys = sorted(interruption_data.keys(), key=lambda x: (x.startswith('TCP'), x))
    
    for protocol_cc in sorted_keys:
        durations = interruption_data[protocol_cc]
        if not durations:  # Skip if no data
            continue
        
        # Convert to numpy array for easier calculations
        durations_array = np.array(durations)
        
        # Calculate statistics
        mean_val = np.mean(durations_array)
        median_val = np.median(durations_array)
        q1 = np.percentile(durations_array, 25)
        q3 = np.percentile(durations_array, 75)
        iqr = q3 - q1
        std_dev = np.std(durations_array)
        min_val = np.min(durations_array)
        max_val = np.max(durations_array)
        num_samples = len(durations_array)
        
        # Store statistics
        statistics[protocol_cc] = {
            'mean': mean_val,
            'median': median_val,
            'q1': q1,
            'q3': q3,
            'iqr': iqr,
            'std': std_dev,
            'min': min_val,
            'max': max_val,
            'count': num_samples
        }
        
        # Print statistics for this protocol-CC
        print(f"\n{protocol_cc}:")
        print(f"  Number of samples: {num_samples}")
        print(f"  Mean:              {mean_val:.4f} seconds")
        print(f"  Median:            {median_val:.4f} seconds")
        print(f"  Q1 (25th %ile):    {q1:.4f} seconds")
        print(f"  Q3 (75th %ile):    {q3:.4f} seconds")
        print(f"  IQR (Q3 - Q1):     {iqr:.4f} seconds")
        print(f"  Std Deviation:     {std_dev:.4f} seconds")
        print(f"  Min:               {min_val:.4f} seconds")
        print(f"  Max:               {max_val:.4f} seconds")
        
        labels.append(protocol_cc)
        data.append(durations)
    
    if not data:
        print("No valid interruption data for box plot.")
        return
    
    print("\n" + "-" * 80)
    print("Step 3: Creating box plot...")
    
    # Create box plot
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Create box plot
    bp = ax.boxplot(data, labels=labels, patch_artist=True, 
                    showmeans=True, meanline=True,
                    boxprops=dict(linewidth=2),
                    medianprops=dict(linewidth=2, color='red'),
                    meanprops=dict(linewidth=2, linestyle='--', color='green'),
                    whiskerprops=dict(linewidth=2),
                    capprops=dict(linewidth=2))
    
    # Color boxes by protocol (QUIC = red, TCP = blue)
    colors = []
    for label in labels:
        if label.startswith('QUIC'):
            colors.append('lightcoral')
        elif label.startswith('TCP'):
            colors.append('lightblue')
        else:
            colors.append('lightgray')
    
    for patch, color in zip(bp['boxes'], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.7)
    
    ax.set_xlabel('Protocol-CC Algorithm', fontsize=14, fontweight='bold')
    ax.set_ylabel('Total Interruption Duration (seconds)', fontsize=14, fontweight='bold')
    ax.set_title('Interruption Duration Distribution by Protocol-CC', fontsize=16, fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    
    # Rotate x-axis labels if needed
    plt.xticks(rotation=45, ha='right')
    
    # Add legend
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='lightcoral', alpha=0.7, label='QUIC'),
        Patch(facecolor='lightblue', alpha=0.7, label='TCP'),
        plt.Line2D([0], [0], color='red', linewidth=2, label='Median'),
        plt.Line2D([0], [0], color='green', linewidth=2, linestyle='--', label='Mean')
    ]
    ax.legend(handles=legend_elements, loc='upper right')
    
    plt.tight_layout()
    
    filename = "interruption_duration_boxplot.png"
    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches='tight')
    plt.close(fig)
    print(f"  Saved {filename}")
    
    # Step 4: Print summary comparison table
    print("\n" + "-" * 80)
    print("Step 4: Summary Comparison Table")
    print("-" * 80)
    print(f"{'Protocol-CC':<20} {'Mean':>12} {'Median':>12} {'IQR':>12} {'Std Dev':>12} {'Count':>8}")
    print("-" * 80)
    
    for protocol_cc in sorted_keys:
        if protocol_cc in statistics:
            stats = statistics[protocol_cc]
            print(f"{protocol_cc:<20} {stats['mean']:>12.4f} {stats['median']:>12.4f} "
                  f"{stats['iqr']:>12.4f} {stats['std']:>12.4f} {stats['count']:>8}")
    
    print("-" * 80)
    
    # Step 5: Find best and worst performers
    print("\nStep 5: Performance Ranking (by Mean Interruption Duration)")
    print("-" * 80)
    
    # Sort by mean interruption duration (lower is better)
    sorted_by_mean = sorted(statistics.items(), key=lambda x: x[1]['mean'])
    
    print("Best (lowest interruption duration) to Worst (highest):")
    for rank, (protocol_cc, stats) in enumerate(sorted_by_mean, 1):
        print(f"  {rank}. {protocol_cc:<20} Mean: {stats['mean']:.4f}s, Median: {stats['median']:.4f}s")
    
    print("\n" + "=" * 80)
    print("Interruption Duration Analysis Complete!")
    print("=" * 80 + "\n")

def generate_per_user_stats(run_groups, log_mapping, output_dir):
    """Generate per-user statistics CSV and plots for each run."""
    rows = []
    rows_for_csv = []
    dumped_first_user_by_algo = set()
    print("\nGenerating per-user plots...")
    for algo_name, runs in run_groups.items():
        print(f"Processing algorithm: {algo_name}")
        protocol_name = "QUIC" if "QUIC" in algo_name else "TCP"
        def _run_sort_key(path):
            base = os.path.basename(path)
            match = re.search(r"run_(\d+)", base)
            return int(match.group(1)) if match else base
        runs_sorted = sorted(runs, key=_run_sort_key)
        
        for run_dir in runs_sorted:
            run_id = os.path.basename(run_dir)
            log_file = log_mapping.get(run_dir)
            if not log_file:
                continue
            
            err_file = log_file.replace(".out", ".err")
            if not os.path.exists(err_file):
                err_file = log_file
            
            num_users, ue_node_ids, distances = parse_ue_nodes_and_distances(err_file)
            playback_by_user = parse_playback_log_per_user(err_file)
            
            if num_users is None:
                if ue_node_ids:
                    num_users = len(ue_node_ids)
                elif playback_by_user:
                    num_users = max(playback_by_user.keys()) + 1
                else:
                    continue
            
            run_rows = []
            
            for player_id in range(num_users):
                print(f"Processing player {player_id}...")
                node_id = ue_node_ids[player_id] if player_id < len(ue_node_ids) else None
                closest_distance = distances[player_id] if player_id < len(distances) else 0.0
                
                events = playback_by_user.get(player_id, [])
                playback_metrics = compute_playback_metrics(events)
                
                # Build bitrate series from PLAYING FRAME lines in .err
                raw_bitrate_series = [
                    (evt[1], evt[2] / 1e3)
                    for evt in events
                    if evt[0] == "playing" and evt[2] is not None
                ]
                
                avg_bitrate_kbps = 0.0
                if raw_bitrate_series:
                    avg_bitrate_kbps = sum(v for _, v in raw_bitrate_series) / len(raw_bitrate_series)
                
                # Compress to change-points for plotting bitrate switches
                bitrate_series = []
                bitrate_switch_count = 0
                last_val = None
                for time, val in raw_bitrate_series:
                    if last_val is None:
                        bitrate_series.append((time, val))
                        last_val = val
                        continue
                    if val != last_val:
                        bitrate_switch_count += 1
                        bitrate_series.append((time, val))
                        last_val = val
                
                rx_file = get_user_rx_file(run_dir, protocol_name, node_id, player_id=player_id)
                if not rx_file:
                    raise FileNotFoundError(f"Missing client RX file for node {node_id} in {run_dir}")
                rx_data = parse_client_rx_data(rx_file)
                throughput_series = calculate_data_rate_kbps(rx_data, time_interval=0.5)
                mean_throughput = sum(v for _, v in throughput_series) / len(throughput_series) if throughput_series else 0.0
                
                rtt_file = get_user_rtt_file(run_dir, protocol_name, node_id)
                if not rtt_file:
                    raise FileNotFoundError(f"Missing client RTT file for node {node_id} in {run_dir}")
                rtt_data = parse_rtt_data(rtt_file)
                rtt_series = sorted(rtt_data, key=lambda x: x[0])
                avg_rtt = sum(v for _, v in rtt_series) / len(rtt_series) if rtt_series else 0.0
                
                cwnd_file = get_user_cwnd_file(run_dir, protocol_name, node_id)
                if not cwnd_file:
                    raise FileNotFoundError(f"Missing client CWND file for node {node_id} in {run_dir}")
                cwnd_data = parse_cwnd_data(cwnd_file)
                cwnd_series = sorted(cwnd_data, key=lambda x: x[0])
                avg_cwnd = sum(v for _, v in cwnd_series) / len(cwnd_series) if cwnd_series else 0.0
                
                if algo_name not in dumped_first_user_by_algo and run_dir == runs_sorted[0] and player_id == 0:
                    safe_algo = algo_name.replace(" ", "_")
                    # Write separate tables per metric
                    throughput_path = os.path.join(output_dir, f"first_user_throughput_{safe_algo}.csv")
                    rtt_path = os.path.join(output_dir, f"first_user_rtt_{safe_algo}.csv")
                    cwnd_path = os.path.join(output_dir, f"first_user_cwnd_{safe_algo}.csv")
                    
                    with open(throughput_path, "w", newline="") as f:
                        writer = csv.writer(f)
                        writer.writerow(["time", "throughput_kbps"])
                        for t, v in throughput_series:
                            writer.writerow([f"{t:.3f}", f"{v}"])
                    
                    with open(rtt_path, "w", newline="") as f:
                        writer = csv.writer(f)
                        writer.writerow(["time", "rtt"])
                        for t, v in rtt_series:
                            writer.writerow([f"{t:.3f}", f"{v}"])
                    
                    with open(cwnd_path, "w", newline="") as f:
                        writer = csv.writer(f)
                        writer.writerow(["time", "cwnd"])
                        for t, v in cwnd_series:
                            writer.writerow([f"{t:.3f}", f"{v}"])
                    
                    dumped_first_user_by_algo.add(algo_name)
                    print(
                        f"Saved first user time series for {algo_name}: "
                        f"{throughput_path}, {rtt_path}, {cwnd_path}"
                    )

                row = {
                    "algo": algo_name,
                    "protocol": protocol_name,
                    "run_id": run_id,
                    "player_id": player_id,
                    "node_id": node_id if node_id is not None else -1,
                    "closest_enb_distance": closest_distance,
                    "mean_throughput_kbps": mean_throughput,
                    "avg_rtt": avg_rtt,
                    "avg_cwnd": avg_cwnd,
                    "avg_playback_bitrate_kbps": avg_bitrate_kbps,
                    "playback_duration": playback_metrics["playback_time"],
                    "interruption_duration": playback_metrics["interruption_time"],
                    "rebuffer_ratio": playback_metrics["rebuffer_ratio"],
                    "stall_count": playback_metrics["stall_count"],
                    "bitrate_switch_count": bitrate_switch_count,
                    "bitrate_series": bitrate_series,
                    "throughput_series": throughput_series,
                    "rtt_series": rtt_series,
                    "cwnd_series": cwnd_series,
                    "playback_state_series": calculate_playback_state_over_time(events, time_interval=0.5, max_time=60.0)
                }
                
                rows.append(row)
                row_csv = dict(row)
                row_csv.pop("bitrate_series", None)
                row_csv.pop("throughput_series", None)
                row_csv.pop("rtt_series", None)
                row_csv.pop("cwnd_series", None)
                row_csv.pop("playback_state_series", None)
                rows_for_csv.append(row_csv)
                run_rows.append(row)
    
    # Averaged user summaries: one combined plot (both algos) for bitrate/throughput/rtt/cwnd,
    # separate playback state plot per algorithm
    if rows:
        _plot_aggregated_user_summaries(rows, output_dir)
    
    if rows_for_csv:
        csv_path = os.path.join(output_dir, "per_user_stats.csv")
        with open(csv_path, "w", newline="") as csvfile:
            fieldnames = [
                "algo", "protocol", "run_id", "player_id", "node_id",
                "closest_enb_distance", "mean_throughput_kbps",
                "avg_rtt", "avg_cwnd", "avg_playback_bitrate_kbps",
                "playback_duration", "interruption_duration", "rebuffer_ratio",
                "stall_count", "bitrate_switch_count"
            ]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows_for_csv)
        print(f"Saved per-user stats CSV: {csv_path}")
    
    # Per-run percentile stats across users, then average across runs per algorithm
    metrics = [
        "avg_playback_bitrate_kbps",
        "playback_duration",
        "interruption_duration",
        "rebuffer_ratio",
        "bitrate_switch_count",
        "mean_throughput_kbps",
        "avg_rtt",
        "avg_cwnd"
    ]
    
    def _mode_value(values):
        if not values:
            return 0.0
        counts = Counter(values)
        max_count = max(counts.values())
        modes = [v for v, c in counts.items() if c == max_count]
        return min(modes)
    
    # Build per-run stats for each algorithm
    per_algo_run_stats = defaultdict(list)
    for row in rows_for_csv:
        per_algo_run_stats[(row["algo"], row["run_id"])].append(row)
    
    per_algo_aggregate = defaultdict(lambda: defaultdict(list))
    
    for (algo, run_id), run_rows in per_algo_run_stats.items():
        for metric in metrics:
            values = []
            for r in run_rows:
                try:
                    values.append(float(r[metric]))
                except (ValueError, TypeError):
                    continue
            if not values:
                continue
            mean_val = float(np.mean(values))
            median_val = float(np.median(values))
            q1 = float(np.percentile(values, 25))
            q3 = float(np.percentile(values, 75))
            iqr_val = q3 - q1
            mode_val = float(_mode_value(values))
            
            per_algo_aggregate[algo][metric].append({
                "mean": mean_val,
                "median": median_val,
                "iqr": iqr_val,
                "mode": mode_val
            })
    
    # Average stats across runs for each algorithm
    output_rows = []
    for algo, metric_stats in per_algo_aggregate.items():
        for metric, run_stats in metric_stats.items():
            if not run_stats:
                continue
            mean_avg = float(np.mean([s["mean"] for s in run_stats]))
            median_avg = float(np.mean([s["median"] for s in run_stats]))
            iqr_avg = float(np.mean([s["iqr"] for s in run_stats]))
            mode_avg = float(np.mean([s["mode"] for s in run_stats]))
            output_rows.append({
                "algorithm": algo,
                "metric": metric,
                "mean": mean_avg,
                "median": median_avg,
                "iqr": iqr_avg,
                "mode": mode_avg
            })
    
    if output_rows:
        output_path = os.path.join(output_dir, "per_algorithm_percentiles.csv")
        with open(output_path, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["algorithm", "metric", "mean", "median", "iqr", "mode"])
            writer.writeheader()
            writer.writerows(output_rows)
        print(f"Saved per-algorithm percentile stats CSV: {output_path}")

    # Box plots per metric: pool all users across runs, one box per algorithm
    # Box: p25-p75, median: p50, whiskers: p10-p90 (percentiles of pooled distribution)
    if rows_for_csv:
        # Pool users across all runs per (algo, metric)
        pooled = defaultdict(lambda: defaultdict(list))
        for r in rows_for_csv:
            algo = r["algo"]
            # Metrics
            for metric in metrics:
                try:
                    pooled[metric][algo].append(float(r[metric]))
                except (ValueError, TypeError):
                    continue
            # Fraction stalled: 1 if had interruption, 0 otherwise
            try:
                stalled = 1.0 if float(r["interruption_duration"]) > 0 else 0.0
                pooled["fraction_stalled_users"][algo].append(stalled)
            except (ValueError, TypeError):
                continue

        def _plot_pooled_boxplot(data_by_algo, title, ylabel, filename):
            if not data_by_algo:
                return
            labels = sorted(data_by_algo.keys())
            data = [data_by_algo[label] for label in labels]
            if not any(data):
                return
            fig, ax = plt.subplots(figsize=(12, 6))
            # whis=(10, 95): whiskers at p10 and p95; box is p25-p75, median at p50
            bp = ax.boxplot(
                data, labels=labels, patch_artist=True, showmeans=True,
                whis=(10, 95),
            )
            colors = []
            for label in labels:
                if label.startswith("QUIC"):
                    colors.append("lightcoral")
                elif label.startswith("TCP"):
                    colors.append("lightblue")
                else:
                    colors.append("lightgray")
            for patch, color in zip(bp["boxes"], colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.7)
            ax.set_title(title, fontsize=12, fontweight="bold")
            ax.set_xlabel("Algorithm")
            ax.set_ylabel(ylabel)
            ax.grid(True, axis="y", alpha=0.3)
            plt.xticks(rotation=30, ha="right")
            plt.tight_layout()
            fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches="tight")
            plt.close(fig)
            print(f"Saved {filename}")

        for metric in metrics:
            _plot_pooled_boxplot(
                pooled.get(metric, {}),
                f"Pooled user distribution (p10-p95 whiskers, p25-p75 box): {metric}",
                metric,
                f"boxplot_{metric}.png",
            )
        _plot_pooled_boxplot(
            pooled.get("fraction_stalled_users", {}),
            "Pooled user distribution: fraction stalled (1=had interruption)",
            "Fraction stalled",
            "boxplot_fraction_stalled_users.png",
        )

    # Median CDF across runs per algorithm (throughput + bitrate)
    if rows_for_csv:
        def _cdf_values(sorted_vals, x_grid):
            if not sorted_vals:
                return None
            n = len(sorted_vals)
            idx = 0
            cdf = []
            for x in x_grid:
                while idx < n and sorted_vals[idx] <= x:
                    idx += 1
                cdf.append(idx / n)
            return cdf
        
        # Group per run
        run_values = defaultdict(list)
        for r in rows_for_csv:
            run_key = (r["algo"], r["run_id"])
            try:
                run_values[run_key].append({
                    "mean_throughput_kbps": float(r["mean_throughput_kbps"]),
                    "avg_playback_bitrate_kbps": float(r["avg_playback_bitrate_kbps"])
                })
            except (ValueError, TypeError):
                continue
        
        # Organize by algorithm
        algo_runs = defaultdict(list)
        for (algo, run_id), vals in run_values.items():
            algo_runs[algo].append(vals)
        
        for algo, runs in algo_runs.items():
            # Throughput CDF aggregation
            max_thr = 0.0
            for vals in runs:
                max_thr = max(max_thr, max(v["mean_throughput_kbps"] for v in vals))
            if max_thr > 0:
                x_grid = np.linspace(0, max_thr, 200)
                cdfs = []
                for vals in runs:
                    sorted_vals = sorted(v["mean_throughput_kbps"] for v in vals)
                    cdf = _cdf_values(sorted_vals, x_grid)
                    if cdf is not None:
                        cdfs.append(cdf)
                if cdfs:
                    cdfs = np.array(cdfs)
                    median_cdf = np.median(cdfs, axis=0)
                    p25 = np.percentile(cdfs, 25, axis=0)
                    p75 = np.percentile(cdfs, 75, axis=0)
                    fig, ax = plt.subplots(figsize=(8, 5))
                    ax.plot(x_grid, median_cdf, linewidth=2, label="Median CDF")
                    ax.fill_between(x_grid, p25, p75, alpha=0.2, label="IQR")
                    ax.set_title(f"Median CDF of mean throughput (Kbps)\n{algo}", fontsize=11, fontweight="bold")
                    ax.set_xlabel("Mean throughput (Kbps)")
                    ax.set_ylabel("CDF")
                    ax.grid(True, alpha=0.3)
                    ax.legend()
                    filename = f"cdf_throughput_{algo.replace(' ', '_')}.png"
                    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches="tight")
                    plt.close(fig)
            
            # Bitrate CDF aggregation
            max_br = 0.0
            for vals in runs:
                max_br = max(max_br, max(v["avg_playback_bitrate_kbps"] for v in vals))
            if max_br > 0:
                x_grid = np.linspace(0, max_br, 200)
                cdfs = []
                for vals in runs:
                    sorted_vals = sorted(v["avg_playback_bitrate_kbps"] for v in vals)
                    cdf = _cdf_values(sorted_vals, x_grid)
                    if cdf is not None:
                        cdfs.append(cdf)
                if cdfs:
                    cdfs = np.array(cdfs)
                    median_cdf = np.median(cdfs, axis=0)
                    p25 = np.percentile(cdfs, 25, axis=0)
                    p75 = np.percentile(cdfs, 75, axis=0)
                    fig, ax = plt.subplots(figsize=(8, 5))
                    ax.plot(x_grid, median_cdf, linewidth=2, color="purple", label="Median CDF")
                    ax.fill_between(x_grid, p25, p75, alpha=0.2, color="purple", label="IQR")
                    ax.set_title(f"Median CDF of avg playback bitrate (Kbps)\n{algo}", fontsize=11, fontweight="bold")
                    ax.set_xlabel("Avg playback bitrate (Kbps)")
                    ax.set_ylabel("CDF")
                    ax.grid(True, alpha=0.3)
                    ax.legend()
                    filename = f"cdf_bitrate_{algo.replace(' ', '_')}.png"
                    fig.savefig(os.path.join(output_dir, filename), dpi=300, bbox_inches="tight")
                    plt.close(fig)

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
    
    base_dir = get_base_dir()
    
    print("=" * 60)
    print("Starting bitrate and playback analysis...")
    print("=" * 60)
    import sys
    sys.stdout.flush()
    
    run_groups = find_all_runs(base_dir)
    
    if not run_groups:
        print("No run directories found in Quic_artifacts or Tcp_artifacts.")
        return
        
    print(f"Found algorithms: {list(run_groups.keys())}")
    sys.stdout.flush()
    
    # Map logs
    print("\nMapping log files to runs...")
    log_mapping = map_logs_to_runs(base_dir)
    sys.stdout.flush()
    
    all_stats = {}
    
    print(f"\nProcessing {len(run_groups)} algorithm groups...")
    sys.stdout.flush()
    
    for name, runs in run_groups.items():
        all_stats[name] = process_runs(runs, name, log_mapping, client_nodes=client_nodes)
        sys.stdout.flush()
        
    # Create output directory
    output_dir = os.path.join(base_dir, "Analysis_artifacts")
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")
        
    sys.stdout.flush()
    
    # # 1. Individual Plots
    # for name, stats in all_stats.items():
    #     plot_single_algo(stats, name, output_dir)
        
    # # 2. Combined QUIC Plots
    # plot_combined_bitrate(all_stats, "QUIC", output_dir)
    # plot_combined_playback(all_stats, "QUIC", output_dir)
    
    # # 3. Combined TCP Plots
    # plot_combined_bitrate(all_stats, "TCP", output_dir)
    # plot_combined_playback(all_stats, "TCP", output_dir)

    # # 4. Grand Combined Plots
    # plot_grand_combined_bitrate(all_stats, output_dir)
    
    # # 5. All Combined Plot (aggregates raw data from all runs)
    # plot_all_combined_bitrate(run_groups, log_mapping, output_dir, client_nodes=client_nodes)
    
    # # 6. Playback Grid Plots
    # plot_playback_grid(all_stats, "QUIC", output_dir)
    # plot_playback_grid(all_stats, "TCP", output_dir)
    
    # # 7. Interruption Duration Box Plot
    # plot_interruption_duration_boxplot(all_stats, output_dir)
    
    # 8. Per-user statistics and plots
    generate_per_user_stats(run_groups, log_mapping, output_dir)
    
    print(f"\nAll plots saved to {output_dir}")
    print("=" * 60)
    print("Analysis complete!")
    print("=" * 60)
    sys.stdout.flush()

if __name__ == '__main__':
    main()
