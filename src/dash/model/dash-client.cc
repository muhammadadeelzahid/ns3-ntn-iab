/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 TEI of Western Macedonia, Greece
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Dimitrios J. Vergados <djvergad@gmail.com>
 */

#include "dash-client.h"

#include "http-header.h"

#include <ns3/inet-socket-address.h>
#include <ns3/inet6-socket-address.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/double.h>
#include <ns3/tcp-socket-factory.h>
#include <ns3/uinteger.h>
#include <sstream>

NS_LOG_COMPONENT_DEFINE("DashClient");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(DashClient);

int DashClient::m_countObjs = 0;

TypeId
DashClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::DashClient")
                            .SetParent<Application>()
                            .AddConstructor<DashClient>()
                            .AddAttribute("VideoId",
                                          "The Id of the video that is played.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&DashClient::m_videoId),
                                          MakeUintegerChecker<uint32_t>(1))
                            .AddAttribute("Remote",
                                          "The address of the destination",
                                          AddressValue(),
                                          MakeAddressAccessor(&DashClient::m_peer),
                                          MakeAddressChecker())
                            .AddAttribute("Protocol",
                                          "The type of TCP protocol to use.",
                                          TypeIdValue(TcpSocketFactory::GetTypeId()),
                                          MakeTypeIdAccessor(&DashClient::m_tid),
                                          MakeTypeIdChecker())
                            .AddAttribute("TargetDt",
                                          "The target buffering time",
                                          TimeValue(Time("35s")),
                                          MakeTimeAccessor(&DashClient::m_target_dt),
                                          MakeTimeChecker())
                            .AddAttribute("window",
                                          "The window for measuring the average throughput (Time)",
                                          TimeValue(Time("10s")),
                                          MakeTimeAccessor(&DashClient::m_window),
                                          MakeTimeChecker())
                            .AddAttribute("bufferSpace",
                                          "The buffer space in bytes",
                                          UintegerValue(30000000),
                                          MakeUintegerAccessor(&DashClient::m_bufferSpace),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("MaxVideoDuration",
                                          "Maximum video duration - stops requesting segments after this time",
                                          TimeValue(Seconds(0)),  // 0 = unlimited
                                          MakeTimeAccessor(&DashClient::m_maxVideoDuration),
                                          MakeTimeChecker())
                            .AddAttribute("MaxBufferS",
                                          "Hard playback-buffer capacity in seconds (models the dash.js "
                                          "BufferController; 0 = unlimited). Decoupled from the ABR's "
                                          "internal buffer target so the actual buffer can be swept "
                                          "independently of BOLA's bufferTime.",
                                          DoubleValue(0.0),  // 0 = unlimited
                                          MakeDoubleAccessor(&DashClient::m_maxBufferS),
                                          MakeDoubleChecker<double>())
                            .AddTraceSource("Tx",
                                            "A new packet is created and is sent",
                                            MakeTraceSourceAccessor(&DashClient::m_txTrace),
                                            "ns3::Packet::TracedCallback")
                            .AddTraceSource("Rx",
                                            "A video segment (MPEG frame) has been received",
                                            MakeTraceSourceAccessor(&DashClient::m_rxTrace),
                                            "ns3::Packet::TracedCallback");
    return tid;
}

DashClient::DashClient()
    : m_bufferSpace(0),
      m_player(this->GetObject<DashClient>(), m_bufferSpace),
      m_rateChanges(0),
      m_target_dt("35s"),
      m_maxBufferS(0.0),
      m_bitrateEstimate(0.0),
      m_segmentId(0),
      m_socket(0),
      m_connected(false),
      m_totBytes(0),
      m_started(Seconds(0)),
      m_sumDt(Seconds(0)),
      m_lastDt(Seconds(-1)),
      m_id(m_countObjs++),
      m_requestTime("0s"),
      m_segment_bytes(0),
      m_bitRate(45000),
      m_window(Seconds(10)),
      m_segmentFetchTime(Seconds(0)),
      m_keepAliveTimer(),
      m_connectWatchdogTimer(),
      m_segmentWatchdogTimer(),
      m_periodicBufferCheckTimer(),
      m_maxVideoDuration(Seconds(0)),
      m_maxSegments(0)
{
    NS_LOG_FUNCTION(this);
    m_parser.SetApp(this); // So the parser knows where to send the received messages
}

DashClient::~DashClient()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Socket>
DashClient::GetSocket(void) const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}

