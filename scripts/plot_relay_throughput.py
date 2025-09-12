#!/usr/bin/env python3
import argparse
import glob
import os
import re
from typing import Dict, List, Optional, Tuple

import matplotlib

# Use a non-interactive backend suitable for WSL/headless environments
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import math


RELAY_FILENAME_RE = re.compile(r"x_(\d+)\.txt$")
SUM_RATE_LINE_RE = re.compile(r"^Sum Rate \(Total Throughput\):\s*([0-9]*\.?[0-9]+)\s*Mbps", re.IGNORECASE)
PER_USER_HEADER_RE = re.compile(r"^Per-User Throughput:\s*$", re.IGNORECASE)
PER_USER_LINE_RE = re.compile(r"^Node\s+(\d+)\s*\([^)]*\):\s*([0-9]*\.?[0-9]+)\s*Mbps", re.IGNORECASE)


def parse_relay_number_from_filename(filename: str) -> Optional[int]:
    match = RELAY_FILENAME_RE.search(os.path.basename(filename))
    return int(match.group(1)) if match else None


def extract_sum_throughput_mbps(file_content: str) -> Optional[float]:
    # Preferred: explicit "Sum Rate (Total Throughput): <val> Mbps"
    for line in file_content.splitlines():
        m = SUM_RATE_LINE_RE.search(line.strip())
        if m:
            try:
                return float(m.group(1))
            except ValueError:
                pass

    # Fallback: try to glean from a table if present (UDP Server Results -> Total row Average Throughput)
    # This is intentionally lenient but avoids heavy parsing.
    try:
        in_udp_section = False
        headers: List[str] = []
        avg_idx = None
        type_idx = None
        for raw in file_content.splitlines():
            line = raw.strip()
            if not in_udp_section and line.lower().startswith("udp server results"):
                in_udp_section = True
                continue
            if in_udp_section and line and all(h in line.lower() for h in ["type", "throughput"]):
                # Header line
                headers = [h.strip().lower() for h in re.split(r"\s{2,}|\t|\s\|\s", line)]
                # Try to find indices for type and average throughput
                type_idx = next((i for i, h in enumerate(headers) if h.startswith("type")), None)
                avg_idx = next(
                    (i for i, h in enumerate(headers) if "average throughput" in h),
                    None,
                )
                continue
            if in_udp_section and headers and line:
                cols = [c.strip() for c in re.split(r"\s{2,}|\t|\s\|\s", line)]
                if type_idx is not None and type_idx < len(cols) and cols[type_idx].lower().startswith("total"):
                    if avg_idx is not None and avg_idx < len(cols):
                        try:
                            return float(cols[avg_idx])
                        except ValueError:
                            pass
            # End UDP section if we hit an empty line after reading some rows
            if in_udp_section and not line:
                break
    except Exception:
        pass

    return None


def extract_per_user_throughputs(file_content: str) -> List[Tuple[int, float]]:
    results: List[Tuple[int, float]] = []
    lines = file_content.splitlines()
    i = 0
    while i < len(lines):
        if PER_USER_HEADER_RE.search(lines[i].strip()):
            i += 1
            # Read until blank line or EOF
            while i < len(lines) and lines[i].strip():
                m = PER_USER_LINE_RE.search(lines[i].strip())
                if m:
                    try:
                        node_id = int(m.group(1))
                        mbps = float(m.group(2))
                        results.append((node_id, mbps))
                    except ValueError:
                        pass
                i += 1
            break
        i += 1
    return results


def discover_files(directory: str, patterns: List[str]) -> List[str]:
    discovered: List[str] = []
    for pattern in patterns:
        discovered.extend(glob.glob(os.path.join(directory, pattern)))
    # Deduplicate and sort by relay number, then filename
    unique = {
        os.path.abspath(p): parse_relay_number_from_filename(p)
        for p in discovered
        if parse_relay_number_from_filename(p) is not None
    }
    return [p for p, _ in sorted(unique.items(), key=lambda kv: (kv[1], kv[0]))]


def build_sum_throughput_series(files: List[str]) -> List[Tuple[int, float]]:
    series: List[Tuple[int, float]] = []
    for path in files:
        relay_num = parse_relay_number_from_filename(path)
        if relay_num is None:
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
        except FileNotFoundError:
            continue
        sum_mbps = extract_sum_throughput_mbps(content)
        if sum_mbps is not None:
            series.append((relay_num, sum_mbps))
    # Sort by relay number just in case
    series.sort(key=lambda t: t[0])
    return series


