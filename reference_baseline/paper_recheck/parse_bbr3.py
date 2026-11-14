#!/usr/bin/env python3
"""Compute the 5-row post-fix Table II including the new TCP-BBRv3 row, using the identical
Yin-2015 composite QoE as parse_fair.py: per UE sum ln(Rn/45) - 4.541*rebuffer - sum|dln R|
(Rn kbps); rebuffer = max inline interTime per UE; mean over 10 UEs, then over seeds.
Runs excluded if last timestamp <= 584 (partial)."""
import glob, math, os, re, statistics as st

BASE = os.path.dirname(__file__)
LINE = re.compile(r"ue-id:\s*(\d+)\s+newBitRate:\s*([0-9.eE+-]+).*?interTime:\s*([0-9.eE+-]+)")
TS = re.compile(r"^([0-9][0-9.]*)\s")

CONFIGS = [
    ("TCP NewReno",  os.path.join(BASE, "fair", "tcp_newreno", "run_*")),
    ("TCP BBR (v1)", os.path.join(BASE, "fair", "tcp_bbr", "run_*")),
    ("TCP BBRv3",    os.path.join(BASE, "fair", "tcp_bbr3", "run_*")),
    ("QUIC NewReno", os.path.join(BASE, "fair_fixed", "quic_newreno", "run_*")),
    ("QUIC BBR",     os.path.join(BASE, "fair_fixed", "quic_bbr", "run_*")),
]

def parse_run(path):
    seqs, reb = {}, {}
    last_t = 0.0
    with open(path, errors="ignore") as f:
        for ln in f:
            mt = TS.match(ln)
            if mt:
                last_t = float(mt.group(1))
            m = LINE.search(ln)
            if not m:
                continue
            ue = int(m.group(1)); br = float(m.group(2)) / 1000.0; it = float(m.group(3))
            seqs.setdefault(ue, []).append(br)
            reb[ue] = max(reb.get(ue, 0.0), it)
    if last_t <= 584:
        return None
    out = []
    for ue, seq in seqs.items():
        if not seq:
            continue
        quality = sum(math.log(r / 45.0) for r in seq)
        smooth = sum(abs(math.log(seq[i]) - math.log(seq[i - 1])) for i in range(1, len(seq)))
        out.append((quality - 4.541 * reb.get(ue, 0.0) - smooth, st.mean(seq)))
    return out

print(f"{'Config':<14} {'nseed':>5} {'QoE_mean':>9} {'QoE_med':>9} {'Mbps':>6}  {'stall_s':>8}")
rows = []
for label, patt in CONFIGS:
    run_qoe, run_br, run_stall = [], [], []
    for rd in sorted(glob.glob(patt)):
        sl = os.path.join(rd, "sim.log")
        if not os.path.exists(sl):
            continue
        ues = parse_run(sl)
        if not ues:
            continue
        run_qoe.append(st.mean(q for q, _ in ues))
        run_br.append(st.mean(b for _, b in ues))
    if not run_qoe:
        print(f"{label:<14} {0:>5}  (no data)")
        continue
    mq, mdq, mbps = st.mean(run_qoe), st.median(run_qoe), st.mean(run_br) / 1000.0
    print(f"{label:<14} {len(run_qoe):>5} {mq:>9.1f} {mdq:>9.1f} {mbps:>6.2f}")
    rows.append((label, len(run_qoe), mq, mdq, mbps))

if rows:
    order = sorted(rows, key=lambda r: -r[2])
    print("\nRanking by mean QoE: " + " > ".join(r[0] for r in order))
