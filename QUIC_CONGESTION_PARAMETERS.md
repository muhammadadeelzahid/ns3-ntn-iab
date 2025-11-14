# QUIC Parameters Affecting Congestion and Acknowledgment Gaps

This document lists QUIC parameters (implemented in `src/quic`) that may cause congestion issues and affect acknowledgment behavior, particularly when clients send acknowledgments with gaps (missing acknowledgments).

## Acknowledgment-Related Parameters

### 1. **kDelayedAckTimeout**
- **Location**: `quic-socket-base.cc` (line 192-195, 381-384)
- **Default Value**: 25 milliseconds
- **Description**: The length of the peer's delayed ACK timer. Controls how long the receiver waits before sending an acknowledgment.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Sends ACKs more frequently, reducing acknowledgment delays and potentially reducing gaps
  - **Increasing** this value: Delays ACKs, which may cause more gaps and delayed loss detection
- **Recommendation**: Decrease if experiencing acknowledgment delays

### 2. **kMaxPacketsReceivedBeforeAckSend**
- **Location**: `quic-socket-base.cc` (line 390-394)
- **Default Value**: 20 packets
- **Description**: Maximum number of packets that can be received without sending an ACK. When this threshold is reached, an ACK is immediately sent.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Forces more frequent ACKs, reducing gaps and improving loss detection
  - **Increasing** this value: Allows more packets before ACK, potentially causing larger gaps
- **Recommendation**: Decrease if seeing large gaps in acknowledgments

### 3. **AckDelayExponent**
- **Location**: `quic-socket-base.cc` (line 159-162), `quic-transport-parameters.cc`
- **Default Value**: 3
- **Description**: Exponent used to encode ACK delay in ACK frames. Higher values allow larger encoded delays.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Limits maximum encodable ACK delay, potentially reducing gaps
  - **Increasing** this value: Allows larger ACK delays to be encoded
- **Recommendation**: Consider decreasing if ACK delays are contributing to gaps

### 4. **MaxTrackedGaps**
- **Location**: `quic-socket-base.cc` (line 128-131)
- **Default Value**: 20 gaps
- **Description**: Maximum number of gaps that can be tracked and reported in a single ACK frame. Older gaps beyond this limit are not included in ACK frames.
- **Impact on Congestion/Gaps**:
  - **Increasing** this value: Allows more gaps to be reported in ACK frames, improving loss detection for scenarios with many missing packets
  - **Decreasing** this value: Limits gap reporting, potentially missing some lost packets
- **Recommendation**: Increase if experiencing many gaps and some are not being reported

## Loss Detection Parameters

### 5. **kReorderingThreshold**
- **Location**: `quic-socket-base.cc` (line 172-175, 357-361)
- **Default Value**: 3 packets
- **Description**: Maximum reordering in packet number space before FACK-style loss detection considers a packet lost. Packets with gaps larger than this threshold are marked as lost.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: More aggressive loss detection, marks packets as lost sooner (may cause false positives)
  - **Increasing** this value: More tolerant of reordering, delays loss detection (may cause congestion)
- **Recommendation**: Decrease if packets are being lost but not detected quickly enough

### 6. **kTimeReorderingFraction**
- **Location**: `quic-socket-base.cc` (line 176-179, 362-366)
- **Default Value**: 9/8 (1.125)
- **Description**: Maximum reordering in time space (as fraction of RTT) before time-based loss detection considers a packet lost. Only used if `kUsingTimeLossDetection` is enabled.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: More aggressive time-based loss detection
  - **Increasing** this value: More tolerant of delays before marking packets as lost
- **Recommendation**: Decrease if time-based loss detection is enabled and losses are detected too late

### 7. **kUsingTimeLossDetection**
- **Location**: `quic-socket-base.cc` (line 180-183, 367-370)
- **Default Value**: false
- **Description**: Whether time-based loss detection is enabled. If false, uses FACK-style (packet number-based) loss detection only.
- **Impact on Congestion/Gaps**:
  - **Enabling** (set to true): Adds time-based loss detection, which can detect losses even when packet numbers don't show gaps
  - **Disabling** (set to false): Relies only on packet number gaps for loss detection
