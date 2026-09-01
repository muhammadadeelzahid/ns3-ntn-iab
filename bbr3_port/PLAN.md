# BBRv3 Port — Execution Plan (approve → auto mode)

**Objective:** Add a faithful ns-3 transcription of Linux kernel TCP **BBRv3** as a new congestion
control `ns3::TcpBbr3`, and produce a **new TCP-BBRv3 row** in the paper's Table II (keeping stock
TCP-BBR as the documented artifact row). Ground truth = `bbr3_port/reference/linux_tcp_bbr_v3.c`
(BBR_VERSION 3, 2408 lines). The existing ns-3 port (`bbr3_port/reference/ns3_tcp-bbr3.*`) is used
only as an API-mapping scaffold — it has faithfulness bugs (integer-truncated constants; BBRv1
quantization; hardcoded loss thresh) that are corrected against the kernel.

**Why:** stock ns-3 TcpBbr (BBRv1) pathologically collapses cwnd to the 4-packet floor under the
LEO channel's per-flow burst losses (loss-decrement + PROBE_RTT/recovery freezes). Six surgical
`tcp-bbr.cc` fixes failed (see memory `bbr_dash_cwnd_collapse.md`). BBRv3's model-based loss response
(2% loss threshold, inflight_hi/lo bounds) should degrade gracefully instead of collapsing.

- **Repo:** `/project/6030214/adeel/ns3-ntn-iab-dash`  **Branch:** `bbr3-port` (off clean `main`;
  stock `TcpBbr` untouched). Investigation edits live on branch `tcpbbr-recovery-gate` (commit db9cda3).
- **Build:** `python3 waf build` (optimized; ~4 min after a core-file change).
- **New files:** `src/internet/model/tcp-bbr3.{h,cc}`.

---

