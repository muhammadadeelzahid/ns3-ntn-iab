/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * ns-3 transcription of Linux kernel TCP BBRv3 (net/ipv4/tcp_bbr.c, BBR_VERSION 3).
 * Ground truth: bbr3_port/reference/linux_tcp_bbr_v3.c. See tcp-bbr3.h for the two documented
 * deviations (ECN stubbed inert; tx_in_flight approximated by rs.m_priorInFlight).
 *
 * Unit convention: the kernel works in *packets*; ns-3's TcpSocketState works in *bytes*. This port
 * works in bytes throughout — kernel packet quantities are multiplied by MSS. Bandwidth is carried
 * as ns-3 DataRate (bits/s) instead of the kernel's pkt/us<<BW_SCALE fixed point. Gains are plain
 * doubles instead of <<BBR_SCALE fixed point.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License version 2 as published by the Free Software Foundation.
 */

#include "tcp-bbr3.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpBbr3");
NS_OBJECT_ENSURE_REGISTERED (TcpBbr3);

// PROBE_BW pacing_gain cycle {UP, DOWN, CRUISE, REFILL} (kernel bbr_pacing_gain[]).
// 5/4, 91/100 (kernel int: 256*91/100=232 -> 232/256=0.90625), 1, 1.
const double TcpBbr3::PACING_GAIN[] = {320.0 / 256.0, 232.0 / 256.0, 1.0, 1.0};

const char* const
TcpBbr3::BbrModeName[BBR_PROBE_RTT + 1] = {
  "BBR_STARTUP", "BBR_DRAIN", "BBR_PROBE_BW", "BBR_PROBE_RTT"
};

TypeId
TcpBbr3::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpBbr3")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpBbr3> ()
    .SetGroupName ("Internet")
    .AddAttribute ("Stream",
                   "Random number stream (default is set to 4 to align with Linux results)",
                   UintegerValue (4),
                   MakeUintegerAccessor (&TcpBbr3::SetStream),
                   MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("InflightHi",
                     "Upper bound on inflight (bytes)",
                     MakeTraceSourceAccessor (&TcpBbr3::m_inflightHi),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("InflightLo",
                     "Lower bound on inflight (bytes)",
                     MakeTraceSourceAccessor (&TcpBbr3::m_inflightLo),
                     "ns3::TracedValueCallback::Uint32")
  ;
  return tid;
}

TcpBbr3::TcpBbr3 ()
  : TcpCongestionOps ()
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
}

TcpBbr3::TcpBbr3 (const TcpBbr3 &sock)
  : TcpCongestionOps (sock),
    m_state (sock.m_state),
    m_prevCaState (sock.m_prevCaState),
    m_maxBwFilter (sock.m_maxBwFilter),
    m_cycleCount (sock.m_cycleCount),
    m_uv (sock.m_uv),
    m_minRtt (sock.m_minRtt),
    m_minRttStamp (sock.m_minRttStamp),
    m_probeRttDoneStamp (sock.m_probeRttDoneStamp),
    m_probeRttMin (sock.m_probeRttMin),
    m_probeRttMinStamp (sock.m_probeRttMinStamp),
    m_nextRttDelivered (sock.m_nextRttDelivered),
    m_cycleStamp (sock.m_cycleStamp),
    m_roundStart (sock.m_roundStart),
    m_ceState (sock.m_ceState),
    m_bwProbeUpRounds (sock.m_bwProbeUpRounds),
    m_tryFastPath (sock.m_tryFastPath),
    m_idleRestart (sock.m_idleRestart),
    m_probeRttRoundDone (sock.m_probeRttRoundDone),
    m_initCwnd (sock.m_initCwnd),
    m_pacingGain (sock.m_pacingGain),
    m_cWndGain (sock.m_cWndGain),
    m_fullBwReached (sock.m_fullBwReached),
    m_fullBwCnt (sock.m_fullBwCnt),
    m_cycleIdx (sock.m_cycleIdx),
    m_hasSeenRtt (sock.m_hasSeenRtt),
    m_priorCwnd (sock.m_priorCwnd),
    m_fullBw (sock.m_fullBw),
    m_ackEpochStamp (sock.m_ackEpochStamp),
    m_extraAcked (sock.m_extraAcked),
    m_ackEpochAcked (sock.m_ackEpochAcked),
    m_extraAckedWinRtts (sock.m_extraAckedWinRtts),
    m_extraAckedWinIdx (sock.m_extraAckedWinIdx),
    m_fullBwNow (sock.m_fullBwNow),
    m_startupEcnRounds (sock.m_startupEcnRounds),
    m_lossInCycle (sock.m_lossInCycle),
    m_ecnInCycle (sock.m_ecnInCycle),
    m_lossRoundDelivered (sock.m_lossRoundDelivered),
    m_undoBwLo (sock.m_undoBwLo),
    m_undoInflightLo (sock.m_undoInflightLo),
    m_undoInflightHi (sock.m_undoInflightHi),
    m_bwLatest (sock.m_bwLatest),
    m_bwLo (sock.m_bwLo),
    m_bwHi (sock.m_bwHi),
    m_inflightLatest (sock.m_inflightLatest),
    m_inflightLo (sock.m_inflightLo),
    m_inflightHi (sock.m_inflightHi),
    m_bwProbeUpCnt (sock.m_bwProbeUpCnt),
    m_bwProbeUpAcks (sock.m_bwProbeUpAcks),
    m_probeWait (sock.m_probeWait),
    m_ecnEligible (sock.m_ecnEligible),
    m_ecnAlpha (sock.m_ecnAlpha),
    m_bwProbeSamples (sock.m_bwProbeSamples),
    m_prevProbeTooHigh (sock.m_prevProbeTooHigh),
    m_stoppedRiskyProbe (sock.m_stoppedRiskyProbe),
    m_roundsSinceProbe (sock.m_roundsSinceProbe),
    m_lossRoundStart (sock.m_lossRoundStart),
    m_lossInRound (sock.m_lossInRound),
    m_ecnInRound (sock.m_ecnInRound),
    m_ackPhase (sock.m_ackPhase),
    m_lossEventsInRound (sock.m_lossEventsInRound),
    m_initialized (sock.m_initialized),
    m_alphaLastDelivered (sock.m_alphaLastDelivered),
    m_alphaLastDeliveredCe (sock.m_alphaLastDeliveredCe),
    m_delivered (sock.m_delivered),
    m_segmentSize (sock.m_segmentSize),
    m_packetConservation (sock.m_packetConservation),
    m_sendQuantum (sock.m_sendQuantum),
    m_targetCwnd (sock.m_targetCwnd),
    m_prevRoundLoss (sock.m_prevRoundLoss)
{
  NS_LOG_FUNCTION (this);
}

std::string
TcpBbr3::GetName () const
{
  return "TcpBbr3";
}

