#!/usr/bin/env python3
"""Fig B (rev): cwnd + RTT of the REPRESENTATIVE (median-cwnd) server connection per config.
Replaces the old 'busiest connection' pick. Server-side (matches Table II RTT). Fair 28-seed set.
NOTE: QUIC-BBR uses current data; regenerate after the no-cap re-run lands."""
import glob, os, numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

RES='fair/results'; OUT='fair/Analysis_artifacts'; os.makedirs(OUT, exist_ok=True)
HO=[131,393]
FAIR=[2,3,4,5,7,10,12,13,17,19,20,21,23,25,26,28,31,32,33,34,36,37,41,43,44,46,47,49]
CONFIGS=[('quic_bbr','QUIC','QUIC-BBR','#1f77b4'),
         ('quic_newreno','QUIC','QUIC-NewReno','#ff7f0e'),
         ('tcp_bbr','TCP','TCP-BBR','#2ca02c'),
         ('tcp_newreno','TCP','TCP-NewReno','#9467bd')]
def load3(f):
    a=np.array(open(f,errors='ignore').read().split(),dtype=float); n=(a.size//3)*3
    return a[:n].reshape(-1,3) if n else None
def dsample(t,v,n=600):
    if t.size<=n: return t,v
    g=np.linspace(t.min(),t.max(),n); i=np.clip(np.searchsorted(t,g)-1,0,v.size-1); return g,v[i]

fig,ax=plt.subplots(2,1,figsize=(9,7),sharex=True)
for cfg,proto,label,color in CONFIGS:
    cand=[]   # (median_cwnd_bytes, cwnd_file)
    for s in FAIR:
        for fp in glob.glob(f'{RES}/{cfg}/run_{s}/server{proto}-cwnd-change*.txt'):
            d=load3(fp)
            if d is None or len(d)<100: continue      # skip listener/short-lived sockets
            cand.append((np.median(d[:,2]), fp))
    if not cand:
        print(f"{label}: no candidates"); continue
    cand.sort(key=lambda x:x[0])
    med_cwnd, cwnd_f = cand[len(cand)//2]              # population-median connection
    d=load3(cwnd_f); t,cw=dsample(d[:,0], d[:,2]/1e3)  # KB
    ax[0].plot(t,cw,color=color,label=label,lw=1.3)
    rtt_f=cwnd_f.replace('cwnd-change','rtt')
    dr=load3(rtt_f)
    if dr is not None:
        tr,rv=dsample(dr[:,0], dr[:,2]*1e3)            # ms
        ax[1].plot(tr,rv,color=color,label=label,lw=1.3)
    print(f"{label}: {len(cand)} conns, median-cwnd={med_cwnd/1e3:.0f}KB, file={os.path.basename(cwnd_f)}")
for a in ax:
    for h in HO: a.axvline(h,color='k',ls='--',lw=1,alpha=0.6)
    a.grid(alpha=0.3); a.set_xlim(0,600)
ax[0].set_ylabel('cwnd (KB)'); ax[1].set_ylabel('RTT (ms)'); ax[1].set_yscale('log')
ax[1].set_xlabel('simulation time (s)')
ax[0].legend(fontsize=9,ncol=2)
ax[0].set_title('Representative (median-cwnd) server connection per config  (dashed = handover)')
fig.tight_layout(); fig.savefig(f'{OUT}/figB_cwnd_rtt.png',dpi=150)
print('wrote figB_cwnd_rtt.png')
