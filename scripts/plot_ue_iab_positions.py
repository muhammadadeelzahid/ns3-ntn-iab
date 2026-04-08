#!/usr/bin/env python3
"""
Plot UE and IAB node positions for each run in Quic_artifacts and Tcp_artifacts.
Saves a plot inside each run folder showing user positions, IAB positions,
and each user's distance from the nearest IAB node.
"""

import argparse
import glob
import os
import re
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np


# Regex patterns for parsing .err files
UE_INSTALL_RE = re.compile(
    r"Install UE with IMSI\s+\d+\s+on Node ID\s+(\d+)\s+"
    r"at position\s+\(([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\)",
    re.IGNORECASE,
)
IAB_INSTALL_RE = re.compile(
    r"Install IAB device.*?Node ID\s+(\d+)\s+at position\s+"
    r"\(([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\)",
    re.IGNORECASE,
)
IAB_NODE_RE = re.compile(
    r"IAB Node ID:\s*(\d+),\s*Position=\(\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\s*\)",
    re.IGNORECASE,
)
DISTANCE_RE = re.compile(
    r"Closest eNB found at distance:\s*([\d.]+)",
    re.IGNORECASE,
)


def find_all_runs(base_dir):
    """Find all run directories in Quic_artifacts and Tcp_artifacts."""
    run_dirs = []
    for artifacts in ("Quic_artifacts", "Tcp_artifacts"):
        root = os.path.join(base_dir, artifacts)
        if not os.path.isdir(root):
            continue
        for algo in os.listdir(root):
            algo_path = os.path.join(root, algo)
            if not os.path.isdir(algo_path):
                continue
            for run_path in glob.glob(os.path.join(algo_path, "run_*")):
                if os.path.isdir(run_path):
                    run_dirs.append(os.path.abspath(run_path))
    return run_dirs


def map_logs_to_runs(base_dir):
    """
    Map run directories to their .err files using .out file first lines.
    Returns dict: { run_dir_abs_path: err_file_abs_path }
    """
    mapping = {}
    for log_path in glob.glob(os.path.join(base_dir, "*.out")):
        try:
            with open(log_path, "r") as f:
                first_line = f.readline()
            m = re.search(r"Running .* in (.*)", first_line)
            if not m:
                continue
            run_rel = m.group(1).strip()
            run_abs = os.path.abspath(os.path.join(base_dir, run_rel))
            err_path = log_path.replace(".out", ".err")
            if os.path.isfile(err_path):
                mapping[run_abs] = os.path.abspath(err_path)
        except Exception:
            pass
    return mapping