void
DashClient::DoDispose(void)
{
    NS_LOG_FUNCTION(this);

    m_socket = 0;
    m_keepAliveTimer.Cancel();
    m_connectWatchdogTimer.Cancel();
    m_segmentWatchdogTimer.Cancel();
    // chain up
    Application::DoDispose();
}

// Application Methods
void
DashClient::StartApplication(void) // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);

    // Create the socket if not already

    NS_LOG_INFO("trying to create connection");
    if (!m_socket)
    {
        NS_LOG_INFO("m_socket is null");

        m_started = Simulator::Now();

        if (m_maxVideoDuration.GetSeconds() > 0)
        {
            double frameDurationSeconds = MPEG_TIME_BETWEEN_FRAMES / 1000.0;  // Convert ms to seconds
            double segmentDurationSeconds = (1.0 * MPEG_FRAMES_PER_SEGMENT * frameDurationSeconds);
            double totalFrames = m_maxVideoDuration.GetSeconds() / frameDurationSeconds;
            
            double segmentsNeeded = m_maxVideoDuration.GetSeconds() / segmentDurationSeconds;
            m_maxSegments = static_cast<uint32_t>(std::ceil(segmentsNeeded));
            
            NS_LOG_INFO("Maximum video duration: " << m_maxVideoDuration.GetSeconds() << "s, segmentsNeeded: " << segmentsNeeded);
            NS_LOG_DEBUG("Frame duration: " << frameDurationSeconds << "s, segment duration: " << segmentDurationSeconds << "s, total frames: " << totalFrames);
        }
        else
        {
            m_maxSegments = 0;  // Unlimited
        }

        m_socket = Socket::CreateSocket(GetNode(), m_tid);

        // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
        if (m_socket->GetSocketType() != Socket::NS3_SOCK_STREAM &&
            m_socket->GetSocketType() != Socket::NS3_SOCK_SEQPACKET)
        {
            NS_FATAL_ERROR("Using HTTP with an incompatible socket type. "
                           "HTTP requires SOCK_STREAM or SOCK_SEQPACKET. "
                           "In other words, use TCP instead of UDP.");
        }

        if (Inet6SocketAddress::IsMatchingType(m_peer))
        {
            m_socket->Bind6();
        }
        else if (InetSocketAddress::IsMatchingType(m_peer))
        {
            m_socket->Bind();
        }

        // Log connection attempt
        std::string peerInfo = "Unknown";
        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            Ipv4Address peerIp = InetSocketAddress::ConvertFrom(m_peer).GetIpv4();
            uint16_t peerPort = InetSocketAddress::ConvertFrom(m_peer).GetPort();
            std::ostringstream oss;
            oss << peerIp << ":" << peerPort;
            peerInfo = oss.str();
        }
        

        m_socket->SetRecvCallback(MakeCallback(&DashClient::HandleRead, this));
        m_socket->SetConnectCallback(MakeCallback(&DashClient::ConnectionSucceeded, this),
                                     MakeCallback(&DashClient::ConnectionFailed, this));
        m_socket->SetSendCallback(MakeCallback(&DashClient::DataSend, this));
        m_socket->SetCloseCallbacks(MakeCallback(&DashClient::ConnectionNormalClosed, this),
                                    MakeCallback(&DashClient::ConnectionErrorClosed, this));
        m_socket->Connect(m_peer);
        m_connectWatchdogTimer.Cancel();
        m_connectWatchdogTimer = Simulator::Schedule(MilliSeconds(200),
                                                     &DashClient::ConnectWatchdog,
                                                     this);

        NS_LOG_INFO("Connected callbacks");
    }
    NS_LOG_INFO("Just started connection");
}

void
DashClient::StopApplication(void) // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
        m_connected = false;
        m_player.m_state = MPEG_PLAYER_DONE;
        m_periodicBufferCheckTimer.Cancel();
        m_connectWatchdogTimer.Cancel();
        m_segmentWatchdogTimer.Cancel();
    }
    else
    {
        NS_LOG_WARN("DashClient found null socket to close in StopApplication");
    }
}

// Private helpers

