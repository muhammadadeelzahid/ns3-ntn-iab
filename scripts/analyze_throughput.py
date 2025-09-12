#!/usr/bin/env python3
import os
import re
import csv
from collections import defaultdict
from typing import Dict, List, Tuple

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


LOG_PATTERN = re.compile(r"Time:\s*([0-9.eE+-]+).*?UE:\s*(\d+).*?Throughput:\s*([0-9.eE+-]+)\s*MB/s")


def parse_file(path: str) -> Dict[int, List[float]]:
    data: Dict[int, List[float]] = defaultdict(list)
    if not os.path.exists(path):
        return data
    with open(path, 'r') as f:
        for line in f:
            m = LOG_PATTERN.search(line)
            if not m:
                continue
            # time = float(m.group(1))  # not used for averaging
            ue = int(m.group(2))
            thr_mb_s = float(m.group(3))
            # Convert MB/s to Kb/s (KB=1000-based): 1 MB/s ≈ 8000 Kb/s
            thr_kb_s = thr_mb_s * 8000.0
            data[ue].append(thr_kb_s)
    return data


def collect_all(prefix: str = 'GenericThroughput_', suffix: str = '.txt', types: List[int] = list(range(0, 7))
               ) -> Dict[int, Dict[int, List[float]]]:
    # traffic_type -> ue_index -> samples
    all_data: Dict[int, Dict[int, List[float]]] = {}
    for t in types:
        path = f"{prefix}{t}{suffix}"
        per_ue = parse_file(path)
        all_data[t] = per_ue
    return all_data


def collect_all_ul(prefix: str = 'GenericThroughput_UL_', suffix: str = '.txt', types: List[int] = list(range(0, 7))
                  ) -> Dict[int, Dict[int, List[float]]]:
    # traffic_type -> ue_index -> samples (UL)
    all_data: Dict[int, Dict[int, List[float]]] = {}
    for t in types:
        path = f"{prefix}{t}{suffix}"
        per_ue = parse_file(path)
        all_data[t] = per_ue
    return all_data


def compute_averages(all_data: Dict[int, Dict[int, List[float]]]) -> Dict[int, Dict[int, float]]:
    # ue_index -> traffic_type -> avg_throughput
    per_ue_avgs: Dict[int, Dict[int, float]] = defaultdict(dict)
    ue_ids = set()
    for t, per_ue in all_data.items():
        for ue, samples in per_ue.items():
            ue_ids.add(ue)
            if samples:
                per_ue_avgs[ue][t] = sum(samples) / len(samples)
            else:
                per_ue_avgs[ue][t] = 0.0
    return per_ue_avgs


def write_csv(per_ue_avgs_dl: Dict[int, Dict[int, float]],
              per_ue_avgs_ul: Dict[int, Dict[int, float]],
              out_path: str = 'Throughput_Averages.csv') -> None:
    types = list(range(0, 7))
    ue_ids = sorted(set(per_ue_avgs_dl.keys()) | set(per_ue_avgs_ul.keys()))
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f)
        header = ['UE']
        for t in types:
            header += [f"type_{t}_DL", f"type_{t}_UL"]
        w.writerow(header)
        for ue in ue_ids:
            row = [ue]
            for t in types:
                row.append(per_ue_avgs_dl.get(ue, {}).get(t, 0.0))
                row.append(per_ue_avgs_ul.get(ue, {}).get(t, 0.0))
            w.writerow(row)


def plot_per_ue(per_ue_avgs: Dict[int, Dict[int, float]], out_dir: str = 'plots') -> None:
    os.makedirs(out_dir, exist_ok=True)
    types = list(range(0, 7))
    type_labels = [str(t) for t in types]
    for ue in sorted(per_ue_avgs.keys()):
        values = [per_ue_avgs[ue].get(t, 0.0) for t in types]
        plt.figure(figsize=(8, 4))
        bars = plt.bar(type_labels, values, color='#4C78A8')
        plt.title(f"UE {ue} Average Throughput by Traffic Type")
        plt.xlabel("Traffic Type")
        plt.ylabel("Throughput (Kb/s)")
        plt.grid(axis='y', linestyle='--', alpha=0.4)
        for rect, v in zip(bars, values):
            plt.text(rect.get_x() + rect.get_width()/2.0, rect.get_height(), f"{v:.2f}",
                     ha='center', va='bottom', fontsize=8)
        out_path = os.path.join(out_dir, f"ue_{ue}_throughput.png")
        plt.tight_layout()
        plt.savefig(out_path)
        plt.close()


def main():
    all_data_dl = collect_all()
    all_data_ul = collect_all_ul()
    per_ue_avgs_dl = compute_averages(all_data_dl)
    per_ue_avgs_ul = compute_averages(all_data_ul)
    write_csv(per_ue_avgs_dl, per_ue_avgs_ul, 'Throughput_Averages.csv')
    # Plot only DL per-UE to keep figure readable (units converted to Kb/s)
    plot_per_ue(per_ue_avgs_dl, 'plots')
    print("Done. Wrote Throughput_Averages.csv (DL/UL in Kb/s) and plots/*.png (DL only)")


if __name__ == '__main__':
    main()