- **Recommendation**: Enable if experiencing losses that aren't detected by packet number gaps alone

### 8. **kMinTLPTimeout**
- **Location**: `quic-socket-base.cc` (line 184-187, 371-375)
- **Default Value**: 10 milliseconds
- **Description**: Minimum time before a Tail Loss Probe (TLP) can be sent. TLPs are used to detect tail losses.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Sends TLPs sooner, detecting tail losses faster
  - **Increasing** this value: Delays TLP transmission, potentially missing tail losses
- **Recommendation**: Decrease if tail losses are not being detected quickly

### 9. **kMinRTOTimeout**
- **Location**: `quic-socket-base.cc` (line 188-191, 376-380)
- **Default Value**: 200 milliseconds
- **Description**: Minimum Retransmission Timeout (RTO) value. The RTO is used when loss detection fails.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Triggers retransmissions sooner, but may cause spurious retransmissions
  - **Increasing** this value: Delays retransmissions, potentially causing congestion
- **Recommendation**: Adjust based on network RTT characteristics

### 10. **kMaxTLPs**
- **Location**: `quic-socket-base.cc` (line 167-171, 352-356)
- **Default Value**: 2 probes
- **Description**: Maximum number of Tail Loss Probes before an RTO fires.
- **Impact on Congestion/Gaps**:
  - **Increasing** this value: Allows more TLPs before RTO, potentially detecting tail losses without full RTO
  - **Decreasing** this value: Triggers RTO sooner, may cause unnecessary retransmissions
- **Recommendation**: Increase if tail losses are common

### 11. **kDefaultInitialRtt**
- **Location**: `quic-socket-base.cc` (line 196-199, 385-389)
- **Default Value**: 100 milliseconds (constructor: 333ms)
- **Description**: Default RTT used before an RTT sample is taken. Used for initial loss detection and timeout calculations.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: More aggressive initial loss detection, but may cause false positives
  - **Increasing** this value: More conservative initial behavior, may delay loss detection
- **Recommendation**: Set based on expected network RTT

## Congestion Control Parameters

### 12. **InitialSlowStartThreshold**
- **Location**: `quic-socket-base.cc` (line 200-205)
- **Default Value**: INT32_MAX (effectively unlimited)
- **Description**: Initial slow start threshold in bytes. Controls when the connection transitions from slow start to congestion avoidance.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Enters congestion avoidance sooner, more conservative sending
  - **Increasing** this value: Stays in slow start longer, more aggressive sending (may cause congestion)
- **Recommendation**: Decrease if experiencing congestion during slow start

### 13. **InitialPacketSize** / **MaxPacketSize**
- **Location**: `quic-socket-base.cc` (line 206-212, 136-140)
- **Default Value**: 1200 bytes (initial), 1460 bytes (max)
- **Description**: Initial and maximum packet sizes. Larger packets can cause more congestion if lost.
- **Impact on Congestion/Gaps**:
  - **Decreasing** packet size: Reduces impact of packet loss, but increases overhead
  - **Increasing** packet size: More efficient, but larger losses when packets are dropped
- **Recommendation**: Decrease if experiencing high packet loss rates

### 14. **SocketSndBufSize** (Send Buffer Size)
- **Location**: `quic-socket-base.cc` (line 141-145)
- **Default Value**: 131072 bytes (128 KB)
- **Description**: Maximum transmit buffer size. Limits how much data can be queued for transmission.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Reduces queued data, may help prevent congestion
  - **Increasing** this value: Allows more data to be queued, may cause bufferbloat
- **Recommendation**: Decrease if experiencing buffer-related congestion

### 15. **SocketRcvBufSize** (Receive Buffer Size)
- **Location**: `quic-socket-base.cc` (line 146-150)
- **Default Value**: 131072 bytes (128 KB)
- **Description**: Maximum receive buffer size. Limits how much data can be buffered before being delivered to application.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Reduces receive buffer, may cause flow control issues
  - **Increasing** this value: Allows more buffering, may help with reordering but can cause bufferbloat