void
DashClient::RequestSegment()
{
    NS_LOG_FUNCTION(this);

    if (m_RequestPending)
    {
        return;
    }
    m_RequestPending = true;

    if (m_connected == false)
    {
        m_RequestPending = false;
        return;
    }
    
    if (m_maxSegments > 0)
    {
        if (m_segmentId >= m_maxSegments)
        {
            m_RequestPending = false;
            return;
        }
    }
    uint32_t requestSegmentId = m_segmentId++;
    m_pendingSegmentId = requestSegmentId;
    m_pendingBitRate = m_bitRate;
    m_pendingRetryUsed = false;
    SendSegmentRequest(requestSegmentId, m_bitRate, false);
    m_requestTime = Simulator::Now();
    m_segment_bytes = 0;
    m_lastWatchdogBytes = 0;
    m_watchdogStuckTicks = 0;

    m_segmentWatchdogTimer.Cancel();
    m_segmentWatchdogTimer = Simulator::Schedule(MilliSeconds(500),
                                                 &DashClient::SegmentRequestWatchdog,
                                                 this);
}

int
DashClient::SendSegmentRequest(uint32_t segmentId, uint32_t bitrate, bool isRetry)
{
    (void)isRetry;
    Ptr<Packet> packet = Create<Packet>(0);

    HTTPHeader httpHeader;
    httpHeader.SetSeq(1);
    httpHeader.SetMessageType(HTTP_REQUEST);
    httpHeader.SetVideoId(m_videoId);
    httpHeader.SetResolution(bitrate);
    httpHeader.SetSegmentId(segmentId);
    packet->AddHeader(httpHeader);

    int res = m_socket->Send(packet);
    if (res < 0)
    {
        // Send can fail legitimately (e.g. the transport socket already closed after an idle timeout
        // on a stalled connection, or a momentarily full send buffer). NS_FATAL_ERROR here turned one
        // dead client into a dead SIMULATION (observed: QUIC's Send-in-IDLE abort at t=554 s killed a
        // whole pilot run). Treat it like a lost request instead: the segment watchdog will retry once
        // and eventually free the client (m_RequestPending=false) so the run and the other UEs go on.
        NS_LOG_WARN("DashClient " << m_id << ": segment request send failed (res=" << res
                    << ", size=" << packet->GetSize() << ") - leaving recovery to the watchdog");
        return res;
    }

    m_txTrace(packet);
    return res;
}

void
DashClient::SegmentRequestWatchdog()
{
    if (!m_connected || !m_socket || !m_RequestPending)
    {
        return;
    }

    // Progress-aware watchdog. The previous version re-sent the segment request on every 500 ms tick
    // regardless of whether the segment was actively downloading. Under multi-user load that turns a
    // merely-slow segment (loss + slow recovery) into a duplicate full-segment resend at the server,
    // which adds congestion and slows every other segment -> a positive-feedback stall spiral. Instead:
    //  - if the segment is making progress (more bytes than the last tick), it is just slow: keep
    //    waiting, do NOT re-request;
    //  - only if there is NO progress do we act: a single re-request when nothing at all has arrived
    //    (the request itself was likely lost), otherwise just keep waiting for the in-flight data;
    //  - as a last resort, after a long stall with no progress, free the client so it can recover
    //    (instead of leaving m_RequestPending stuck true forever).
    bool progressing = (m_segment_bytes > m_lastWatchdogBytes);
    m_lastWatchdogBytes = m_segment_bytes;

    if (progressing)
    {
        m_watchdogStuckTicks = 0;
        m_segmentWatchdogTimer = Simulator::Schedule(MilliSeconds(500),
                                                     &DashClient::SegmentRequestWatchdog, this);
        return;
    }

    m_watchdogStuckTicks++;

    // Nothing received yet: the request may have been lost in a handover/loss burst — retry it once.
    if (m_segment_bytes == 0 && !m_pendingRetryUsed)
    {
        m_pendingRetryUsed = true;
        SendSegmentRequest(m_pendingSegmentId, m_pendingBitRate, true);
        m_segmentWatchdogTimer = Simulator::Schedule(MilliSeconds(500),
                                                     &DashClient::SegmentRequestWatchdog, this);
        return;
    }

    // Some data arrived then progress stalled, or the lone retry already went out. Keep waiting for the
    // in-flight segment (it usually completes), but bound the wait so a truly dead segment frees the
    // client (~5 s of no progress) rather than wedging it permanently.
    if (m_watchdogStuckTicks < 10)
    {
        m_segmentWatchdogTimer = Simulator::Schedule(MilliSeconds(500),
                                                     &DashClient::SegmentRequestWatchdog, this);
        return;
    }

    m_RequestPending = false; // give up on this segment; allow the buffer-underrun path to re-request
}