def choose_per_user_source(files: List[str], explicit: Optional[str] = None) -> Optional[str]:
    if explicit:
        return explicit
    # Default to the file with the highest relay number
    best: Tuple[int, str] = (-1, "")
    for path in files:
        rn = parse_relay_number_from_filename(path)
        if rn is not None and rn > best[0]:
            best = (rn, path)
    return best[1] if best[0] >= 0 else None


def plot_sum_vs_relay(series: List[Tuple[int, float]], out_path: str) -> None:
    if not series:
        print("No sum throughput data found to plot.")
        return
    xs = [rn for rn, _ in series]
    ys = [mbps for _, mbps in series]
    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, ys, marker="o", linewidth=2)
    plt.title("Sum Throughput vs Number of IAB Nodes")
    plt.xlabel("Number of IAB nodes (relay)")
    plt.ylabel("Sum Throughput (Mbps)")
    plt.grid(True, linestyle=":", alpha=0.6)
    # Force discrete integer ticks at observed relay counts (e.g., 2, 3, ...)
    unique_xs = sorted(set(xs))
    plt.xticks(unique_xs)
    ax = plt.gca()
    ax.xaxis.set_major_locator(MaxNLocator(integer=True))
    if unique_xs:
        pad = 0.4 if len(unique_xs) > 1 else 0.6
        plt.xlim(min(unique_xs) - pad, max(unique_xs) + pad)
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"Saved: {out_path}")


def plot_per_user_bar(
    per_user: List[Tuple[int, float]],
    out_path: str,
    top_n: int = 20,
    node_ids: Optional[List[int]] = None,
) -> None:
    if not per_user:
        # Still allow plotting if node_ids provided (will show zeros)
        if not node_ids:
            print("No per-user throughput data found to plot.")
            return
    # If a global node_ids list is provided, align values to it and fill missing as 0
    if node_ids is not None:
        values_map = {nid: v for nid, v in per_user}
        aligned_ids = list(node_ids)
        labels = [f"Node {nid}" for nid in aligned_ids]
        values = [float(values_map.get(nid, 0.0)) for nid in aligned_ids]
    else:
        # Keep the order as it appears; if more than top_n, truncate
        data = per_user[:top_n]
        labels = [f"Node {nid}" for nid, _ in data]
        values = [mbps for _, mbps in data]
    plt.figure(figsize=(10, 5))
    bars = plt.bar(labels, values)
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("Throughput (Mbps)")
    if node_ids is not None:
        plt.title("Per-User Throughput (aligned users)")
    else:
        plt.title(f"Per-User Throughput (Top {min(top_n, len(per_user))} users)")
    plt.grid(True, axis="y", linestyle=":", alpha=0.6)
    # Annotate bars with values
    for rect, val in zip(bars, values):
        plt.text(rect.get_x() + rect.get_width() / 2.0, rect.get_height(), f"{val:.2f}",
                 ha="center", va="bottom", fontsize=8, rotation=0)
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"Saved: {out_path}")