- **Recommendation**: Adjust based on application requirements and network conditions

### 16. **m_kMinimumWindow** (Minimum Congestion Window)
- **Location**: `quic-socket-base.h` (line 105), `quic-socket-base.cc` (line 416-417)
- **Default Value**: 2 * segmentSize
- **Description**: Minimum congestion window size. The congestion window cannot go below this value.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Allows more aggressive reduction during congestion, but may cause underutilization
  - **Increasing** this value: Prevents window from getting too small, may help maintain throughput
- **Recommendation**: Increase if connection is too conservative after losses

### 17. **m_kLossReductionFactor** (Loss Reduction Factor)
- **Location**: `quic-socket-base.h` (line 106), `quic-socket-base.cc` (line 418)
- **Default Value**: 0.5 (halves the window)
- **Description**: Multiplier applied to congestion window when a loss is detected. Value of 0.5 means window is halved.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: More aggressive reduction on loss (e.g., 0.3 = reduce to 30%), more conservative
  - **Increasing** this value: Less aggressive reduction (e.g., 0.7 = reduce to 70%), more aggressive
- **Recommendation**: Decrease if experiencing persistent congestion, increase if being too conservative

### 18. **m_initialCWnd** (Initial Congestion Window)
- **Location**: `quic-socket-base.cc` (line 436, 483)
- **Default Value**: 10 * segmentSize
- **Description**: Initial congestion window size when connection starts.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: More conservative start, reduces initial burst
  - **Increasing** this value: More aggressive start, may cause initial congestion
- **Recommendation**: Decrease if experiencing congestion at connection start

## Flow Control Parameters

### 19. **MaxData** (Connection-Level Flow Control)
- **Location**: `quic-socket-base.cc` (line 114-118)
- **Default Value**: 4294967295 (effectively unlimited)
- **Description**: Maximum amount of data that can be sent on the connection. Connection-level flow control limit.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Limits connection throughput, may cause flow control blocking (not congestion-related, but can appear as congestion)
  - **Increasing** this value: Allows more data in flight, may contribute to congestion if set too high
- **Recommendation**: Set based on application needs and network capacity. Too low may cause unnecessary blocking.

### 20. **MaxStreamData** (Stream-Level Flow Control)
- **Location**: `quic-socket-base.cc` (line 109-113)
- **Default Value**: 4294967295 (effectively unlimited)
- **Description**: Maximum amount of data that can be sent on any newly created stream. Stream-level flow control limit.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Limits per-stream throughput, may cause stream-level blocking
  - **Increasing** this value: Allows more data per stream, may contribute to congestion
- **Recommendation**: Set based on stream requirements. Too low may cause stream blocking.

### 21. **MaxDataInterval**
- **Location**: `quic-stream-base.cc` (line 68-72), `quic-socket-base.cc` (line 602)
- **Default Value**: 15000 (stream), 10 (socket)
- **Description**: Interval between MAX_DATA frames sent in ACK frames. Controls how frequently flow control windows are updated.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Sends MAX_DATA updates more frequently, improving flow control responsiveness
  - **Increasing** this value: Sends MAX_DATA updates less frequently, may delay window updates
- **Recommendation**: Decrease if experiencing flow control blocking issues

### 22. **StreamSndBufSize** (Stream Send Buffer Size)
- **Location**: `quic-stream-base.cc` (line 58-62)
- **Default Value**: 131072 bytes (128 KB)
- **Description**: Maximum transmit buffer size per stream. Limits how much data can be queued per stream.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Reduces per-stream queuing, may help prevent congestion
  - **Increasing** this value: Allows more per-stream queuing, may cause bufferbloat
- **Recommendation**: Decrease if experiencing stream-level buffer issues