void
DashClient::SendBlank()
{
    HTTPHeader http_header;
    http_header.SetMessageType(HTTP_BLANK);
    http_header.SetVideoId(-1);
    http_header.SetResolution(-1);
    http_header.SetSegmentId(-1);

    Ptr<Packet> blank_packet = Create<Packet>(0);
    blank_packet->AddHeader(http_header);

    m_socket->Send(blank_packet);
}

void
DashClient::CheckBuffer()
{
    NS_LOG_FUNCTION(this);
    if (!m_socket)
    {
        NS_LOG_DEBUG("CheckBuffer called with null socket");
        return;
    }
    m_parser.ReadSocket(m_socket);
}

void
DashClient::PeriodicBufferCheck()
{
    NS_LOG_FUNCTION(this);
    
    // Periodically check buffer even when player is paused
    // This ensures we continue receiving data that may have arrived while paused
    // Without this, if HandleRead callback doesn't fire, we'd never check for data
    if (m_connected && m_socket)
    {
        uint32_t rxAvailable = m_socket->GetRxAvailable();
        
        // Check if there's data available to read
        if (rxAvailable > 0)
        {
            m_parser.ReadSocket(m_socket);
        }
        else
        {
        }
        
        // Continue periodic checks if player is paused
        // Stop if player is done or not started
        if (m_player.m_state == MPEG_PLAYER_PAUSED)
        {
            m_periodicBufferCheckTimer = Simulator::Schedule(MilliSeconds(50), 
                                                             &DashClient::PeriodicBufferCheck, this);
        }
        else
        {
            // Player resumed or done, cancel periodic checks
            m_periodicBufferCheckTimer.Cancel();
        }
    }
    else
    {
    }
}

void
DashClient::KeepAliveTimeout()
{
    if (m_socket)
    {
        SendBlank();
    }
    else
    {
        m_keepAliveTimer.Cancel();
        Time delay = MilliSeconds(300);
        m_keepAliveTimer = Simulator::Schedule(delay, &DashClient::KeepAliveTimeout, this);
    }
}

void
DashClient::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    
    m_keepAliveTimer.Cancel();
    Time delay = MilliSeconds(300);
    m_keepAliveTimer = Simulator::Schedule(delay, &DashClient::KeepAliveTimeout, this);
    
    m_parser.ReadSocket(socket);
}

void
DashClient::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_UNCOND("DashClient " << m_id << " (VideoId=" << m_videoId << ") - Connection SUCCEEDED at time " << Simulator::Now().GetSeconds() << "s");
    m_connected = true;
    m_connectWatchdogTimer.Cancel();
    socket->SetCloseCallbacks(MakeCallback(&DashClient::ConnectionNormalClosed, this),
                              MakeCallback(&DashClient::ConnectionErrorClosed, this));

    RequestSegment();
}

void
DashClient::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_UNCOND("DashClient " << m_id << " (VideoId=" << m_videoId << ") - Connection FAILED at time " << Simulator::Now().GetSeconds() << "s - Retrying...");
    m_connectWatchdogTimer.Cancel();
    m_segmentWatchdogTimer.Cancel();
    m_socket = 0;
    m_connected = false;
    m_RequestPending = false;
    m_pendingRetryUsed = false;
    StartApplication();
}

void
DashClient::ConnectionNormalClosed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_INFO("DashClient " << m_id << ", Connection closed normally");
}

void
DashClient::ConnectionErrorClosed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_INFO("DashClient " << m_id << ", Connection closed due to error, retrying...");
    m_connectWatchdogTimer.Cancel();
    m_segmentWatchdogTimer.Cancel();
    m_socket = 0;
    m_connected = false;
    m_RequestPending = false;
    m_pendingRetryUsed = false;
    StartApplication();
}

void
DashClient::ConnectWatchdog()
{
    if (m_connected || !m_socket)
    {
        return;
    }

    NS_LOG_UNCOND("DashClient " << m_id << " (VideoId=" << m_videoId
                   << ") - connect watchdog fired at time "
                   << Simulator::Now().GetSeconds()
                   << "s, retrying connection setup");

    Ptr<Socket> oldSocket = m_socket;
    m_socket = 0;
    m_connected = false;
    oldSocket->Close();
    m_segmentWatchdogTimer.Cancel();
    m_RequestPending = false;
    m_pendingRetryUsed = false;
    Simulator::Schedule(MilliSeconds(1), &DashClient::StartApplication, this);
}

void
DashClient::DataSend(Ptr<Socket>, uint32_t)
{
    NS_LOG_FUNCTION(this);

    if (m_connected)
    { // Only send new data if the connection has completed

        NS_LOG_INFO("DashClient " << m_id << ", Something was sent");
    }
    else
    {
        NS_LOG_INFO("DashClient " << m_id << ", NOT CONNECTED!!!!");
    }
}