bool
TcpBbr3::HasCongControl () const
{
  return true;
}

Ptr<TcpCongestionOps>
TcpBbr3::Fork (void)
{
  return CopyObject<TcpBbr3> (this);
}

void
TcpBbr3::SetStream (uint32_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
}

void
TcpBbr3::SetBbrState (BbrMode_t mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_state = mode;
}

// ---- bandwidth model -------------------------------------------------------------------------

// kernel bbr_max_bw(): max(bw_hi[0], bw_hi[1]). Faithful 2-slot filter (NOT a windowed filter — a
// windowed filter ages out the good sample and reintroduces BBRv1-style BtlBw decay/collapse).
DataRate
TcpBbr3::bbr_max_bw () const
{
  return std::max (m_bwHi[0], m_bwHi[1]);
}

// kernel bbr_bw(): min(bbr_max_bw, bw_lo).
DataRate
TcpBbr3::bbr_bw () const
{
  return std::min (bbr_max_bw (), m_bwLo);
}

// kernel bbr_take_max_bw_sample(): bw_hi[1] = max(bw_hi[1], bw).
void
TcpBbr3::bbr_take_max_bw_sample (DataRate bw)
{
  NS_LOG_FUNCTION (this << bw);
  m_bwHi[1] = std::max (m_bwHi[1], bw);
}

// kernel bbr_advance_max_bw_filter(): shift bw_hi[1]->bw_hi[0], reset bw_hi[1]. Keeps max of last
// 1-2 bw-probe cycles; called only at PROBE_STOPPING, so the estimate is held across a probe cycle.
void
TcpBbr3::bbr_advance_max_bw_filter ()
{
  NS_LOG_FUNCTION (this);
  if (m_bwHi[1] == DataRate (0))
    {
      return; // no samples in this window; keep the old window
    }
  m_bwHi[0] = m_bwHi[1];
  m_bwHi[1] = DataRate (0);
}

// kernel bbr_rate_bytes_per_sec(): rate(bytes/s) = bw * gain, with a pacing margin. Here bw is a
// DataRate so we scale its bit-rate by gain and (1 - margin/100).
DataRate
TcpBbr3::bbr_rate_bytes_per_sec (Ptr<TcpSocketState> tcb, DataRate rate, double gain) const
{
  double bps = rate.GetBitRate () * gain;
  bps = bps * (100 - bbr_pacing_margin_percent) / 100.0;
  return DataRate (std::max<uint64_t> (static_cast<uint64_t> (bps), 1));
}

// ---- BDP / inflight target -------------------------------------------------------------------

// kernel bbr_bdp(): bdp = ceil(bw * min_rtt * gain). Returns BYTES here.
uint32_t
TcpBbr3::bbr_bdp (Ptr<TcpSocketState> tcb, DataRate bw, double gain) const
{
  if (m_minRtt == Time::Max ())
    {
      return m_initCwnd; // no valid RTT sample yet: cap at initial cwnd (bytes)
    }
  // bytes = bw[bits/s]/8 * rtt[s]; then apply gain and round up.
  double bytes = (bw.GetBitRate () / 8.0) * m_minRtt.GetSeconds ();
  double bdp = std::ceil (bytes * gain);
  return static_cast<uint32_t> (bdp);
}

// kernel bbr_quantization_budget(): floor at cwnd_min_target; +2 pkts in PROBE_UP. In bytes.
uint32_t
TcpBbr3::bbr_quantization_budget (Ptr<TcpSocketState> tcb, uint32_t cwnd) const
{
  cwnd = std::max (cwnd, bbr_cwnd_min_target * m_segmentSize);
  if (m_state == BBR_PROBE_BW && m_cycleIdx == BBR_BW_PROBE_UP)
    {
      cwnd += 2 * m_segmentSize;
    }
  return cwnd;
}

// kernel bbr_inflight(): quantized bdp.
uint32_t
TcpBbr3::bbr_inflight (Ptr<TcpSocketState> tcb, DataRate bw, double gain) const
{
  uint32_t inflight = bbr_bdp (tcb, bw, gain);
  inflight = bbr_quantization_budget (tcb, inflight);
  return inflight;
}

// kernel bbr_ack_aggregation_cwnd(): extra cwnd (bytes) for ACK aggregation.
uint32_t
TcpBbr3::bbr_ack_aggregation_cwnd () const
{
  uint32_t aggrCwnd = 0;
  if (bbr_extra_acked_gain && m_fullBwReached)
    {
      // max_aggr = bw * extra_acked_max_us(100ms); extra = gain * max(extra_acked[0],[1]).
      uint32_t maxAggr = static_cast<uint32_t> ((bbr_bw ().GetBitRate () / 8.0)
                                                * (bbr_extra_acked_max_us / 1e6));
      aggrCwnd = static_cast<uint32_t> (bbr_extra_acked_gain
                                        * std::max (m_extraAcked[0], m_extraAcked[1]));
      aggrCwnd = std::min (aggrCwnd, maxAggr);
    }
  return aggrCwnd;
}

// kernel bbr_probe_rtt_cwnd(): max(cwnd_min_target, bdp(bw, probe_rtt_cwnd_gain)).
uint32_t
TcpBbr3::bbr_probe_rtt_cwnd (Ptr<TcpSocketState> tcb) const
{
  return std::max (bbr_cwnd_min_target * m_segmentSize,
                   bbr_bdp (tcb, bbr_bw (), bbr_probe_rtt_cwnd_gain));
}

// ---- round / bw sample -----------------------------------------------------------------------

// kernel bbr_update_round_start(): detect a new packet-timed round; return round_delivered (bytes).
bool
TcpBbr3::bbr_update_round_start (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  NS_LOG_FUNCTION (this << tcb);
  m_roundStart = false;
  if (rs.m_delivered > 0 && rs.m_priorDelivered >= m_nextRttDelivered)
    {
      m_nextRttDelivered = m_delivered;
      m_roundStart = true;
    }
  return m_roundStart;
}

// kernel bbr_calculate_bw_sample(): bw = delivered / interval. Here delivered is bytes and the
// rate sample already provides a DataRate (m_deliveryRate), so use it directly.
void
TcpBbr3::bbr_calculate_bw_sample (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  // ns-3 TcpRateOps computes m_deliveryRate = ackedSacked / (max sendElapsed, ackElapsed).
  // This plays the role of the kernel ctx->sample_bw.
}

// ---- save/restore cwnd -----------------------------------------------------------------------

// kernel bbr_save_cwnd().
void
TcpBbr3::bbr_save_cwnd (Ptr<const TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this);
  if (m_prevCaState < TcpSocketState::CA_RECOVERY && m_state != BBR_PROBE_RTT)
    {
      m_priorCwnd = tcb->m_cWnd; // this cwnd is good enough
    }
  else // loss recovery or PROBE_RTT have temporarily cut cwnd
    {
      m_priorCwnd = std::max (m_priorCwnd, tcb->m_cWnd.Get ());
    }
}

