#!/usr/bin/env python3
"""Post-process the IAB backhaul-handover campaign: TCP vs QUIC under LEO constellation handover.

Walks handover_campaign/results/<config>/run_<seed>/ and produces:
  - summary.csv            : per-config aggregated DASH QoE (across seeds x UEs)
  - cwnd_rtt_vs_time.png   : server cwnd & RTT over time (representative seed), HO instants marked
  - stalls_per_config.png  : stalls + stall duration per config (boxplots across seeds)
  - recovery_per_config.png: per-handover cwnd recovery time per config
Usage: python3 analyze.py [results_dir]
"""
import os, re, glob, sys
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS = sys.argv[1] if len(sys.argv) > 1 else "results"
HO_TIMES = [15.0, 30.0, 45.0]
CONFIGS = ["quic_bbr", "quic_newreno", "tcp_bbr", "tcp_cubic", "tcp_newreno"]
LABEL = {"quic_bbr": "QUIC-BBR", "quic_newreno": "QUIC-NewReno",
         "tcp_bbr": "TCP-BBR", "tcp_cubic": "TCP-CUBIC", "tcp_newreno": "TCP-NewReno"}
COLOR = {"quic_bbr": "tab:blue", "quic_newreno": "tab:cyan",
         "tcp_bbr": "tab:red", "tcp_cubic": "tab:orange", "tcp_newreno": "tab:brown"}
PROTO = {c: ("QUIC" if c.startswith("quic") else "TCP") for c in CONFIGS}

# Float token tolerant of nan/inf: a UE that plays zero frames prints avgRate/AvgDt as nan
# (division by m_framesPlayed=0). Those are the *fully-stalled* UEs we must not silently drop.
_F = r"(-?(?:[\d.]+(?:[eE][+-]?\d+)?|nan|inf))"
QOE_RE = re.compile(
    r"ue-id:\s*(\d+)\s+InterruptionTime:\s*" + _F + r"\s+interruptions:\s*(\d+)\s+"
    r"avgRate:\s*" + _F + r"\s+minRate:\s*" + _F + r"\s+AvgDt:\s*" + _F + r"\s+"
    r"changes:\s*(\d+)\s+TotalPlaybackTime:\s*" + _F)


def parse_simlog(path):
    """Return (qoe_rows, ho_end_times, n_failed)."""
    qoe, ho_ends, fails = [], [], 0
    if not os.path.exists(path):
        return qoe, ho_ends, fails
    with open(path, errors="ignore") as f:
        for line in f:
            m = QOE_RE.search(line)
            if m:
                qoe.append(dict(ue=int(m.group(1)), stall_time=float(m.group(2)),
                                stalls=int(m.group(3)), avg_bitrate=float(m.group(4)),
                                playback=float(m.group(8))))
            elif "HANDOVER END OK" in line:
                mm = re.search(r"t=([\d.]+)s", line)
                if mm:
                    ho_ends.append(float(mm.group(1)))
            elif "HANDOVER FAILED" in line:
                fails += 1
    return qoe, ho_ends, fails


def server_cwnd_series(rundir, proto):
    """Return {conn_id: (t, cwnd)} for the server-side connections (one per UE)."""
    out = {}
    for fp in glob.glob(os.path.join(rundir, "server%s-cwnd-change*-conn*.txt" % proto)):
        conn = re.search(r"-conn(\d+)\.txt$", fp).group(1)
        try:
            d = np.loadtxt(fp)
            if d.size == 0:
                continue
            d = d.reshape(-1, 3)
            out[conn] = (d[:, 0], d[:, 2])
        except Exception:
            pass
    return out


def server_rtt_series(rundir, proto):
    out = {}
    for fp in glob.glob(os.path.join(rundir, "server%s-rtt*-conn*.txt" % proto)):
        conn = re.search(r"-conn(\d+)\.txt$", fp).group(1)
        try:
            d = np.loadtxt(fp)
            if d.size == 0:
                continue
            d = d.reshape(-1, 3)
            out[conn] = (d[:, 0], d[:, 2])
        except Exception:
            pass
    return out


def recovery_times(t, cwnd, ho_times, window=5.0):
    """For each handover instant, time for cwnd to return to its pre-HO level (or NaN)."""
    rec = []
    t = np.asarray(t); cwnd = np.asarray(cwnd)
    for ho in ho_times:
        pre = cwnd[(t >= ho - 1.0) & (t < ho)]
        if pre.size == 0:
            rec.append(np.nan); continue
        target = np.median(pre)
        after = (t >= ho) & (t <= ho + window)
        ta, ca = t[after], cwnd[after]
        hit = np.where(ca >= target)[0]
        rec.append(ta[hit[0]] - ho if hit.size else np.nan)
    return rec


