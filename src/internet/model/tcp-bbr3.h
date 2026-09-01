/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * ns-3 transcription of Linux kernel TCP BBRv3 (net/ipv4/tcp_bbr.c, BBR_VERSION 3,
 * google/bbr v3 branch). Ground-truth source: bbr3_port/reference/linux_tcp_bbr_v3.c.
 *
 * This is a faithful port of the kernel algorithm into ns-3's TcpCongestionOps /
 * TcpRateOps framework. Two documented deviations forced by ns-3.27's rate sampler:
 *   (1) ECN path is stubbed inert (no delivered_ce accounting in TcpRateSample) -> loss-only BBRv3.
 *   (2) inflight-hi-from-lost-packet uses rs.m_priorInFlight as a proxy for the kernel's
 *       per-packet rs->tx_in_flight (not tracked per-skb in ns-3).
 *
 * Kernel fixed-point note: the kernel scales gains by BBR_UNIT=256 (BBR_SCALE=8) and bandwidth
 * by 2^24 (BW_SCALE). Here gains/fractions are plain doubles and bandwidth is DataRate (bits/s);
 * each constant is annotated with the kernel expression it reproduces (including kernel integer
 * truncation, so behaviour matches).
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License version 2 as published by the Free Software Foundation.
 */

#ifndef TCP_BBR3_H
#define TCP_BBR3_H

#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/traced-value.h"
#include "ns3/windowed-filter.h"

#include <array>

namespace ns3 {

/**
 * \ingroup congestionOps
 * \brief BBRv3 congestion control (faithful port of Linux net/ipv4/tcp_bbr.c, BBR_VERSION 3).
 */
class TcpBbr3 : public TcpCongestionOps
{
public:
  static TypeId GetTypeId (void);

  TcpBbr3 ();
  TcpBbr3 (const TcpBbr3 &sock);

  /** BBR state-machine modes (kernel enum bbr_mode). */
  enum BbrMode_t
  {
    BBR_STARTUP,   /**< Ramp up sending rate rapidly to fill pipe */
    BBR_DRAIN,     /**< Drain any queue created during startup */
    BBR_PROBE_BW,  /**< Discover, share bw: pace around estimated bw */
    BBR_PROBE_RTT, /**< Cut inflight to min to probe min_rtt */
  };

  /** PROBE_BW cycle phases (kernel enum bbr_pacing_gain_phase). */
  enum BbrPacingGainPhase_t
  {
    BBR_BW_PROBE_UP,     /**< push up inflight to probe for bw/vol */
    BBR_BW_PROBE_DOWN,   /**< drain excess inflight from the queue */
    BBR_BW_PROBE_CRUISE, /**< use pipe, w/ headroom in queue/pipe */
    BBR_BW_PROBE_REFILL, /**< refill the pipe again to 100% */
  };

  /** Meaning of the incoming ACK stream w.r.t. bw probing (kernel enum bbr_ack_phase). */
  enum BbrAckPhase_t
  {
    BBR_ACKS_INIT,
    BBR_ACKS_REFILLING,
    BBR_ACKS_PROBE_STARTING,
    BBR_ACKS_PROBE_FEEDBACK,
    BBR_ACKS_PROBE_STOPPING,
  };

  static const char* const BbrModeName[BBR_PROBE_RTT + 1];

  virtual void SetStream (uint32_t stream);

  // TcpCongestionOps interface
  std::string GetName () const override;
  bool HasCongControl () const override;
  void CongControl (Ptr<TcpSocketState> tcb,
                    const TcpRateOps::TcpRateConnection &rc,
                    const TcpRateOps::TcpRateSample &rs) override;
  void CongestionStateSet (Ptr<TcpSocketState> tcb,
                           const TcpSocketState::TcpCongState_t newState) override;
  void CwndEvent (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event) override;
  uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
  Ptr<TcpCongestionOps> Fork () override;

private:
  // ---- kernel bbr_main pipeline --------------------------------------------------------------
  void InitPacingRate (Ptr<TcpSocketState> tcb);
  void InitRoundCounting ();
  void InitFullPipe ();

