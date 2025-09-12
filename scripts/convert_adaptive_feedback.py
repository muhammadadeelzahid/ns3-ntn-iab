#!/usr/bin/env python3
import argparse
import csv
import os
import re


PACKET_LOSS_RE = re.compile(r"Packet\s+Loss:\s*([0-9.eE+-]+)\s*%", re.IGNORECASE)
THROUGHPUT_RE = re.compile(r"Throughput:\s*([0-9.eE+-]+)\s*MB/s", re.IGNORECASE)
FPS_RE = re.compile(r"FPS:\s*(\d+)\s*->\s*(\d+)", re.IGNORECASE)
DATARATE_RE = re.compile(r"DataRate:\s*([0-9.eE+-]+)\s*->\s*([0-9.eE+-]+)\s*Mbps", re.IGNORECASE)


def convert(in_path: str, out_path: str) -> None:
    rows = []
    if not os.path.exists(in_path):
        raise FileNotFoundError(in_path)
    with open(in_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            m_loss = PACKET_LOSS_RE.search(s)
            m_thr = THROUGHPUT_RE.search(s)
            m_fps = FPS_RE.search(s)
            m_dr = DATARATE_RE.search(s)
            if not (m_loss and m_thr and m_fps and m_dr):
                continue
            loss = float(m_loss.group(1))
            thr_mb_s = float(m_thr.group(1))
            fps_cur, fps_new = int(m_fps.group(1)), int(m_fps.group(2))
            dr_cur, dr_new = float(m_dr.group(1)), float(m_dr.group(2))
            rows.append([loss, thr_mb_s, dr_cur, dr_new, fps_cur, fps_new])

    with open(out_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "Packet Loss (%)",
            "Throughput (MB/s)",
            "Data Rate Current (Mbps)",
            "Data Rate New (Mbps)",
            "Current FPS",
            "New FPS",
        ])
        w.writerows(rows)


def main() -> None:
    ap = argparse.ArgumentParser(description="Convert AdaptiveFeedback_*.txt to a CSV with selected columns.")
    ap.add_argument("--in", dest="in_path", default="AdaptiveFeedback_4.txt", help="Input log file")
    ap.add_argument("--out", dest="out_path", default="AdaptiveFeedback_4.csv", help="Output CSV path")
    args = ap.parse_args()
    convert(args.in_path, args.out_path)
    print(f"Wrote: {args.out_path}")


if __name__ == "__main__":
    main()