def parse_positions_and_distances(err_file):
    """
    Parse UE positions, IAB positions, and per-UE distances from .err file.
    Returns (ue_positions, iab_positions, ue_distances) where:
      ue_positions: dict {node_id: (x, y, z)}
      iab_positions: dict {node_id: (x, y, z)}
      ue_distances: dict {node_id: distance}  # distance to nearest IAB
    """
    ue_positions = {}
    iab_positions = {}
    distances_ordered = []  # in attach order (matches UE install order)

    if not err_file or not os.path.isfile(err_file):
        return ue_positions, iab_positions, {}

    with open(err_file, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = UE_INSTALL_RE.search(line)
            if m:
                node_id = int(m.group(1))
                x, y, z = float(m.group(2)), float(m.group(3)), float(m.group(4))
                ue_positions[node_id] = (x, y, z)
                continue

            m = IAB_INSTALL_RE.search(line)
            if m:
                node_id = int(m.group(1))
                x, y, z = float(m.group(2)), float(m.group(3)), float(m.group(4))
                iab_positions[node_id] = (x, y, z)
                continue

            m = IAB_NODE_RE.search(line)
            if m:
                node_id = int(m.group(1))
                x, y, z = float(m.group(2)), float(m.group(3)), float(m.group(4))
                iab_positions[node_id] = (x, y, z)
                continue

            m = DISTANCE_RE.search(line)
            if m:
                distances_ordered.append(float(m.group(1)))

    # Map distances to UE nodes by order (install order = attach order)
    ue_nodes_ordered = sorted(ue_positions.keys())
    ue_distances = {}
    for i, node_id in enumerate(ue_nodes_ordered):
        if i < len(distances_ordered):
            ue_distances[node_id] = distances_ordered[i]
        else:
            # Fallback: compute Euclidean distance to nearest IAB
            ue_pos = ue_positions[node_id]
            min_d = float("inf")
            for iab_pos in iab_positions.values():
                d = np.sqrt(
                    (ue_pos[0] - iab_pos[0]) ** 2
                    + (ue_pos[1] - iab_pos[1]) ** 2
                    + (ue_pos[2] - iab_pos[2]) ** 2
                )
                min_d = min(min_d, d)
            ue_distances[node_id] = min_d if min_d != float("inf") else 0.0

    return ue_positions, iab_positions, ue_distances


def dist_3d(p1, p2):
    """Euclidean distance between two (x,y,z) points."""
    return np.sqrt((p1[0] - p2[0]) ** 2 + (p1[1] - p2[1]) ** 2 + (p1[2] - p2[2]) ** 2)


def plot_positions(run_dir, ue_positions, iab_positions, ue_distances, output_name="ue_iab_positions.png"):
    """Create and save a 2D plot of UE and IAB positions with distance labels."""
    if not ue_positions:
        return False

    fig, ax = plt.subplots(figsize=(10, 8))

    # Plot IAB nodes first (so they appear behind UEs)
    if iab_positions:
        iab_x = [p[0] for p in iab_positions.values()]
        iab_y = [p[1] for p in iab_positions.values()]
        ax.scatter(iab_x, iab_y, marker="s", s=120, c="red", label="IAB", zorder=2)
        for node_id, (x, y, _) in iab_positions.items():
            ax.annotate(f"IAB {node_id}", (x, y), fontsize=9, color="red", ha="left", va="bottom")

    # Plot UEs
    ue_x = [p[0] for p in ue_positions.values()]
    ue_y = [p[1] for p in ue_positions.values()]
    ax.scatter(ue_x, ue_y, marker="o", s=80, c="steelblue", label="UE", zorder=3)

    # Annotate each UE with node ID and distance from nearest IAB
    for node_id, (x, y, _) in ue_positions.items():
        d = ue_distances.get(node_id)
        if d is not None:
            label = f"UE {node_id} ({d:.1f}m)"
        else:
            label = f"UE {node_id}"
        ax.annotate(label, (x, y), fontsize=8, ha="right", va="top", wrap=True)

    ax.set_xlabel("X position (m)")
    ax.set_ylabel("Y position (m)")
    run_name = os.path.basename(os.path.dirname(run_dir)) + "/" + os.path.basename(run_dir)
    ax.set_title(f"UE and IAB positions — {run_name}")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)
    ax.set_aspect("equal")
    plt.tight_layout()
    out_path = os.path.join(run_dir, output_name)
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Plot UE and IAB positions with distances for each run in *_artifacts."
    )
    parser.add_argument(
        "--base-dir",
        default=".",
        help="Project root (default: current directory).",
    )
    parser.add_argument(
        "--output-name",
        default="ue_iab_positions.png",
        help="Output filename inside each run folder (default: ue_iab_positions.png).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only print what would be done, do not create plots.",
    )
    args = parser.parse_args()
    base_dir = os.path.abspath(args.base_dir)

    run_dirs = find_all_runs(base_dir)
    log_mapping = map_logs_to_runs(base_dir)
    # Convert to err mapping
    err_mapping = {}
    for run_dir, log_path in log_mapping.items():
        err_path = log_path.replace(".out", ".err")
        if os.path.isfile(err_path):
            err_mapping[run_dir] = err_path

    print(f"Found {len(run_dirs)} run directories, {len(err_mapping)} with .err files.")

    plotted = 0
    skipped = 0
    for run_dir in sorted(run_dirs):
        err_file = err_mapping.get(run_dir)
        if not err_file:
            if not args.dry_run:
                print(f"Skipping {run_dir}: no .err file")
            skipped += 1
            continue

        ue_pos, iab_pos, ue_dist = parse_positions_and_distances(err_file)
        if not ue_pos:
            if not args.dry_run:
                print(f"Skipping {run_dir}: no UE positions in {err_file}")
            skipped += 1
            continue

        if args.dry_run:
            print(f"Would plot: {run_dir} ({len(ue_pos)} UEs, {len(iab_pos)} IABs)")
            plotted += 1
            continue

        if plot_positions(run_dir, ue_pos, iab_pos, ue_dist, args.output_name):
            print(f"Saved: {os.path.join(run_dir, args.output_name)}")
            plotted += 1
        else:
            skipped += 1

    print(f"Done. Plotted: {plotted}, Skipped: {skipped}")


if __name__ == "__main__":
    main()