  void bbr_main (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_update_model (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_update_control_parameters (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);

  // Bandwidth model / max-bw filter
  DataRate bbr_max_bw () const;
  DataRate bbr_bw () const;
  void bbr_take_max_bw_sample (DataRate bw);
  void bbr_advance_max_bw_filter ();
  DataRate bbr_rate_bytes_per_sec (Ptr<TcpSocketState> tcb, DataRate rate, double gain) const;

  // Round / delivery signals
  bool bbr_update_round_start (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_calculate_bw_sample (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_update_latest_delivery_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_advance_latest_delivery_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);

  // Congestion signals: loss / ECN, bw_lo / inflight_lo, bw_hi / inflight_hi
  void bbr_update_congestion_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_reset_congestion_signals ();
  void bbr_init_lower_bounds (Ptr<TcpSocketState> tcb, bool init_bw);
  void bbr_loss_lower_bounds (Ptr<TcpSocketState> tcb);
  void bbr_reset_lower_bounds ();
  void bbr_adapt_lower_bounds (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  bool bbr_adapt_upper_bounds (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  bool bbr_is_inflight_too_high (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs) const;
  void bbr_handle_inflight_too_high (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs, bool rsmode);
  uint32_t bbr_inflight_hi_from_lost_packet (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs) const;

  // Loss-rate / STARTUP exit
  bool bbr_is_loss_too_high_in_startup (const TcpRateOps::TcpRateSample &rs) const;
  void bbr_check_loss_too_high_in_startup (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_handle_queue_too_high_in_startup (Ptr<TcpSocketState> tcb);

  // inflight_hi probing
  uint32_t bbr_target_inflight (Ptr<TcpSocketState> tcb) const;
  uint32_t bbr_inflight_with_headroom () const;
  void bbr_raise_inflight_hi_slope (Ptr<TcpSocketState> tcb);
  void bbr_probe_inflight_hi_upward (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);

  // PROBE_BW cycle
  bool bbr_is_reno_coexistence_probe_time (Ptr<TcpSocketState> tcb) const;
  bool bbr_check_time_to_probe_bw (Ptr<TcpSocketState> tcb);
  bool bbr_has_elapsed_in_phase (Time interval) const;
  bool bbr_check_time_to_cruise (Ptr<TcpSocketState> tcb, DataRate bw) const;
  void bbr_pick_probe_wait ();
  void bbr_set_cycle_idx (uint32_t cycle_idx);
  void bbr_start_bw_probe_down ();
  void bbr_start_bw_probe_cruise ();
  void bbr_start_bw_probe_refill (Ptr<TcpSocketState> tcb, uint32_t bw_probe_up_rounds);
  void bbr_start_bw_probe_up (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_update_cycle_phase (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  bool bbr_is_probing_bandwidth () const;

  // full-bw / drain / startup->probe transitions
  void bbr_check_full_bw_reached (const TcpRateOps::TcpRateSample &rs);
  bool bbr_full_bw_reached () const;
  void bbr_reset_full_bw ();
  void bbr_check_drain (Ptr<TcpSocketState> tcb);
  void bbr_update_gains ();

  // min-rtt / PROBE_RTT
  void bbr_update_min_rtt (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  uint32_t bbr_probe_rtt_cwnd (Ptr<TcpSocketState> tcb) const;
  void bbr_check_probe_rtt_done (Ptr<TcpSocketState> tcb);
  void bbr_exit_probe_rtt (Ptr<TcpSocketState> tcb);
  void bbr_save_cwnd (Ptr<const TcpSocketState> tcb);
  void RestoreCwnd (Ptr<TcpSocketState> tcb);

  // ack-aggregation extra cwnd
  void bbr_update_ack_aggregation (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  uint32_t bbr_ack_aggregation_cwnd () const;

  // cwnd / pacing / inflight-target
  uint32_t bbr_bdp (Ptr<TcpSocketState> tcb, DataRate bw, double gain) const;
  uint32_t bbr_quantization_budget (Ptr<TcpSocketState> tcb, uint32_t inflight) const;
  uint32_t bbr_inflight (Ptr<TcpSocketState> tcb, DataRate bw, double gain) const;
  void bbr_bound_cwnd_for_inflight_model (Ptr<TcpSocketState> tcb);
  bool bbr_set_cwnd_to_recover_or_restore (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs, uint32_t acked, uint32_t *newCwnd);
  void bbr_set_cwnd (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs);
  void bbr_set_pacing_rate (Ptr<TcpSocketState> tcb, double gain);
  void bbr_set_send_quantum (Ptr<TcpSocketState> tcb);
  void bbr_exit_loss_recovery (Ptr<TcpSocketState> tcb);

  void SetBbrState (BbrMode_t mode);

  // ---- constants (kernel bbr_* params; each annotated with its kernel expression) ------------
  static constexpr double BBR_UNIT = 256.0;             //!< kernel BBR_UNIT (1<<BBR_SCALE)
  static constexpr uint32_t bbr_cwnd_min_target = 4;    //!< min packets in flight
  static constexpr uint32_t bbr_min_rtt_win_sec = 10;   //!< min_rtt filter window (s)
  static constexpr uint32_t bbr_probe_rtt_mode_ms = 200;//!< min time at min cwnd in PROBE_RTT
  static constexpr uint32_t bbr_probe_rtt_win_ms = 5000;//!< probe_rtt_min filter window (ms)
  static constexpr double bbr_probe_rtt_cwnd_gain = 0.5;// BBR_UNIT*1/2
  static constexpr int bbr_pacing_margin_percent = 1;
  static constexpr double bbr_high_gain = 710.0 / BBR_UNIT;      // BBR_UNIT*277/100+1 = 710
  static constexpr double bbr_startup_pacing_gain = 710.0 / BBR_UNIT;
  static constexpr double bbr_startup_cwnd_gain = 2.0;          // BBR_UNIT*2
  static constexpr double bbr_drain_gain = 88.0 / BBR_UNIT;     // BBR_UNIT*1000/2885 = 88
  static constexpr double bbr_cwnd_gain = 2.0;                  // BBR_UNIT*2
  static constexpr double bbr_full_bw_thresh = 320.0 / BBR_UNIT;// BBR_UNIT*5/4 = 320 -> 1.25
  static constexpr uint32_t bbr_full_bw_cnt = 3;
  static constexpr double bbr_extra_acked_gain = 1.0;           // BBR_UNIT
  static constexpr uint32_t bbr_extra_acked_win_rtts = 5;
  static constexpr uint32_t bbr_ack_epoch_acked_reset_thresh = 1U << 20;
  static constexpr uint32_t bbr_extra_acked_max_us = 100 * 1000;
  static constexpr double bbr_beta = 76.0 / BBR_UNIT;          // BBR_UNIT*30/100 = 76 -> 0.2969
  static constexpr double bbr_loss_thresh = 5.0 / BBR_UNIT;    // BBR_UNIT*2/100 = 5 -> ~2%
  static constexpr uint32_t bbr_full_loss_cnt = 6;
  static constexpr double bbr_inflight_headroom = 38.0 / BBR_UNIT; // BBR_UNIT*15/100 = 38 -> 0.148
  static constexpr uint32_t bbr_bw_probe_max_rounds = 63;
  static constexpr uint32_t bbr_bw_probe_rand_rounds = 2;
  static constexpr uint32_t bbr_bw_probe_base_us = 2000000;    // 2 s
  static constexpr uint32_t bbr_bw_probe_rand_us = 1000000;    // 1 s
  static constexpr uint32_t bbr_bw_probe_cwnd_gain = 1;        // in units of BBR_UNIT/4 (=0.25)
  static constexpr bool bbr_fast_path = true;
  static constexpr bool bbr_fast_ack_mode = true;
  static constexpr bool bbr_loss_probe_recovery = true;
  // The PROBE_BW pacing_gain cycle {UP, DOWN, CRUISE, REFILL} (kernel bbr_pacing_gain[]).
  static const double PACING_GAIN[];

  typedef WindowedFilter<DataRate, MaxFilter<DataRate>, uint32_t, uint32_t> MaxBandwidthFilter_t;

  // ---- state (kernel struct bbr) -------------------------------------------------------------
  BbrMode_t m_state{BBR_STARTUP};
  TcpSocketState::TcpCongState_t m_prevCaState{TcpSocketState::CA_OPEN};

  MaxBandwidthFilter_t m_maxBwFilter;   //!< max-bw windowed filter (kernel bbr->bw_hi via minmax)
  uint32_t m_cycleCount{0};             //!< kernel filter time index for max_bw (rounds)
  Ptr<UniformRandomVariable> m_uv{nullptr};

  Time m_minRtt{Time::Max ()};          //!< min_rtt_us
  Time m_minRttStamp{Seconds (0)};      //!< min_rtt_stamp
  Time m_probeRttDoneStamp{Seconds (0)};//!< probe_rtt_done_stamp
  Time m_probeRttMin{Time::Max ()};     //!< probe_rtt_min_us
  Time m_probeRttMinStamp{Seconds (0)}; //!< probe_rtt_min_stamp
  uint32_t m_nextRttDelivered{0};       //!< next_rtt_delivered
  Time m_cycleStamp{Seconds (0)};       //!< cycle_mstamp

  bool m_roundStart{false};             //!< round_start
  bool m_ceState{false};                //!< ce_state
  uint32_t m_bwProbeUpRounds{0};        //!< bw_probe_up_rounds
  bool m_tryFastPath{false};            //!< try_fast_path
  bool m_idleRestart{false};            //!< idle_restart
  bool m_probeRttRoundDone{false};      //!< probe_rtt_round_done
  uint32_t m_initCwnd{0};               //!< init_cwnd

  double m_pacingGain{bbr_high_gain};   //!< pacing_gain (as fraction, not scaled)
  double m_cWndGain{bbr_high_gain};     //!< cwnd_gain
  bool m_fullBwReached{false};          //!< full_bw_reached
  uint32_t m_fullBwCnt{0};              //!< full_bw_cnt
  uint32_t m_cycleIdx{0};               //!< cycle_idx
  bool m_hasSeenRtt{false};             //!< has_seen_rtt

  uint32_t m_priorCwnd{0};              //!< prior_cwnd
  DataRate m_fullBw{0};                 //!< full_bw

  Time m_ackEpochStamp{Seconds (0)};    //!< ack_epoch_mstamp
  std::array<uint32_t, 2> m_extraAcked{{0, 0}}; //!< extra_acked[2]
  uint32_t m_ackEpochAcked{0};          //!< ack_epoch_acked
  uint32_t m_extraAckedWinRtts{0};      //!< extra_acked_win_rtts
  uint32_t m_extraAckedWinIdx{0};       //!< extra_acked_win_idx

  bool m_fullBwNow{false};              //!< full_bw_now
  uint32_t m_startupEcnRounds{0};       //!< startup_ecn_rounds
  bool m_lossInCycle{false};            //!< loss_in_cycle
  bool m_ecnInCycle{false};             //!< ecn_in_cycle
  uint32_t m_lossRoundDelivered{0};     //!< loss_round_delivered

  DataRate m_undoBwLo{0};               //!< undo_bw_lo
  uint32_t m_undoInflightLo{0};         //!< undo_inflight_lo
  uint32_t m_undoInflightHi{0};         //!< undo_inflight_hi
  DataRate m_bwLatest{0};               //!< bw_latest
  DataRate m_bwLo{DataRate (std::numeric_limits<uint64_t>::max ())};   //!< bw_lo (~infinite)
  std::array<DataRate, 2> m_bwHi{{DataRate (0), DataRate (0)}};        //!< bw_hi[2]
  uint32_t m_inflightLatest{0};         //!< inflight_latest
  TracedValue<uint32_t> m_inflightLo{UINT32_MAX}; //!< inflight_lo
  TracedValue<uint32_t> m_inflightHi{UINT32_MAX}; //!< inflight_hi
  uint32_t m_bwProbeUpCnt{0};           //!< bw_probe_up_cnt
  uint32_t m_bwProbeUpAcks{0};          //!< bw_probe_up_acks
  Time m_probeWait{Seconds (0)};        //!< probe_wait_us

  bool m_ecnEligible{false};            //!< ecn_eligible (always false: ECN stubbed)
  double m_ecnAlpha{1.0};               //!< ecn_alpha (EWMA), init 1.0
  bool m_bwProbeSamples{false};         //!< bw_probe_samples
  bool m_prevProbeTooHigh{false};       //!< prev_probe_too_high
  bool m_stoppedRiskyProbe{false};      //!< stopped_risky_probe
  uint32_t m_roundsSinceProbe{0};       //!< rounds_since_probe
  bool m_lossRoundStart{false};         //!< loss_round_start
  bool m_lossInRound{false};            //!< loss_in_round
  bool m_ecnInRound{false};             //!< ecn_in_round
  BbrAckPhase_t m_ackPhase{BBR_ACKS_INIT}; //!< ack_phase
  uint32_t m_lossEventsInRound{0};      //!< loss_events_in_round
  bool m_initialized{false};            //!< initialized

  uint32_t m_alphaLastDelivered{0};     //!< alpha_last_delivered
  uint32_t m_alphaLastDeliveredCe{0};   //!< alpha_last_delivered_ce

  // ns-3 bookkeeping (kernel reads these off tcp_sock / rate_sample)
  uint64_t m_delivered{0};              //!< tp->delivered (bytes), from TcpRateConnection
  uint32_t m_segmentSize{0};            //!< cached MSS
  bool m_packetConservation{false};     //!< packet_conservation (recovery)
  uint32_t m_sendQuantum{0};            //!< send quantum bytes
  uint32_t m_targetCwnd{0};             //!< last computed inflight target (for tracing)
  bool m_prevRoundLoss{false};          //!< helper for loss-round detection
};

} // namespace ns3

#endif // TCP_BBR3_H