void
TcpBbr3::RestoreCwnd (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this);
  tcb->m_cWnd = std::max (m_priorCwnd, tcb->m_cWnd.Get ());
}

// ---- pacing ----------------------------------------------------------------------------------

// kernel bbr_init_pacing_rate_from_rtt(): startup_pacing_gain * init_cwnd / RTT.
void
TcpBbr3::InitPacingRate (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (!tcb->m_pacing)
    {
      NS_LOG_WARN ("BBR must use pacing");
      tcb->m_pacing = true;
    }
  Time rtt;
  if (tcb->m_minRtt != Time::Max ())
    {
      rtt = MilliSeconds (std::max<int64_t> (tcb->m_minRtt.GetMilliSeconds (), 1));
      m_hasSeenRtt = true;
    }
  else
    {
      rtt = MilliSeconds (1);
    }
  // nominal bw = cwnd / rtt (bytes/s -> bits/s).
  DataRate nominalBw (static_cast<uint64_t> (tcb->m_cWnd * 8.0 / rtt.GetSeconds ()));
  tcb->m_pacingRate = DataRate (static_cast<uint64_t> (bbr_startup_pacing_gain * nominalBw.GetBitRate ()));
}

// kernel bbr_set_pacing_rate(): pace at bw*gain; only raise once pipe filled (or if higher).
void
TcpBbr3::bbr_set_pacing_rate (Ptr<TcpSocketState> tcb, double gain)
{
  NS_LOG_FUNCTION (this << tcb << gain);
  DataRate rate = bbr_rate_bytes_per_sec (tcb, bbr_bw (), gain);
  rate = DataRate (std::min (rate.GetBitRate (), tcb->m_maxPacingRate.GetBitRate ()));
  if (!m_hasSeenRtt && tcb->m_minRtt != Time::Max ())
    {
      InitPacingRate (tcb);
    }
  if (m_fullBwReached || rate > tcb->m_pacingRate)
    {
      tcb->m_pacingRate = rate;
    }
}

// kernel bbr_set_send_quantum(): ns-3 has no TSO; keep a nominal 1-MSS quantum.
void
TcpBbr3::bbr_set_send_quantum (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_sendQuantum = m_segmentSize;
}

// ---- full-bw / target-inflight / probing-state helpers ---------------------------------------

bool
TcpBbr3::bbr_full_bw_reached () const
{
  return m_fullBwReached;
}

// kernel bbr_reset_full_bw().
void
TcpBbr3::bbr_reset_full_bw ()
{
  m_fullBw = DataRate (0);
  m_fullBwCnt = 0;
  m_fullBwNow = false;
}

// kernel bbr_target_inflight(): min(bdp(bw,1.0), cwnd). Bytes.
uint32_t
TcpBbr3::bbr_target_inflight (Ptr<TcpSocketState> tcb) const
{
  uint32_t bdp = bbr_inflight (tcb, bbr_bw (), 1.0);
  return std::min (bdp, tcb->m_cWnd.Get ());
}

// kernel bbr_is_probing_bandwidth().
bool
TcpBbr3::bbr_is_probing_bandwidth () const
{
  return (m_state == BBR_STARTUP)
         || (m_state == BBR_PROBE_BW
             && (m_cycleIdx == BBR_BW_PROBE_REFILL || m_cycleIdx == BBR_BW_PROBE_UP));
}

// kernel bbr_has_elapsed_in_phase(): time since phase start > interval.
bool
TcpBbr3::bbr_has_elapsed_in_phase (Time interval) const
{
  return (Simulator::Now () - (m_cycleStamp + interval)) > Seconds (0);
}

// ---- STARTUP loss/queue exit -----------------------------------------------------------------

// kernel bbr_handle_queue_too_high_in_startup().
void
TcpBbr3::bbr_handle_queue_too_high_in_startup (Ptr<TcpSocketState> tcb)
{
  m_fullBwReached = true;
  uint32_t bdp = bbr_inflight (tcb, bbr_max_bw (), 1.0);
  m_inflightHi = std::max (bdp, m_inflightLatest);
}

// ---- inflight_hi upward probing --------------------------------------------------------------

// kernel bbr_raise_inflight_hi_slope(): packets S/ACKed per inflight_hi increment.
void
TcpBbr3::bbr_raise_inflight_hi_slope (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  uint32_t growthThisRound = 1U << m_bwProbeUpRounds;
  m_bwProbeUpRounds = std::min<uint32_t> (m_bwProbeUpRounds + 1, 30);
  // kernel: cnt = cwnd(pkts) / growth. In bytes we keep cnt as an ACKed-bytes budget per MSS
  // increment: cnt = (cwnd/growth) bytes ACKed -> one MSS of inflight_hi growth.
  uint32_t cnt = std::max<uint32_t> (tcb->m_cWnd.Get () / growthThisRound, 1);
  m_bwProbeUpCnt = cnt;
}

// kernel bbr_probe_inflight_hi_upward().
void
TcpBbr3::bbr_probe_inflight_hi_upward (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  NS_LOG_FUNCTION (this << tcb);
  // kernel gate: is_cwnd_limited && cwnd >= inflight_hi. ns-3 lacks is_cwnd_limited; approximate
  // by "cwnd has reached inflight_hi" (fully using the probe budget).
  if (tcb->m_cWnd < m_inflightHi)
    {
      m_bwProbeUpAcks = 0;
      return;
    }
  m_bwProbeUpAcks += rs.m_ackedSacked;
  if (m_bwProbeUpAcks >= m_bwProbeUpCnt)
    {
      uint32_t delta = m_bwProbeUpAcks / m_bwProbeUpCnt;
      m_bwProbeUpAcks -= delta * m_bwProbeUpCnt;
      m_inflightHi = m_inflightHi + delta * m_segmentSize; // +delta packets, in bytes
      m_tryFastPath = false;
    }
  if (m_roundStart)
    {
      bbr_raise_inflight_hi_slope (tcb);
    }
}

// kernel bbr_is_inflight_too_high(): loss rate > loss_thresh (ECN path stubbed).
// tx_in_flight is not tracked per-skb in ns-3; use rs.m_priorInFlight as the proxy (documented).
bool
TcpBbr3::bbr_is_inflight_too_high (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs) const
{
  uint32_t txInFlight = rs.m_priorInFlight; // proxy for kernel rs->tx_in_flight
  if (rs.m_bytesLoss > 0 && txInFlight > 0)
    {
      uint32_t lossThresh = static_cast<uint32_t> (txInFlight * bbr_loss_thresh);
      if (rs.m_bytesLoss > lossThresh)
        {
          return true;
        }
    }
  // ECN path intentionally omitted (no delivered_ce accounting in ns-3.27 rate sampler).
  return false;
}