bool
DashClient::MessageReceived(Packet message)
{
    NS_LOG_FUNCTION(this << message);

    MPEGHeader mpegHeader;
    HTTPHeader httpHeader;
    const uint32_t headerSize = httpHeader.GetSerializedSize() + mpegHeader.GetSerializedSize();

    if (message.GetSize() < headerSize)
    {
        NS_LOG_UNCOND("Dropping undersized message: size=" << message.GetSize()
                                                         << " headerSize=" << headerSize);
        return true;
    }

    Ptr<Packet> tempPacket = message.Copy();
    uint32_t removedHttp = tempPacket->RemoveHeader(httpHeader);
    uint32_t removedMpeg = tempPacket->RemoveHeader(mpegHeader);
    if (removedHttp != httpHeader.GetSerializedSize() ||
        removedMpeg != mpegHeader.GetSerializedSize())
    {
        NS_LOG_UNCOND("Dropping corrupted DASH message: removedHttp=" << removedHttp
                    << " removedMpeg=" << removedMpeg
                    << " size=" << message.GetSize());
        return true;
    }
    if (httpHeader.GetMessageType() != HTTP_RESPONSE ||
        mpegHeader.GetFrameId() >= MPEG_FRAMES_PER_SEGMENT ||
        mpegHeader.GetSize() + headerSize != message.GetSize())
    {
        NS_LOG_UNCOND("Dropping invalid DASH message fields: msgType=" << httpHeader.GetMessageType()
                    << " frameId=" << mpegHeader.GetFrameId()
                    << " frameSize=" << mpegHeader.GetSize()
                    << " total=" << message.GetSize());
        return true;
    }

    // Send the frame to the player (with headers intact - PlayFrame will remove them)
    // If it doesn't fit in the buffer, don't continue
    if (!m_player.ReceiveFrame(&message))
    {
        return false;
    }
    m_segment_bytes += message.GetSize();
    m_totBytes += message.GetSize();
    m_rxTrace(message.Copy());

    switch (m_player.m_state)
    {
    case MPEG_PLAYER_PLAYING:
        m_sumDt += m_player.GetRealPlayTime(mpegHeader.GetPlaybackTime());
        break;
    case MPEG_PLAYER_PAUSED:
        break;
    case MPEG_INITIAL_BUFFERING:
        break;
    case MPEG_PLAYER_DONE:
        return true;
    default:
        NS_FATAL_ERROR("WRONG STATE");
    }

    if (mpegHeader.GetFrameId() == MPEG_FRAMES_PER_SEGMENT - 1)
    {
        m_RequestPending = false;
        m_segmentWatchdogTimer.Cancel();
        m_pendingRetryUsed = false;
        m_segmentFetchTime = Simulator::Now() - m_requestTime;

        // Feed the bitrate info to the player
        AddBitRate(Simulator::Now(), 8 * m_segment_bytes / m_segmentFetchTime.GetSeconds());

        Time currDt = m_player.GetRealPlayTime(mpegHeader.GetPlaybackTime());
        // Sanitize: absurd currDt (e.g. from reordering/corruption) corrupts m_lastDt and state
        if (currDt.GetSeconds() > 120.0 || currDt.GetSeconds() < -10.0)
          {
            NS_LOG_WARN("DASH client " << GetNode()->GetId() << " currDt=" << currDt.GetSeconds()
                        << "s out of range, clamping to last valid or 0");
            currDt = (m_lastDt >= Time("0s")) ? m_lastDt : Seconds(0);
          }
        // And tell the player to monitor the buffer level
        LogBufferLevel(currDt);

        uint32_t old = m_bitRate;
        //  double diff = m_lastDt >= 0 ? (currDt - m_lastDt).GetSeconds() : 0;

        Time bufferDelay;

        // m_player.CalcNextSegment(m_bitRate, m_player.GetBufferEstimate(), diff,
        // m_bitRate, bufferDelay);

        uint32_t prevBitrate = m_bitRate;

        CalcNextSegment(prevBitrate, m_bitRate, bufferDelay);

        if (prevBitrate != m_bitRate)
        {
            m_rateChanges++;
        }

        // BufferController-style hard cap (models dash.js's separate buffer target). Holds the actual
        // playback buffer at m_maxBufferS regardless of the ABR's internal buffer target, so the
        // buffer capacity can be swept independently of BOLA's bufferTime. When the buffer plus one
        // segment would exceed the cap, defer the next fetch until it has drained back to the cap.
        if (m_maxBufferS > 0.0)
        {
            const double segDur = (MPEG_FRAMES_PER_SEGMENT * MPEG_TIME_BETWEEN_FRAMES) / 1000.0;
            double capDelay = currDt.GetSeconds() + segDur - m_maxBufferS;
            if (capDelay > bufferDelay.GetSeconds())
            {
                bufferDelay = Seconds(capDelay);
            }
        }

        if (bufferDelay == Seconds(0))
        {
            RequestSegment();
        }
        else
        {
            Simulator::Schedule(bufferDelay, &DashClient::RequestSegment, this);
        }

        std::cout << Simulator::Now().GetSeconds() << " ue-id: " << GetNode()->GetId()
                  << " newBitRate: " << m_bitRate << " oldBitRate: " << old
                  << " estBitRate: " << GetBitRateEstimate()
                  << " interTime: " << m_player.m_interruption_time.GetSeconds()
                  << " T: " << currDt.GetSeconds()
                  << " dT: " << (m_lastDt >= Time("0s") ? (currDt - m_lastDt).GetSeconds() : 0)
                  << " del: " << bufferDelay.GetSeconds() << std::endl;

        NS_LOG_INFO("==== Last frame received. Requesting segment " << m_segmentId);

        (void)old;
        NS_LOG_INFO("!@#$#@!$@#\t" << Simulator::Now().GetSeconds() << " old: " << old
                                   << " new: " << m_bitRate << " t: " << currDt.GetSeconds()
                                   << " dt: " << (currDt - m_lastDt).GetSeconds());

        m_lastDt = currDt;
    }
    return true;
}