### 23. **StreamRcvBufSize** (Stream Receive Buffer Size)
- **Location**: `quic-stream-base.cc` (line 63-67)
- **Default Value**: 131072 bytes (128 KB)
- **Description**: Maximum receive buffer size per stream. Limits how much data can be buffered per stream before delivery.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Reduces per-stream buffering, may cause flow control issues
  - **Increasing** this value: Allows more per-stream buffering, may help with reordering
- **Recommendation**: Adjust based on stream requirements

## Connection Management Parameters

### 24. **IdleTimeout**
- **Location**: `quic-socket-base.cc` (line 104-108)
- **Default Value**: 300 seconds
- **Description**: Idle timeout value after which the socket is closed if no packets are sent or received.
- **Impact on Congestion/Gaps**:
  - **Decreasing** this value: Closes idle connections sooner, frees resources
  - **Increasing** this value: Keeps connections alive longer, may hold resources unnecessarily
- **Recommendation**: Adjust based on application requirements. Not directly related to congestion but affects connection lifecycle.

## Summary of Recommendations for Acknowledgment Gaps

If you're experiencing **acknowledgment gaps** (missing acknowledgments), consider adjusting these parameters in priority order:

1. **kMaxPacketsReceivedBeforeAckSend**: **Decrease** (e.g., from 20 to 10) - Forces more frequent ACKs
2. **kDelayedAckTimeout**: **Decrease** (e.g., from 25ms to 10ms) - Reduces ACK delay
3. **MaxTrackedGaps**: **Increase** (e.g., from 20 to 50) - Allows more gaps to be reported
4. **kReorderingThreshold**: **Decrease** (e.g., from 3 to 2) - More aggressive loss detection
5. **kUsingTimeLossDetection**: **Enable** (set to true) - Adds time-based loss detection
6. **kMinTLPTimeout**: **Decrease** (e.g., from 10ms to 5ms) - Faster tail loss detection

## Summary of Recommendations for Congestion

If you're experiencing **congestion issues**, consider adjusting these parameters:

1. **InitialSlowStartThreshold**: **Decrease** - Enter congestion avoidance sooner
2. **SocketSndBufSize**: **Decrease** - Reduce send buffer to prevent bufferbloat
3. **m_kLossReductionFactor**: **Decrease** (e.g., from 0.5 to 0.3) - More aggressive reduction on loss
4. **m_initialCWnd**: **Decrease** - More conservative initial window
5. **MaxPacketSize**: **Decrease** - Reduce impact of packet losses
6. **kMinRTOTimeout**: **Adjust** based on network RTT characteristics

## Additional Notes

### Pacing Parameters (Inherited from TcpSocketState)
QUIC inherits pacing functionality from `TcpSocketState`:
- **EnablePacing**: Enable/disable packet pacing (default: inherited from TCP)
- **MaxPacingRate**: Maximum pacing rate for packet transmission
- These are not QUIC-specific but affect QUIC behavior when pacing is enabled

### RTT Calculation Parameters
RTT calculation uses hardcoded RFC 6298 values (not configurable):
- Smoothed RTT: `7/8 * smoothedRtt + 1/8 * latestRtt`
- RTT Variance: `3/4 * rttVar + 1/4 * rttVarSample`
- These are standard and should not be modified

### m_maxAckDelay (Internal Variable)
- **Location**: `quic-socket-base.h` (line 98), `quic-socket-base.cc` (line 414)
- **Default Value**: 25 milliseconds (0.025 seconds)
- **Description**: Maximum ACK delay observed in incoming ACK frames. Used internally for RTT calculations and timeout adjustments.
- **Note**: This is a runtime variable, not a configurable parameter. It's automatically updated based on received ACK delays.

## Notes

- All parameters are configurable via ns-3 attributes
- Changes should be tested incrementally
- Network conditions (RTT, loss rate, bandwidth) should be considered when tuning
- Some parameters interact with each other, so changes may have combined effects
- Flow control parameters (MaxData, MaxStreamData) can cause blocking that appears similar to congestion but is actually flow control limiting
- The QUIC implementation inherits some TCP parameters (like pacing) from `TcpSocketState`