// kernel bbr_inflight_hi_from_lost_skb(): solve for the inflight prefix where loss crossed
// loss_thresh. ns-3 has no per-skb tx_in_flight/pcount, so approximate with the rate sample's
// pre-ACK inflight and total bytesLoss (documented deviation).
uint32_t
TcpBbr3::bbr_inflight_hi_from_lost_packet (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs) const
{
  uint32_t pcount = rs.m_ackedSacked ? rs.m_ackedSacked : m_segmentSize;
  int64_t inflightPrev = static_cast<int64_t> (rs.m_priorInFlight) - pcount;
  if (inflightPrev < 0)
    {
      return UINT32_MAX;
    }
  int64_t lostPrev = static_cast<int64_t> (rs.m_bytesLoss) - pcount;
  if (lostPrev < 0)
    {
      lostPrev = 0;
    }
  double lossBudget = inflightPrev * bbr_loss_thresh; // bytes of loss tolerated before this skb
  double lostPrefix;
  if (lostPrev >= lossBudget)
    {
      lostPrefix = 0;
    }
  else
    {
      double divisor = 1.0 - bbr_loss_thresh;
      lostPrefix = (lossBudget - lostPrev) / divisor;
    }
  return static_cast<uint32_t> (inflightPrev + lostPrefix);
}

// kernel bbr_inflight_with_headroom(): leave (1 - inflight_headroom) of inflight_hi.
uint32_t
TcpBbr3::bbr_inflight_with_headroom () const
{
  if (m_inflightHi == UINT32_MAX)
    {
      return UINT32_MAX;
    }
  uint32_t inflightHi = m_inflightHi.Get ();
  uint32_t headroom = static_cast<uint32_t> (inflightHi * bbr_inflight_headroom);
  headroom = std::max<uint32_t> (headroom, 1);
  uint32_t floor = bbr_cwnd_min_target * m_segmentSize;
  return (inflightHi > headroom) ? std::max<uint32_t> (inflightHi - headroom, floor) : floor;
}

// kernel bbr_bound_cwnd_for_inflight_model().
void
TcpBbr3::bbr_bound_cwnd_for_inflight_model (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (!m_initialized)
    {
      return;
    }
  uint32_t cap = UINT32_MAX;
  if (m_state == BBR_PROBE_BW && m_cycleIdx != BBR_BW_PROBE_CRUISE)
    {
      cap = m_inflightHi;
    }
  else if (m_state == BBR_PROBE_RTT
           || (m_state == BBR_PROBE_BW && m_cycleIdx == BBR_BW_PROBE_CRUISE))
    {
      cap = bbr_inflight_with_headroom ();
    }
  cap = std::min (cap, m_inflightLo.Get ());
  cap = std::max (cap, bbr_cwnd_min_target * m_segmentSize);
  tcb->m_cWnd = std::min (tcb->m_cWnd.Get (), cap);
}

// ---- lower bounds (bw_lo / inflight_lo) ------------------------------------------------------

// kernel bbr_init_lower_bounds().
void
TcpBbr3::bbr_init_lower_bounds (Ptr<TcpSocketState> tcb, bool init_bw)
{
  if (init_bw && m_bwLo == DataRate (std::numeric_limits<uint64_t>::max ()))
    {
      m_bwLo = bbr_max_bw ();
    }
  if (m_inflightLo == UINT32_MAX)
    {
      m_inflightLo = tcb->m_cWnd.Get ();
    }
}

// kernel bbr_loss_lower_bounds(): reduce bw_lo / inflight_lo to (1 - beta), floored at *_latest.
void
TcpBbr3::bbr_loss_lower_bounds (Ptr<TcpSocketState> tcb)
{
  double lossCut = 1.0 - bbr_beta; // kernel BBR_UNIT - beta
  uint64_t bwLoCut = static_cast<uint64_t> (m_bwLo.GetBitRate () * lossCut);
  m_bwLo = std::max (m_bwLatest, DataRate (bwLoCut));
  m_inflightLo = std::max (m_inflightLatest,
                           static_cast<uint32_t> (m_inflightLo.Get () * lossCut));
}

// kernel bbr_reset_lower_bounds().
void
TcpBbr3::bbr_reset_lower_bounds ()
{
  m_bwLo = DataRate (std::numeric_limits<uint64_t>::max ());
  m_inflightLo = UINT32_MAX;
}

// ---- ProbeBW cycle transitions ---------------------------------------------------------------

void
TcpBbr3::bbr_set_cycle_idx (uint32_t cycle_idx)
{
  m_cycleIdx = cycle_idx;
  m_tryFastPath = false;
}

// kernel bbr_start_bw_probe_down().
void
TcpBbr3::bbr_start_bw_probe_down ()
{
  bbr_reset_congestion_signals ();
  m_bwProbeUpCnt = UINT32_MAX;
  bbr_pick_probe_wait ();
  m_cycleStamp = Simulator::Now ();
  m_ackPhase = BBR_ACKS_PROBE_STOPPING;
  m_nextRttDelivered = m_delivered;
  bbr_set_cycle_idx (BBR_BW_PROBE_DOWN);
}

// kernel bbr_start_bw_probe_cruise().
void
TcpBbr3::bbr_start_bw_probe_cruise ()
{
  if (m_inflightLo != UINT32_MAX)
    {
      m_inflightLo = std::min (m_inflightLo.Get (), m_inflightHi.Get ());
    }
  bbr_set_cycle_idx (BBR_BW_PROBE_CRUISE);
}

// kernel bbr_start_bw_probe_refill().
void
TcpBbr3::bbr_start_bw_probe_refill (Ptr<TcpSocketState> tcb, uint32_t bw_probe_up_rounds)
{
  bbr_reset_lower_bounds ();
  m_bwProbeUpRounds = bw_probe_up_rounds;
  m_bwProbeUpAcks = 0;
  m_stoppedRiskyProbe = false;
  m_ackPhase = BBR_ACKS_REFILLING;
  m_nextRttDelivered = m_delivered;
  bbr_set_cycle_idx (BBR_BW_PROBE_REFILL);
}

// kernel bbr_start_bw_probe_up().
void
TcpBbr3::bbr_start_bw_probe_up (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  m_ackPhase = BBR_ACKS_PROBE_STARTING;
  m_nextRttDelivered = m_delivered;
  m_cycleStamp = Simulator::Now ();
  bbr_set_cycle_idx (BBR_BW_PROBE_UP);
  bbr_raise_inflight_hi_slope (tcb);
}

