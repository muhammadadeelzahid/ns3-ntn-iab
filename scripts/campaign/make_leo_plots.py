#!/usr/bin/env python3
"""Standalone LEO/handover figures for the CommMag revision.
Produces TWO new files (leaves figA/figB untouched):
  fair/Analysis_artifacts/figC_handover_ensemble.png  -- goodput & RTT ensemble-aligned at handovers
  fair/Analysis_artifacts/figD_rtt_ccdf.png           -- RTT complementary CDF (tail) per config
Uses the same fair 28-seed set as Fig A.
"""
import glob, os, sys, numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

RESULTS = sys.argv[1] if len(sys.argv) > 1 else 'results'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'fair/Analysis_artifacts'
os.makedirs(OUT, exist_ok=True)
HO = [131, 393]                      # handover instants (s)
W = 30                               # +/- window (s) around a handover
FAIR = [2,3,4,5,7,10,12,13,17,19,20,21,23,25,26,28,31,32,33,34,36,37,41,43,44,46,47,49]

CONFIGS = [                          # (dir, label, color)
    ('quic_bbr',    'QUIC-BBR',     '#1f77b4'),
    ('quic_newreno','QUIC-NewReno', '#ff7f0e'),
    ('tcp_bbr',     'TCP-BBR',      '#2ca02c'),
    ('tcp_newreno', 'TCP-NewReno',  '#9467bd'),
]

def read2(f):
    a = np.array(open(f).read().split(), dtype=float); n=(a.size//2)*2
    return a[:n].reshape(-1,2)

def read3(f):
    a = np.array(open(f).read().split(), dtype=float); n=(a.size//3)*3
    return a[:n].reshape(-1,3)

def dash_cols(f):
    """DashClientRx: skip '#' header; return (time, pktsize)."""
    rows=[]
    for ln in open(f):
        if ln.startswith('#') or not ln.strip(): continue
        p=ln.split()
        rows.append((float(p[0]), float(p[1])))
    return np.array(rows) if rows else np.empty((0,2))

# ---------- gather per-seed 1s timelines ----------
def goodput_timeline(run, T=605):
    b=np.zeros(T+1)
    for f in glob.glob(f'{run}/DashClientRx*Node_*.txt'):
        d=dash_cols(f)
        if d.size==0: continue
        idx=d[:,0].astype(int); m=(idx>=0)&(idx<=T)
        np.add.at(b, idx[m], d[m,1])
    return b*8/1e6                       # Mbps per 1s bin

def rtt_timeline_and_samples(run, T=605):
    """Return (per-second mean RTT ms timeline, pooled RTT ms samples)."""
    binsum=np.zeros(T+1); bincnt=np.zeros(T+1); samples=[]
    for f in glob.glob(f'{run}/client*-rtt*.txt'):
        d=read3(f)
        if d.size==0: continue
        rtt=d[:,2]*1000.0                # ms
        samples.append(rtt)
        idx=d[:,0].astype(int); m=(idx>=0)&(idx<=T); idx=idx[m]; r=rtt[m]
        np.add.at(binsum, idx, r); np.add.at(bincnt, idx, 1)
    tl=np.divide(binsum, bincnt, out=np.full(T+1,np.nan), where=bincnt>0)
    samp=np.concatenate(samples) if samples else np.empty(0)
    return tl, samp

# ---------- build full-run goodput timelines (mean over fair seeds) ----------
T = 600
t = np.arange(T+1)
def smooth(y, w=5):
    if y is None: return None
    k = np.ones(w)/w
    return np.convolve(y, k, mode='same')
mean_gp={}
for cfg,label,color in CONFIGS:
    gps=[]
    for s in FAIR:
        run=f'{RESULTS}/{cfg}/run_{s}'
        if not os.path.isdir(run): continue
        gps.append(goodput_timeline(run)[:T+1])
    mean_gp[cfg]=np.nanmean(np.vstack(gps),0) if gps else None
    print(f"{label}: {len(gps)} seeds")

# ================= Fig C: DASH goodput vs time, handover markers =================
fig, ax = plt.subplots(figsize=(9, 4.4))
# plot order = CONFIGS order -> legend reads QUIC-BBR/QUIC-NewReno (col1), TCP-BBR/TCP-NewReno (col2)
for cfg,label,color in CONFIGS:
    if mean_gp[cfg] is not None:
        ax.plot(t, smooth(mean_gp[cfg]), color=color, label=label, lw=1.6)
for h in HO:
    ax.axvline(h, color='k', ls='--', lw=1.2, alpha=0.7)
    ax.text(h, ax.get_ylim()[1]*0.98, f' handover {h}s', va='top', ha='left', fontsize=8, color='k')
ax.grid(alpha=0.3); ax.set_xlim(0, T); ax.set_ylim(bottom=0)
ax.set_ylabel('DASH goodput (Mbps)')
ax.set_xlabel('simulation time (s)')
ax.legend(fontsize=9, ncol=2, loc='upper right', columnspacing=1.2)
fig.tight_layout()
fig.savefig(f'{OUT}/figC_handover_ensemble.png', dpi=150)
print('wrote figC_handover_ensemble.png (goodput-only)')
print('DONE')