def main():
    summary = []
    per_config_stalls = {c: [] for c in CONFIGS}      # per-run total stalls (sum over UEs)
    per_config_stalltime = {c: [] for c in CONFIGS}   # per-run total stall duration
    per_config_recovery = {c: [] for c in CONFIGS}    # per-(run,conn,ho) recovery times
    rep_cwnd = {}; rep_rtt = {}                        # representative trace for the time-series fig
    total_fails = 0

    for cfg in CONFIGS:
        cdir = os.path.join(RESULTS, cfg)
        runs = sorted(glob.glob(os.path.join(cdir, "run_*")))
        for rundir in runs:
            qoe, ho_ends, fails = parse_simlog(os.path.join(rundir, "sim.log"))
            total_fails += fails
            if fails:
                continue  # exclude runs with a failed handover (IAB black-holed)
            if qoe:
                per_config_stalls[cfg].append(sum(q["stalls"] for q in qoe))
                per_config_stalltime[cfg].append(sum(q["stall_time"] for q in qoe))
                for q in qoe:
                    summary.append(dict(config=cfg, proto=PROTO[cfg], **q,
                                        seed=os.path.basename(rundir)))
            cw = server_cwnd_series(rundir, PROTO[cfg])
            for conn, (t, c) in cw.items():
                per_config_recovery[cfg] += [r for r in recovery_times(t, c, HO_TIMES) if not np.isnan(r)]
            # pick a representative (busiest connection of the first run) for the time-series fig
            if cfg not in rep_cwnd and cw:
                busiest = max(cw, key=lambda k: len(cw[k][0]))
                rep_cwnd[cfg] = cw[busiest]
                rt = server_rtt_series(rundir, PROTO[cfg])
                if busiest in rt:
                    rep_rtt[cfg] = rt[busiest]

    if total_fails:
        print("WARNING: %d handover failures across runs (those runs excluded)" % total_fails)

    # ---- summary.csv ----
    df = pd.DataFrame(summary)
    if not df.empty:
        agg = df.groupby(["proto", "config"]).agg(
            runs_ue=("ue", "count"),
            mean_stalls=("stalls", "mean"), median_stalls=("stalls", "median"),
            mean_stall_s=("stall_time", "mean"),
            mean_bitrate_Mbps=("avg_bitrate", lambda s: s.mean() / 1e6),
            mean_playback_s=("playback", "mean")).reset_index()
        agg.to_csv(os.path.join(RESULTS, "summary.csv"), index=False)
        print(agg.to_string(index=False))

    # ---- cwnd + RTT vs time ----
    fig, ax = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
    for cfg in CONFIGS:
        if cfg in rep_cwnd:
            t, c = rep_cwnd[cfg]
            ax[0].plot(t, np.asarray(c) / 1e3, label=LABEL[cfg], color=COLOR[cfg], lw=1)
        if cfg in rep_rtt:
            t, r = rep_rtt[cfg]
            ax[1].plot(t, np.asarray(r) * 1e3, label=LABEL[cfg], color=COLOR[cfg], lw=1)
    for a in ax:
        for ho in HO_TIMES:
            a.axvline(ho, color="k", ls="--", alpha=0.4, lw=0.8)
    ax[0].set_ylabel("cwnd (KB)"); ax[1].set_ylabel("RTT (ms)"); ax[1].set_xlabel("time (s)")
    ax[0].set_title("Server cwnd & RTT across constellation handovers (dashed = HO @ 15/30/45 s)")
    ax[0].legend(fontsize=8, ncol=2)
    fig.tight_layout(); fig.savefig(os.path.join(RESULTS, "cwnd_rtt_vs_time.png"), dpi=140)

    # ---- stalls per config ----
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    order = [c for c in CONFIGS if per_config_stalls[c]]
    ax[0].boxplot([per_config_stalls[c] for c in order], labels=[LABEL[c] for c in order])
    ax[0].set_ylabel("stalls per run (sum over UEs)"); ax[0].set_title("Playback stalls under handover")
    ax[1].boxplot([per_config_stalltime[c] for c in order], labels=[LABEL[c] for c in order])
    ax[1].set_ylabel("total stall duration per run (s)"); ax[1].set_title("Stall duration under handover")
    for a in ax:
        a.tick_params(axis="x", rotation=30)
    fig.tight_layout(); fig.savefig(os.path.join(RESULTS, "stalls_per_config.png"), dpi=140)

    # ---- recovery time per config ----
    fig, ax = plt.subplots(figsize=(8, 4))
    order = [c for c in CONFIGS if per_config_recovery[c]]
    if order:
        ax.boxplot([per_config_recovery[c] for c in order], labels=[LABEL[c] for c in order])
        ax.set_ylabel("cwnd recovery time per handover (s)")
        ax.set_title("Congestion-window recovery after each handover")
        ax.tick_params(axis="x", rotation=30)
        fig.tight_layout(); fig.savefig(os.path.join(RESULTS, "recovery_per_config.png"), dpi=140)
    print("\nFigures + summary.csv written to", RESULTS)


if __name__ == "__main__":
    main()