// kernel bbr_pick_probe_wait(): rounds_since_probe rand, probe_wait = base + rand time.
void
TcpBbr3::bbr_pick_probe_wait ()
{
  m_roundsSinceProbe = m_uv->GetInteger (0, bbr_bw_probe_rand_rounds - 1);
  m_probeWait = MicroSeconds (bbr_bw_probe_base_us
                              + m_uv->GetInteger (0, bbr_bw_probe_rand_us - 1));
}

// kernel bbr_is_reno_coexistence_probe_time().
bool
TcpBbr3::bbr_is_reno_coexistence_probe_time (Ptr<TcpSocketState> tcb) const
{
  uint32_t maxRounds = bbr_bw_probe_max_rounds; // local copy avoids ODR-use of the constexpr
  uint32_t rounds = std::min (maxRounds, bbr_target_inflight (tcb) / m_segmentSize);
  return m_roundsSinceProbe >= rounds;
}

// kernel bbr_check_time_to_probe_bw(): decide whether to REFILL toward a fresh bw probe.
bool
TcpBbr3::bbr_check_time_to_probe_bw (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (bbr_has_elapsed_in_phase (m_probeWait) || bbr_is_reno_coexistence_probe_time (tcb))
    {
      bbr_start_bw_probe_refill (tcb, 0);
      return true;
    }
  return false;
}

// kernel bbr_check_time_to_cruise(): true once inflight has drained to <= target with headroom.
bool
TcpBbr3::bbr_check_time_to_cruise (Ptr<TcpSocketState> tcb, DataRate bw) const
{
  if (m_inflightHi != UINT32_MAX && bbr_inflight_with_headroom () < tcb->m_bytesInFlight.Get ())
    {
      return false;
    }
  return tcb->m_bytesInFlight.Get () <= bbr_inflight (tcb, bw, 1.0);
}

// kernel bbr_update_gains(): set pacing/cwnd gains per mode.
void
TcpBbr3::bbr_update_gains ()
{
  switch (m_state)
    {
    case BBR_STARTUP:
      m_pacingGain = bbr_startup_pacing_gain;
      m_cWndGain = bbr_startup_cwnd_gain;
      break;
    case BBR_DRAIN:
      m_pacingGain = bbr_drain_gain;
      m_cWndGain = bbr_startup_cwnd_gain;
      break;
    case BBR_PROBE_BW:
      m_pacingGain = PACING_GAIN[m_cycleIdx];
      m_cWndGain = bbr_cwnd_gain;
      if (bbr_bw_probe_cwnd_gain && m_cycleIdx == BBR_BW_PROBE_UP)
        {
          m_cWndGain += bbr_bw_probe_cwnd_gain / 4.0; // units of BBR_UNIT/4
        }
      break;
    case BBR_PROBE_RTT:
      m_pacingGain = 1.0;
      m_cWndGain = 1.0;
      break;
    }
}

// ---- congestion signals (loss/ECN, lower & upper bounds) -------------------------------------

// kernel bbr_reset_congestion_signals().
void
TcpBbr3::bbr_reset_congestion_signals ()
{
  m_lossInRound = false;
  m_ecnInRound = false;
  m_lossInCycle = false;
  m_ecnInCycle = false;
  m_bwLatest = DataRate (0);
  m_inflightLatest = 0;
}

// kernel bbr_exit_loss_recovery().
void
TcpBbr3::bbr_exit_loss_recovery (Ptr<TcpSocketState> tcb)
{
  tcb->m_cWnd = std::max (tcb->m_cWnd.Get (), m_priorCwnd);
  m_tryFastPath = false;
}

// kernel bbr_update_latest_delivery_signals(): track recent rate/volume; detect loss-round start.
// ctx->sample_bw == rs.m_deliveryRate in ns-3.
void
TcpBbr3::bbr_update_latest_delivery_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  m_lossRoundStart = false;
  if (rs.m_delivered <= 0 || rs.m_ackedSacked == 0)
    {
      return;
    }
  m_bwLatest = std::max (m_bwLatest, rs.m_deliveryRate);
  m_inflightLatest = std::max (m_inflightLatest, static_cast<uint32_t> (rs.m_delivered));
  if (rs.m_priorDelivered >= m_lossRoundDelivered)
    {
      m_lossRoundDelivered = m_delivered;
      m_lossRoundStart = true;
    }
}

// kernel bbr_advance_latest_delivery_signals(): once per loss round, reset the latest filters.
void
TcpBbr3::bbr_advance_latest_delivery_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (m_lossRoundStart)
    {
      m_bwLatest = rs.m_deliveryRate;
      m_inflightLatest = static_cast<uint32_t> (rs.m_delivered);
    }
}

// kernel bbr_adapt_lower_bounds(): cut bw_lo/inflight_lo on loss when NOT probing bw.
void
TcpBbr3::bbr_adapt_lower_bounds (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (bbr_is_probing_bandwidth ())
    {
      return;
    }
  // ECN response omitted (stubbed). Loss response:
  if (m_lossInRound)
    {
      bbr_init_lower_bounds (tcb, true);
      bbr_loss_lower_bounds (tcb);
    }
  if (m_bwLo == DataRate (0))
    {
      m_bwLo = DataRate (1);
    }
}

// kernel bbr_update_congestion_signals(): take bw sample, track loss, per-round lower-bound adapt.
void
TcpBbr3::bbr_update_congestion_signals (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (rs.m_delivered <= 0 || rs.m_ackedSacked == 0)
    {
      return;
    }
  DataRate bw = rs.m_deliveryRate;
  if (!rs.m_isAppLimited || bw >= bbr_max_bw ())
    {
      bbr_take_max_bw_sample (bw);
    }
  m_lossInRound |= (rs.m_bytesLoss > 0);
  if (!m_lossRoundStart)
    {
      return; // per-round updates only at loss-round boundaries
    }
  bbr_adapt_lower_bounds (tcb, rs);
  m_lossInRound = false;
  m_ecnInRound = false;
}

// kernel bbr_handle_inflight_too_high(): cut inflight_hi once per probe, restart cycle.
void
TcpBbr3::bbr_handle_inflight_too_high (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs, bool rsmode)
{
  m_prevProbeTooHigh = true;
  m_bwProbeSamples = false; // react once per probe
  if (!rs.m_isAppLimited)
    {
      uint32_t txInFlight = rs.m_priorInFlight; // proxy for rs->tx_in_flight
      uint32_t targetCut = static_cast<uint32_t> (bbr_target_inflight (tcb) * (1.0 - bbr_beta));
      m_inflightHi = std::max (txInFlight, targetCut);
    }
  if (m_state == BBR_PROBE_BW && m_cycleIdx == BBR_BW_PROBE_UP)
    {
      bbr_start_bw_probe_down ();
    }
}