def plot_combined_figure(
    series: List[Tuple[int, float]],
    per_user_by_relay: Dict[int, List[Tuple[int, float]]],
    out_path: str,
    top_n: int = 20,
    node_ids: Optional[List[int]] = None,
) -> None:
    # Total subplots: 1 for sum vs relay, plus one per relay with per-user data
    relay_keys = sorted(per_user_by_relay.keys())
    total_plots = 1 + len(relay_keys)
    if total_plots == 1 and not series:
        print("No data available to create combined figure.")
        return

    cols = 2
    rows = math.ceil(total_plots / cols)
    fig, axes = plt.subplots(rows, cols, figsize=(8 * cols, 4.5 * rows))
    if isinstance(axes, plt.Axes):
        axes = [[axes]]
    axes_flat: List[plt.Axes] = [ax for row in axes for ax in row]

    # First subplot: sum vs relay
    ax0 = axes_flat[0]
    if series:
        xs = [rn for rn, _ in series]
        ys = [mbps for _, mbps in series]
        ax0.plot(xs, ys, marker="o", linewidth=2)
        ax0.set_title("Sum Throughput vs Number of IAB Nodes")
        ax0.set_xlabel("Number of IAB nodes (relay)")
        ax0.set_ylabel("Sum Throughput (Mbps)")
        ax0.grid(True, linestyle=":", alpha=0.6)
        unique_xs = sorted(set(xs))
        ax0.set_xticks(unique_xs)
        ax0.xaxis.set_major_locator(MaxNLocator(integer=True))
        if unique_xs:
            pad = 0.4 if len(unique_xs) > 1 else 0.6
            ax0.set_xlim(min(unique_xs) - pad, max(unique_xs) + pad)
    else:
        ax0.text(0.5, 0.5, "No sum-throughput data", ha="center", va="center")
        ax0.set_axis_off()

    # Remaining subplots: per-user bars per relay
    for i, rn in enumerate(relay_keys, start=1):
        ax = axes_flat[i]
        data = per_user_by_relay.get(rn, [])
        # Align to global node_ids if provided
        if node_ids is not None and len(node_ids) > 0:
            values_map = {nid: v for nid, v in data}
            labels = [f"Node {nid}" for nid in node_ids]
            values = [float(values_map.get(nid, 0.0)) for nid in node_ids]
        else:
            if not data:
                ax.text(0.5, 0.5, f"No per-user data (relay={rn})", ha="center", va="center")
                ax.set_axis_off()
                continue
            data = data[:top_n]
            labels = [f"Node {nid}" for nid, _ in data]
            values = [mbps for _, mbps in data]
        bars = ax.bar(labels, values)
        ax.set_xticklabels(labels, rotation=45, ha="right")
        ax.set_ylabel("Throughput (Mbps)")
        ax.set_title(f"Per-User Throughput (relay={rn})")
        ax.grid(True, axis="y", linestyle=":", alpha=0.6)
        for rect, val in zip(bars, values):
            ax.text(
                rect.get_x() + rect.get_width() / 2.0,
                rect.get_height(),
                f"{val:.2f}",
                ha="center",
                va="bottom",
                fontsize=8,
            )

    # Hide any unused axes
    for j in range(1 + len(relay_keys), len(axes_flat)):
        axes_flat[j].set_axis_off()

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"Saved: {out_path}")