## Documented deviations (forced by ns-3.27 rate sampler; keep this list current)
1. **ECN path stubbed inert** — no `delivered_ce` accounting in `TcpRateSample`; BBRv3 runs loss-only.
   **STANDING RULE (user-approved):** ECN and PLB are skipped/stubbed *consistently across the whole
   port*, including the remaining chunks 4–5. Do NOT partially implement ECN. Concretely, in the
   remaining transcription: `bbr_update_model` skips `bbr_update_ecn_alpha`/`bbr_plb`; the PROBE_UP
   `full_bw_now`/ECN checks in `bbr_update_cycle_phase` are loss-only; `CwndEvent` ignores
   `CA_EVENT_ECN_IS_CE`/`CA_EVENT_ECN_NO_CE`; startup ECN-exit is inert (`m_ecnEligible` stays false).
   ECN/PLB state fields are retained only for `struct bbr` fidelity. **Disclose this explicitly in
   the Table II methods note** ("TCP-BBRv3: faithful loss-only port; ECN and PLB omitted, inert in
   this non-ECN LEO scenario").
2. **`tx_in_flight` ≈ `rs.m_priorInFlight`** — ns-3 has no per-skb tx_in_flight; used in
   `bbr_is_inflight_too_high`, `bbr_handle_inflight_too_high`, `bbr_inflight_hi_from_lost_packet`,
   `bbr_adapt_upper_bounds`.
3. **`ctx->sample_bw` = `rs.m_deliveryRate`** (ns-3 computes this natively).
4. **No TSO/GSO/EDT/PLB** — send quantum = 1 MSS; PLB omitted.
5. Units: kernel works in **packets**, this port works in **bytes** (×MSS). Bandwidth = `DataRate`.
Log any *new* deviation forced during the remaining transcription into this section.

---

## Status checklist

### Implementation — `tcp-bbr3.h`
- [x] State (`struct bbr`) mapped; enums; kernel-correct constants (annotated with kernel exprs).

### Implementation — `tcp-bbr3.cc`
- [x] Chunk 1: boilerplate, TypeId, ctor/copy/Fork, bandwidth/BDP/inflight/quantization,
      ack-agg cwnd, probe-rtt-cwnd, round-start, save/restore cwnd, pacing.
- [x] Chunk 2: full-bw/target-inflight helpers, STARTUP queue exit, inflight_hi upward probing,
      is_inflight_too_high, inflight_with_headroom, bound_cwnd_for_inflight_model, lower bounds,
      ProbeBW cycle transitions (down/cruise/refill/up), pick_probe_wait, reno-coexistence,
      check_time_to_probe/cruise, update_gains.
- [x] Chunk 3: reset/exit-recovery, latest-delivery-signals, update_congestion_signals,
      adapt_lower_bounds, handle_inflight_too_high, adapt_upper_bounds.
- [ ] Chunk 4: `bbr_update_cycle_phase` (finish switch: PROBE_UP/DOWN cases), `bbr_check_full_bw_reached`,
      `bbr_check_drain`, `bbr_update_min_rtt`, `bbr_check_probe_rtt_done`, `bbr_exit_probe_rtt`,
      `bbr_update_ack_aggregation`.
- [ ] Chunk 5: `bbr_set_cwnd` (+ `bbr_set_cwnd_to_recover_or_restore`), `bbr_update_model`,
      `bbr_update_control_parameters`, `CongControl` (bbr_main glue), init (`InitRoundCounting`,
      `InitFullPipe`, first-ACK init in `CongestionStateSet`), `CongestionStateSet`, `CwndEvent`,
      `GetSsThresh`. Read kernel lines 1826–2408 for these.

### Registration + build
- [ ] Add `model/tcp-bbr3.cc` to sources and `model/tcp-bbr3.h` to headers in `src/internet/wscript`.
- [ ] `python3 waf build`; fix 3.27 API compile deltas **inside tcp-bbr3.{h,cc} only**. Iterate to clean build.
- [ ] Sanity: `grep -c TcpBbr3 build/... ` / confirm TypeId `ns3::TcpBbr3` registers (a tiny run or
      `--PrintTypeIds` style check, or just that the scenario accepts `--ccAlgorithm=ns3::TcpBbr3`).

### Validation — 1 seed (gate)
- [ ] Run: `env` (no special vars) `build/scratch/ntn-iab-tcp-dash --ccAlgorithm=ns3::TcpBbr3 --run=2
      --numUes=10 --numRelay=1 --numSat=3 --hoTime=262 --simDuration=120 --backhaulRate=100Mbps
      --targetDt=30 --abrAlgorithm=ns3::BolaClient --traces=false --ueMobility=false`
      via SLURM (`--time=24:00:00`, 16G) into `handover_campaign/tcpbbr3_test/run_2`.
- [ ] Milestone watcher at sim-t ≥ 70; completion watcher. (Never tight-poll SLURM; read disk first.)
- [ ] **PASS criteria:** (a) no SIGSEGV / no wedge (sim-t advances); (b) server-socket cwnd (bin1,
      60–120 s) does **not** sit at the 4-pkt floor (5600 B) the way stock does — i.e. bin1 median
      ≫ ~8 KB and floor-fraction ≪ 54%; (c) `inflightHi`/`inflightLo` traces evolve sanely (not
      pinned at UINT32_MAX or 0). Analyzer: `handover_campaign/analyze_gate2160.py`.
      Baselines for comparison: stock TCP-BBR bin1 median 8.8 KB, 54% at floor.

### Validation — full 22-seed + Table II
- [ ] SLURM array over the 22 fair seeds → `fair/results/tcp_bbr3/run_<seed>/` (≈16 h/seed; array,
      not a loop). Config identical to the other fair configs.
- [ ] Parse QoE (reuse `reference_baseline/paper_recheck/parse_fair.py` pattern) → **add TCP-BBRv3
      row** to Table II next to stock TCP-BBR. Regenerate Figs (bitrate/playback, cwnd/RTT, goodput).
- [ ] Write results paragraph + methods note (BBRv3 port + the deviations above).

### Wrap-up
- [ ] **Do NOT commit during the autonomous run.** Leave all changes uncommitted on `bbr3-port`,
      present the regenerated Table II + figures, and WAIT for user approval. Commit only after
      approval, and then with **no AI/Claude attribution or co-author trailers** (user override).
- [ ] Update memory `bbr_dash_cwnd_collapse.md` and this checklist as milestones land (notes, not commits).

---

## Auto-mode rules

**Proceed automatically (no confirmation needed):**
- Finish chunks 4–5; register in wscript; build and fix compile errors *within* `tcp-bbr3.{h,cc}`.
- Launch the 1-seed run and its watchers; analyze on completion/milestone.
- If 1-seed PASSES: launch the 22-seed array, integrate, add the Table II row, regenerate figures.
- Keep this file's checklist + the memory updated (these are notes/results, not git commits).
- Re-provision walltime / resubmit if a job is killed by time limit (traces flush only on clean exit).

**DO NOT COMMIT AUTOMATICALLY (user-approved).** The autonomous run implements, tests, and
regenerates the paper results, leaving ALL changes **uncommitted** in the working tree on
`bbr3-port`. No `git commit`, no push, no PR. After the results are regenerated, **STOP and present
them for the user to approve**; commit only after explicit approval. (Milestone "commits" in this
plan mean progress-log/notes updates, never `git commit`.)

**Surface a checkpoint (report, then keep going):**
- First clean build. The 1-seed verdict. 22-seed integration + the new Table II numbers.

**Final gate (STOP, wait for approval):**
- Once the algorithm is implemented, validated, and the paper results (Table II row + figures) are
  regenerated, STOP and hand back for review/approval. Do not commit until approved.

**STOP and ask the user:**
- A fix would require editing files **outside** `tcp-bbr3.{h,cc}` (shared headers, `TcpRateOps`,
  `TcpSocketBase`) — scope/regression risk to the other 3 configs.
- 1-seed still crashes/wedges after **2** debugging attempts, OR BBRv3 behaves pathologically
  (collapses like stock, or storms/wedges the sim) — this becomes a science/scope decision.
- BBRv3 clearly *loses* to stock in a way that changes the paper's framing decision (replace vs add).
- Any ambiguity that would change the locked thesis or the "add as new row" framing.

**Ops constraints (carry forward):** never poll SLURM in tight loops (read from disk first,
rate-limit; 900 s poll interval); un-collapsed BBR runs ≈10× slower (~16 h per 120–200 s seed);
`serverTCP-cwnd-change` traces flush only at clean sim end (provision walltime, don't rely on
timeout-killed traces); no co-author/AI trailers in commits.

---

## Risk register
- **Unit bugs (packets↔bytes):** highest-likelihood defect class. Mitigation: every kernel packet
  quantity ×MSS; audited per function. Watch inflight_hi/lo growing in MSS steps.
- **`inflight_hi/lo` init to UINT32_MAX:** must be treated as "unset" everywhere (kernel `~0U`).
- **Rate-sample semantics differ 3.27 vs kernel:** `m_bytesLoss`, `m_priorInFlight`, `m_deliveryRate`
  meanings validated on the 1-seed (sane trace evolution) before trusting the 22-seed.
- **Sim slowdown/instability from large cwnd:** keep an eye on wall-rate; if a storm/wedge appears,
  that's a STOP condition (report), not a silent workaround.
- **Fallback if BBRv3 can't be made to behave:** revert to disclose (stock TcpBbr caveat) — but only
  after reporting, per STOP rules.

## Progress log
- 2026-08-31: branch + references set up; `tcp-bbr3.h` done; `tcp-bbr3.cc` chunks 1–3 done
  (~45 functions, kernel-audited). Next: chunk 4.
- 2026-08-31: `tcp-bbr3.cc` chunks 4–5 done (~55 functions total; ECN/PLB stubbed throughout).
  Registered in wscript. Builds clean (fixed: DataRate accessor, TracedValue/std::max, constexpr
  ODR-use). Reconfigured tree debug->optimized and rebuilt. Smoke test (5s) clean, TypeId works.
- 2026-08-31: **1-seed gate PASS.** seed2 120s optimized (was job 7561723, cancelled at sim-t92
  after milestone). Server cwnd: bin0 89KB, **bin1 median 22.5KB / 36% floor** vs stock TCP-BBR
  **8.8KB / 54%**. No crash/wedge. => BBRv3 degrades gracefully (cwnd ~2.5x higher, less floored),
  not fully healthy (~130KB target) — expected loss-aware-BBRv3-on-lossy-LEO behavior.
- 2026-08-31: launched 22-seed fair array **SLURM 7561730** (ns3::TcpBbr3, exact fair recheck config:
  600s, hoTime=262, feederDelay=0.010, BOLA, 22 seeds) -> reference_baseline/paper_recheck/fair/tcp_bbr3/.
  ~8.5h/seed. On completion: parse QoE, add TCP-BBRv3 row to post-fix Table II, regen figures, STOP
  for approval (NO auto-commit).