// kernel bbr_adapt_upper_bounds(): react to probe feedback; raise/lower inflight_hi. Returns true
// if it decided a state transition.
bool
TcpBbr3::bbr_adapt_upper_bounds (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (m_ackPhase == BBR_ACKS_PROBE_STARTING && m_roundStart)
    {
      m_ackPhase = BBR_ACKS_PROBE_FEEDBACK;
    }
  if (m_ackPhase == BBR_ACKS_PROBE_STOPPING && m_roundStart)
    {
      m_bwProbeSamples = false;
      m_ackPhase = BBR_ACKS_INIT;
      if (m_state == BBR_PROBE_BW && !rs.m_isAppLimited)
        {
          bbr_advance_max_bw_filter ();
        }
      if (m_state == BBR_PROBE_BW && m_stoppedRiskyProbe && !m_prevProbeTooHigh)
        {
          bbr_start_bw_probe_refill (tcb, 0);
          return true;
        }
    }
  if (bbr_is_inflight_too_high (tcb, rs))
    {
      if (m_bwProbeSamples)
        {
          bbr_handle_inflight_too_high (tcb, rs, true);
        }
    }
  else
    {
      if (m_inflightHi == UINT32_MAX)
        {
          return false;
        }
      uint32_t txInFlight = rs.m_priorInFlight; // proxy for rs->tx_in_flight
      if (txInFlight > m_inflightHi)
        {
          m_inflightHi = txInFlight;
        }
      if (m_state == BBR_PROBE_BW && m_cycleIdx == BBR_BW_PROBE_UP)
        {
          bbr_probe_inflight_hi_upward (tcb, rs);
        }
    }
  return false;
}

// ---- min-rtt / PROBE_RTT ---------------------------------------------------------------------

// kernel bbr_check_probe_rtt_done().
void
TcpBbr3::bbr_check_probe_rtt_done (Ptr<TcpSocketState> tcb)
{
  if (!(m_probeRttDoneStamp != Seconds (0) && Simulator::Now () > m_probeRttDoneStamp))
    {
      return;
    }
  m_probeRttMinStamp = Simulator::Now (); // schedule next PROBE_RTT
  tcb->m_cWnd = std::max (tcb->m_cWnd.Get (), m_priorCwnd);
  bbr_exit_probe_rtt (tcb);
}

// kernel bbr_exit_probe_rtt().
void
TcpBbr3::bbr_exit_probe_rtt (Ptr<TcpSocketState> tcb)
{
  bbr_reset_lower_bounds ();
  if (bbr_full_bw_reached ())
    {
      m_state = BBR_PROBE_BW;
      bbr_start_bw_probe_down ();
      bbr_start_bw_probe_cruise ();
    }
  else
    {
      m_state = BBR_STARTUP;
    }
}

// kernel bbr_update_min_rtt(): two-level filter (probe_rtt_min over 5s, min_rtt over 10s), and
// PROBE_RTT entry/maintenance. is_ack_delayed is not available in ns-3 -> treated as false.
void
TcpBbr3::bbr_update_min_rtt (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  bool probeRttExpired =
      Simulator::Now () > (m_probeRttMinStamp + MilliSeconds (bbr_probe_rtt_win_ms));
  Time lastRtt = tcb->m_lastRtt;
  if (lastRtt >= Seconds (0) && (lastRtt < m_probeRttMin || probeRttExpired))
    {
      m_probeRttMin = lastRtt;
      m_probeRttMinStamp = Simulator::Now ();
    }
  bool minRttExpired =
      Simulator::Now () > (m_minRttStamp + Seconds (bbr_min_rtt_win_sec));
  if (m_probeRttMin <= m_minRtt || minRttExpired)
    {
      m_minRtt = m_probeRttMin;
      m_minRttStamp = m_probeRttMinStamp;
    }

  if (bbr_probe_rtt_mode_ms > 0 && probeRttExpired && !m_idleRestart && m_state != BBR_PROBE_RTT)
    {
      m_state = BBR_PROBE_RTT;
      bbr_save_cwnd (tcb);
      m_probeRttDoneStamp = Seconds (0);
      m_ackPhase = BBR_ACKS_PROBE_STOPPING;
      m_nextRttDelivered = m_delivered;
    }

  if (m_state == BBR_PROBE_RTT)
    {
      if (m_probeRttDoneStamp == Seconds (0)
          && tcb->m_bytesInFlight.Get () <= bbr_probe_rtt_cwnd (tcb))
        {
          m_probeRttDoneStamp = Simulator::Now () + MilliSeconds (bbr_probe_rtt_mode_ms);
          m_probeRttRoundDone = false;
          m_nextRttDelivered = m_delivered;
        }
      else if (m_probeRttDoneStamp != Seconds (0))
        {
          if (m_roundStart)
            {
              m_probeRttRoundDone = true;
            }
          if (m_probeRttRoundDone)
            {
              bbr_check_probe_rtt_done (tcb);
            }
        }
    }
  if (rs.m_delivered > 0)
    {
      m_idleRestart = false;
    }
}

// ---- ack aggregation -------------------------------------------------------------------------

// kernel bbr_update_ack_aggregation(): windowed max of excess acked beyond bw*epoch. Bytes.
void
TcpBbr3::bbr_update_ack_aggregation (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (bbr_extra_acked_gain == 0.0 || rs.m_ackedSacked == 0 || rs.m_delivered < 0)
    {
      return;
    }
  uint32_t winRttsThresh = bbr_extra_acked_win_rtts;
  if (m_roundStart)
    {
      m_extraAckedWinRtts = std::min<uint32_t> (0x1F, m_extraAckedWinRtts + 1);
      if (!bbr_full_bw_reached ())
        {
          winRttsThresh = 1;
        }
      if (m_extraAckedWinRtts >= winRttsThresh)
        {
          m_extraAckedWinRtts = 0;
          m_extraAckedWinIdx = m_extraAckedWinIdx ? 0 : 1;
          m_extraAcked[m_extraAckedWinIdx] = 0;
        }
    }
  double epochS = (Simulator::Now () - m_ackEpochStamp).GetSeconds ();
  uint32_t expectedAcked = static_cast<uint32_t> ((bbr_bw ().GetBitRate () / 8.0) * epochS);
  if (m_ackEpochAcked <= expectedAcked
      || (m_ackEpochAcked + rs.m_ackedSacked >= bbr_ack_epoch_acked_reset_thresh))
    {
      m_ackEpochAcked = 0;
      m_ackEpochStamp = Simulator::Now ();
      expectedAcked = 0;
    }
  m_ackEpochAcked = m_ackEpochAcked + rs.m_ackedSacked;
  uint32_t extraAcked = m_ackEpochAcked - expectedAcked;
  extraAcked = std::min (extraAcked, tcb->m_cWnd.Get ());
  if (extraAcked > m_extraAcked[m_extraAckedWinIdx])
    {
      m_extraAcked[m_extraAckedWinIdx] = extraAcked;
    }
}