def extract_ul_per_user_avg_mbps_from_generic(file_path: str) -> List[Tuple[int, float]]:
    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except FileNotFoundError:
        return []
    per_ue_sum: Dict[int, float] = {}
    per_ue_cnt: Dict[int, int] = {}
    for line in content.splitlines():
        m = UL_LINE_RE.search(line)
        if not m:
            continue
        try:
            ue = int(m.group(2))
            mb_per_s = float(m.group(3))  # MB/s
            mbps = mb_per_s * 8.0  # convert to Mb/s
        except ValueError:
            continue
        per_ue_sum[ue] = per_ue_sum.get(ue, 0.0) + mbps
        per_ue_cnt[ue] = per_ue_cnt.get(ue, 0) + 1
    results: List[Tuple[int, float]] = []
    for ue, s in per_ue_sum.items():
        c = per_ue_cnt.get(ue, 0)
        if c > 0:
            avg = s / c
            if avg > 0.0:
                results.append((ue, avg))
    # Sort by UE index
    results.sort(key=lambda t: t[0])
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot sum throughput vs relay and per-user throughput from relay_x_*.txt logs. Also parse UL per-UE averages from GenericThroughput_UL_*.txt and add to per-user bars if non-zero.")
    parser.add_argument(
        "--dir",
        default=os.getcwd(),
        help="Directory containing relay text files (default: CWD)",
    )
    parser.add_argument(
        "--patterns",
        nargs="+",
        default=["relay_x_*.txt", "relaxy_x_*.txt"],
        help="Glob patterns of files to include",
    )
    parser.add_argument(
        "--per-user-file",
        default=None,
        help="Specific file to use for per-user bar chart (default: highest relay)",
    )
    parser.add_argument(
        "--top-n",
        type=int,
        default=20,
        help="Number of users to include in per-user bar chart (default: 20)",
    )
    parser.add_argument(
        "--out-prefix",
        default="plots",
        help="Output filename prefix (default: 'plots')",
    )
    parser.add_argument(
        "--ul-dir",
        default=None,
        help="Directory containing GenericThroughput_UL_*.txt (default: same as --dir)",
    )

    args = parser.parse_args()

    directory = os.path.abspath(args.dir)
    files = discover_files(directory, args.patterns)

    if not files:
        print(f"No files found in {directory} matching: {args.patterns}")
        return

    print("Discovered files (sorted):")
    for f in files:
        rn = parse_relay_number_from_filename(f)
        print(f"  relay={rn}: {f}")

    # Build series for plot 1
    series = build_sum_throughput_series(files)
    if series:
        out1 = os.path.join(directory, f"{args.out_prefix}_sum_vs_relay.png")
        plot_sum_vs_relay(series, out1)
    else:
        print("Warning: Could not extract any sum throughput values.")

    # Per-user bar charts for each relay file (or a specific one if provided)
    per_user_sources: List[str]
    if args.per_user_file:
        per_user_sources = [args.per_user_file]
    else:
        per_user_sources = files

    any_chart = False
    per_user_by_relay: Dict[int, List[Tuple[int, float]]] = {}
    for src in per_user_sources:
        try:
            with open(src, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
        except FileNotFoundError:
            continue
        per_user = extract_per_user_throughputs(content)
        rn = parse_relay_number_from_filename(src)
        if rn is not None:
            per_user_by_relay[rn] = per_user
    if not per_user_by_relay:
        print("Warning: No suitable per-user throughput data found in provided files.")
    # Build a global list of node IDs across all relays; keep sorted order
    all_node_ids: List[int] = []
    if per_user_by_relay:
        union = set()
        for data in per_user_by_relay.values():
            union.update(nid for nid, _ in data)
        all_node_ids = sorted(union)
    # Load UL per-UE averages from GenericThroughput_UL_<type>.txt files (traffic types 0..6)
    ul_dir = os.path.abspath(args.ul_dir) if args.ul_dir else directory
    ul_files = sorted(glob.glob(os.path.join(ul_dir, "GenericThroughput_UL_*.txt")))
    ul_data_by_type: Dict[str, List[Tuple[int, float]]] = {}
    for p in ul_files:
        # filename: GenericThroughput_UL_<type>.txt ; capture <type>
        base = os.path.basename(p)
        m = re.match(r"GenericThroughput_UL_(\w+)\.txt$", base)
        if not m:
            continue
        t = m.group(1)
        ul_data_by_type[t] = extract_ul_per_user_avg_mbps_from_generic(p)

    # Generate per-relay bar charts aligned to all_node_ids (fill missing with 0) and overlay UL bars if non-zero
    for rn, data in per_user_by_relay.items():
        out2_name = f"{args.out_prefix}_per_user_relay_{rn if rn is not None else 'na'}.png"
        out2 = os.path.join(directory, out2_name)
        # Build UL overlays per traffic type aligned to node_ids list
        ul_overlay_by_type: Dict[str, List[float]] = {}
        keys = sorted(ul_data_by_type.keys())
        if all_node_ids:
            for t in keys:
                ul_map = {ue: mbps for ue, mbps in ul_data_by_type.get(t, []) if mbps > 0.0}
                ul_overlay_by_type[t] = [float(ul_map.get(nid, 0.0)) for nid in all_node_ids]
        else:
            present_ids = [nid for nid, _ in data]
            for t in keys:
                ul_map = {ue: mbps for ue, mbps in ul_data_by_type.get(t, []) if mbps > 0.0}
                ul_overlay_by_type[t] = [float(ul_map.get(nid, 0.0)) for nid in present_ids]

        # Plot DL bars and overlay UL points (per-type, only non-zero values appear)
        plot_per_user_bar(
            data,
            out2,
            node_ids=all_node_ids if all_node_ids else None,
            top_n=args.top_n,
            ul_overlay_by_type=ul_overlay_by_type if ul_overlay_by_type else None,
        )
        any_chart = True
        any_chart = True

    # Combined figure containing all plots
    combined_out = os.path.join(directory, f"{args.out_prefix}_combined.png")
    plot_combined_figure(
        series,
        per_user_by_relay,
        combined_out,
        top_n=args.top_n,
        node_ids=all_node_ids if all_node_ids else None,
    )


if __name__ == "__main__":
    main()


