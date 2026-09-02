# BBRv3 Port — Results (2026-09-02)

Faithful loss-only port of Linux kernel TCP BBRv3 as `ns3::TcpBbr3`. Validated on the fair 10-UE
LEO/IAB campaign (600 s, handovers @131/393 s, 100 Mbps backhaul, BOLA, 22 seeds) — identical config
to the post-fix Table II baseline; only the CC differs.

## Table II (composite QoE, Yin-2015; 22 seeds unless noted)

| Config        | QoE_mean | QoE_med | Mbps | notes |
|---------------|---------:|--------:|-----:|-------|
| TCP NewReno   |   846.2  |  886.5  | 3.72 | winner |
| **TCP BBRv3** | **556.0**| **568.9**| **3.59** | **this port — 2nd best** |
| QUIC BBR      |   248.0  |  244.1  | 2.15 | |
| QUIC NewReno  |   218.4  |  264.8  | 2.61 | 21 seeds |
| TCP BBR (v1)  |  −719.3  | −724.5  | 2.59 | stock ns-3 BBRv1, 20 seeds (artifact) |

Ranking: **TCP NewReno > TCP BBRv3 > QUIC BBR > QUIC NewReno > TCP BBR(v1)**.

## Findings
- **BBRv3 makes BBR viable over lossy LEO.** QoE +556 vs stock BBRv1 −719 (a **+1275 swing**), and
  it nearly matches loss-based TCP-NewReno while beating both QUIC variants.
- **No cwnd collapse.** Server cwnd stays healthy across the full 600 s (seed 2, 60 s bins, KB):
  253, 271, 560, 377, 202, 338, 469, 547, 526, 563 — fluctuates with probing/handovers, recovers
  from PROBE_RTT/handover dips, never slides to the 4-packet floor. Stock BBRv1 pins at ~8.8 KB.
- The decisive port fix was the **2-slot `bw_hi` max-bw filter** (kernel-faithful). An earlier
  version mapped it onto a `WindowedFilter` that aged out the good sample -> BtlBw decayed to
  ~0.05 Mbps -> cwnd pinned at the floor (QoE −1116, worse than stock). Fixed 2026-09-01.

## Deviations (disclose in the paper methods note)
"TCP-BBRv3: faithful loss-only port of Linux kernel tcp_bbr.c (BBR_VERSION 3); ECN and PLB omitted
(no `delivered_ce` accounting in the ns-3.27 rate sampler; inert in this non-ECN LEO scenario);
`tx_in_flight` approximated by the rate sample's prior-in-flight."

## Artifacts
- Code: `src/internet/model/tcp-bbr3.{h,cc}` (branch `bbr3-port`, on GitHub).
- Results: `reference_baseline/paper_recheck/fair/tcp_bbr3/run_<seed>/` (22 seeds, 600 s).
- Table: `reference_baseline/paper_recheck/table2_with_bbr3.txt`; parser `parse_bbr3.py`.
- Ground truth: `bbr3_port/reference/linux_tcp_bbr_v3.c`. Full history: `bbr3_port/PLAN.md`.

## TODO (post-downtime)
- Regenerate the QoE box / cwnd-RTT / goodput figures to include the TCP-BBRv3 series (raw data on
  Grex `/project`, persists through maintenance).
- Fold the BBRv3 row + methods note into `revision_results.tex`.