void
DashClient::CalcNextSegment(uint32_t currRate, uint32_t& nextRate, Time& delay)
{
    nextRate = currRate;
    delay = Seconds(0);
}

void
DashClient::GetStats()
{
    // Finalize interruption time if player is still paused
    // This handles the case where the simulation ends while the player is paused
    // and the interruption time wasn't recorded because playback never resumed
    m_player.FinalizeInterruptionTime();
    
    std::cout << " ue-id: " << GetNode()->GetId()
              << " InterruptionTime: " << m_player.m_interruption_time.GetSeconds()
              << " interruptions: " << m_player.m_interrruptions
              << " avgRate: " << (1.0 * m_player.m_totalRate) / m_player.m_framesPlayed
              << " minRate: " << m_player.m_minRate
              << " AvgDt: " << m_sumDt.GetSeconds() / m_player.m_framesPlayed
              << " changes: " << m_rateChanges
              << " TotalPlaybackTime: " << m_player.m_totalPlaybackTime.GetSeconds() << std::endl;
}

void
DashClient::LogBufferLevel(Time t)
{
    m_bufferState[Simulator::Now()] = t;
    for (auto it = m_bufferState.cbegin(); it != m_bufferState.cend();)
    {
        if (it->first < (Simulator::Now() - m_window))
        {
            m_bufferState.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

double
DashClient::GetBufferEstimate()
{
    double sum = 0;
    int count = 0;
    for (std::map<Time, Time>::iterator it = m_bufferState.begin(); it != m_bufferState.end(); ++it)
    {
        sum += it->second.GetSeconds();
        count++;
    }
    return sum / count;
}

double
DashClient::GetBufferDifferential()
{
    std::map<Time, Time>::iterator it = m_bufferState.end();

    if (it == m_bufferState.begin())
    {
        // Empty buffer
        return 0;
    }
    it--;
    Time last = it->second;

    if (it == m_bufferState.begin())
    {
        // Only one element
        return 0;
    }
    it--;
    Time prev = it->second;
    return (last - prev).GetSeconds();
}

double
DashClient::GetSegmentFetchTime()
{
    return m_segmentFetchTime.GetSeconds();
}

void
DashClient::AddBitRate(Time time, double bitrate)
{
    // Remove old values outside the window
    while (!m_bitrateQueue.empty() && m_bitrateQueue.front().first < (time - m_window))
    {
        m_bitrateSum -= m_bitrateQueue.front().second;
        m_bitrateQueue.pop_front();
    }

    // Add the new value
    m_bitrateQueue.emplace_back(time, bitrate);
    m_bitrateSum += bitrate;

    // Update the estimated bitrate
    m_bitrateEstimate = m_bitrateSum / m_bitrateQueue.size();
}

} // Namespace ns3