// ---- STARTUP exit / full-bw / drain ----------------------------------------------------------

// kernel bbr_check_loss_too_high_in_startup().
void
TcpBbr3::bbr_check_loss_too_high_in_startup (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  if (bbr_full_bw_reached ())
    {
      return;
    }
  if (rs.m_bytesLoss > 0 && m_lossEventsInRound < 0xf)
    {
      m_lossEventsInRound++;
    }
  if (bbr_full_loss_cnt && m_lossRoundStart
      && tcb->m_congState == TcpSocketState::CA_RECOVERY
      && m_lossEventsInRound >= bbr_full_loss_cnt
      && bbr_is_inflight_too_high (tcb, rs))
    {
      bbr_handle_queue_too_high_in_startup (tcb);
      return;
    }
  if (m_lossRoundStart)
    {
      m_lossEventsInRound = 0;
    }
}

// kernel bbr_check_full_bw_reached(): bw plateau over full_bw_cnt non-app-limited rounds.
void
TcpBbr3::bbr_check_full_bw_reached (const TcpRateOps::TcpRateSample &rs)
{
  if (m_fullBwNow || rs.m_isAppLimited)
    {
      return;
    }
  DataRate bwThresh = DataRate (static_cast<uint64_t> (m_fullBw.GetBitRate () * bbr_full_bw_thresh));
  if (rs.m_deliveryRate >= bwThresh)
    {
      bbr_reset_full_bw ();
      m_fullBw = rs.m_deliveryRate;
      return;
    }
  if (!m_roundStart)
    {
      return;
    }
  ++m_fullBwCnt;
  m_fullBwNow = m_fullBwCnt >= bbr_full_bw_cnt;
  m_fullBwReached = m_fullBwReached || m_fullBwNow;
}

// kernel bbr_check_drain(): STARTUP->DRAIN->PROBE_BW.
void
TcpBbr3::bbr_check_drain (Ptr<TcpSocketState> tcb)
{
  if (m_state == BBR_STARTUP && bbr_full_bw_reached ())
    {
      m_state = BBR_DRAIN;
      tcb->m_ssThresh = bbr_inflight (tcb, bbr_max_bw (), 1.0); // export only; BBR ignores ssthresh
      bbr_reset_congestion_signals ();
    }
  if (m_state == BBR_DRAIN
      && tcb->m_bytesInFlight.Get () <= bbr_inflight (tcb, bbr_max_bw (), 1.0))
    {
      m_state = BBR_PROBE_BW;
      bbr_start_bw_probe_down ();
    }
}

// ---- PROBE_BW cycle state machine ------------------------------------------------------------

// kernel bbr_update_cycle_phase(). bbr_packets_in_net_at_edt() has no ns-3 analogue (no EDT
// pacing at this layer) -> inflight == rs.m_priorInFlight.
void
TcpBbr3::bbr_update_cycle_phase (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  bool isBwProbeDone = false;
  if (!bbr_full_bw_reached ())
    {
      return;
    }
  if (bbr_adapt_upper_bounds (tcb, rs))
    {
      return; // already decided a state transition
    }
  if (m_state != BBR_PROBE_BW)
    {
      return;
    }
  uint32_t inflight = rs.m_priorInFlight;
  DataRate bw = bbr_max_bw ();
  switch (m_cycleIdx)
    {
    case BBR_BW_PROBE_CRUISE:
      if (bbr_check_time_to_probe_bw (tcb))
        {
          return;
        }
      break;
    case BBR_BW_PROBE_REFILL:
      if (m_roundStart)
        {
          m_bwProbeSamples = true;
          bbr_start_bw_probe_up (tcb, rs);
        }
      break;
    case BBR_BW_PROBE_UP:
      if (m_prevProbeTooHigh && inflight >= m_inflightHi)
        {
          m_stoppedRiskyProbe = true;
          isBwProbeDone = true;
        }
      else
        {
          // kernel: is_cwnd_limited && cwnd >= inflight_hi (is_cwnd_limited approximated true).
          if (tcb->m_cWnd.Get () >= m_inflightHi)
            {
              bbr_reset_full_bw ();
              m_fullBw = rs.m_deliveryRate;
            }
          else if (m_fullBwNow)
            {
              isBwProbeDone = true;
            }
        }
      if (isBwProbeDone)
        {
          m_prevProbeTooHigh = false;
          bbr_start_bw_probe_down ();
        }
      break;
    case BBR_BW_PROBE_DOWN:
      if (bbr_check_time_to_probe_bw (tcb))
        {
          return;
        }
      if (bbr_check_time_to_cruise (tcb, bw))
        {
          bbr_start_bw_probe_cruise ();
        }
      break;
    }
}

// ---- cwnd ------------------------------------------------------------------------------------

// kernel bbr_set_cwnd(): grow toward target (bdp+aggregation, quantized). NOTE: BBRv3 has NO
// per-packet loss decrement here (unlike ns-3 BBRv1) — loss response is via inflight_lo/hi bounds
// applied in bbr_bound_cwnd_for_inflight_model(). This is the key graceful-degradation property.
void
TcpBbr3::bbr_set_cwnd (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  uint32_t acked = rs.m_ackedSacked;
  uint32_t cwnd = tcb->m_cWnd.Get ();
  uint32_t target = 0;
  if (acked == 0)
    {
      goto done; // no packet fully ACKed; just apply caps
    }
  target = bbr_bdp (tcb, bbr_bw (), m_cWndGain);
  target += bbr_ack_aggregation_cwnd ();
  target = bbr_quantization_budget (tcb, target);
  m_tryFastPath = false;
  if (bbr_full_bw_reached ())
    {
      cwnd += acked;
      if (cwnd >= target)
        {
          cwnd = target;
          m_tryFastPath = true;
        }
    }
  else if (cwnd < target || cwnd < 2 * m_initCwnd)
    {
      cwnd += acked;
    }
  else
    {
      m_tryFastPath = true;
    }
  cwnd = std::max (cwnd, bbr_cwnd_min_target * m_segmentSize);
done:
  tcb->m_cWnd = cwnd;
  m_targetCwnd = target;
  if (m_state == BBR_PROBE_RTT)
    {
      tcb->m_cWnd = std::min (tcb->m_cWnd.Get (), bbr_probe_rtt_cwnd (tcb));
    }
}

// ---- model + main ----------------------------------------------------------------------------

// kernel bbr_update_model().
void
TcpBbr3::bbr_update_model (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  bbr_update_congestion_signals (tcb, rs);
  bbr_update_ack_aggregation (tcb, rs);
  bbr_check_loss_too_high_in_startup (tcb, rs);
  bbr_check_full_bw_reached (rs);
  bbr_check_drain (tcb);
  bbr_update_cycle_phase (tcb, rs);
  bbr_update_min_rtt (tcb, rs);
}

void
TcpBbr3::InitRoundCounting ()
{
  m_nextRttDelivered = 0;
  m_roundStart = false;
}

void
TcpBbr3::InitFullPipe ()
{
  m_fullBwReached = false;
  m_fullBw = DataRate (0);
  m_fullBwCnt = 0;
  m_fullBwNow = false;
}

// kernel bbr_init(): first-ACK initialization. Called on the first CA_OPEN.
void
TcpBbr3::bbr_main (Ptr<TcpSocketState> tcb, const TcpRateOps::TcpRateSample &rs)
{
  // The fast path (bbr_run_fast_path) is a CPU optimization only; we always take the full path,
  // which is behaviourally identical.
  bbr_update_round_start (tcb, rs);
  if (m_roundStart)
    {
      m_roundsSinceProbe = std::min<uint32_t> (m_roundsSinceProbe + 1, 0xFF);
      // bbr_update_ecn_alpha() skipped (ECN stubbed)
    }
  // bbr_plb() skipped; ecn_in_round skipped (ECN stubbed)
  bbr_update_latest_delivery_signals (tcb, rs);
  bbr_update_model (tcb, rs);
  bbr_update_gains ();
  bbr_set_pacing_rate (tcb, m_pacingGain);
  bbr_set_send_quantum (tcb);
  bbr_set_cwnd (tcb, rs);
  bbr_bound_cwnd_for_inflight_model (tcb);

  bbr_advance_latest_delivery_signals (tcb, rs);
  m_prevCaState = tcb->m_congState;
  m_lossInCycle |= (rs.m_bytesLoss > 0);
}

void
TcpBbr3::CongControl (Ptr<TcpSocketState> tcb,
                      const TcpRateOps::TcpRateConnection &rc,
                      const TcpRateOps::TcpRateSample &rs)
{
  NS_LOG_FUNCTION (this << tcb);
  m_delivered = rc.m_delivered;
  m_segmentSize = tcb->m_segmentSize;
  bbr_main (tcb, rs);
}

// ---- state overrides -------------------------------------------------------------------------

// kernel bbr_init() (first CA_OPEN) + bbr_set_state() (CA_LOSS / recovery exit).
void
TcpBbr3::CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
{
  NS_LOG_FUNCTION (this << tcb << newState);
  if (newState == TcpSocketState::CA_OPEN && !m_initialized)
    {
      m_segmentSize = tcb->m_segmentSize;
      m_initialized = true;
      m_initCwnd = tcb->m_initialCWnd * tcb->m_segmentSize;
      m_priorCwnd = tcb->m_cWnd;
      tcb->m_ssThresh = tcb->m_initialSsThresh;
      m_nextRttDelivered = m_delivered;
      m_prevCaState = TcpSocketState::CA_OPEN;

      m_probeRttDoneStamp = Seconds (0);
      m_probeRttRoundDone = false;
      m_minRtt = (tcb->m_lastRtt.Get () != Time::Max ()) ? tcb->m_lastRtt.Get () : Time::Max ();
      m_minRttStamp = Simulator::Now ();
      m_probeRttMin = m_minRtt;
      m_probeRttMinStamp = Simulator::Now ();

      m_hasSeenRtt = false;
      m_roundStart = false;
      m_idleRestart = false;
      m_cycleStamp = Seconds (0);
      m_cycleIdx = 0;
      m_sendQuantum = m_segmentSize;

      SetBbrState (BBR_STARTUP);
      m_pacingGain = bbr_startup_pacing_gain;
      m_cWndGain = bbr_startup_cwnd_gain;

      InitRoundCounting ();
      InitFullPipe ();
      InitPacingRate (tcb);

      m_ackEpochStamp = Simulator::Now ();
      m_ackEpochAcked = 0;
      m_extraAckedWinRtts = 0;
      m_extraAckedWinIdx = 0;
      m_extraAcked[0] = 0;
      m_extraAcked[1] = 0;

      m_lossRoundDelivered = m_delivered + 1;
      m_lossRoundStart = false;
      m_lossEventsInRound = 0;
      bbr_reset_congestion_signals ();
      m_bwLo = DataRate (std::numeric_limits<uint64_t>::max ());
      m_bwHi[0] = DataRate (0);
      m_bwHi[1] = DataRate (0);
      m_inflightLo = UINT32_MAX;
      m_inflightHi = UINT32_MAX;
      bbr_reset_full_bw ();
      m_bwProbeUpCnt = UINT32_MAX;
      m_bwProbeUpAcks = 0;
      m_bwProbeUpRounds = 0;
      m_probeWait = Seconds (0);
      m_stoppedRiskyProbe = false;
      m_ackPhase = BBR_ACKS_INIT;
      m_roundsSinceProbe = 0;
      m_bwProbeSamples = false;
      m_prevProbeTooHigh = false;
      m_ecnEligible = false;
    }
  else if (newState == TcpSocketState::CA_LOSS)
    {
      // kernel bbr_set_state(TCP_CA_Loss): re-learn path, seed inflight_lo from pre-RTO cwnd.
      m_prevCaState = TcpSocketState::CA_LOSS;
      m_lossInRound = true;
      m_lossInCycle = true;
      bbr_reset_full_bw ();
      if (!bbr_is_probing_bandwidth () && m_inflightLo == UINT32_MAX)
        {
          m_inflightLo = std::max (tcb->m_cWnd.Get (), m_priorCwnd);
        }
    }
  else if ((m_prevCaState == TcpSocketState::CA_LOSS || m_prevCaState == TcpSocketState::CA_RECOVERY)
           && newState != TcpSocketState::CA_LOSS && newState != TcpSocketState::CA_RECOVERY)
    {
      bbr_exit_loss_recovery (tcb);
    }
}

// kernel bbr_cwnd_event(): handle restart-from-idle (TX_START).
void
TcpBbr3::CwndEvent (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  if (event == TcpSocketState::CA_EVENT_TX_START)
    {
      m_idleRestart = true;
      m_ackEpochStamp = Simulator::Now ();
      m_ackEpochAcked = 0;
      if (m_state == BBR_PROBE_BW)
        {
          bbr_set_pacing_rate (tcb, 1.0); // pace at estimated bw (gain 1)
        }
      else if (m_state == BBR_PROBE_RTT)
        {
          bbr_check_probe_rtt_done (tcb);
        }
    }
}

// kernel bbr_ssthresh(): entering recovery — save cwnd + undo state.
uint32_t
TcpBbr3::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  bbr_save_cwnd (tcb);
  m_undoBwLo = m_bwLo;
  m_undoInflightLo = m_inflightLo;
  m_undoInflightHi = m_inflightHi;
  return tcb->m_ssThresh;
}

} // namespace ns3
