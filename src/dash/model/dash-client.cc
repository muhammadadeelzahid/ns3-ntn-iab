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
#include <ns3/tcp-socket-factory.h>
#include <ns3/uinteger.h>

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

        m_socket->Connect(m_peer);
        m_socket->SetRecvCallback(MakeCallback(&DashClient::HandleRead, this));
        m_socket->SetConnectCallback(MakeCallback(&DashClient::ConnectionSucceeded, this),
                                     MakeCallback(&DashClient::ConnectionFailed, this));
        m_socket->SetSendCallback(MakeCallback(&DashClient::DataSend, this));
        m_socket->SetCloseCallbacks(MakeCallback(&DashClient::ConnectionNormalClosed, this),
                                    MakeCallback(&DashClient::ConnectionErrorClosed, this));

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
    NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] RequestSegment called at " << Simulator::Now().GetSeconds() << "s");

    if (m_RequestPending)
    {
        NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] Request already pending for segment " << m_segmentId);
        NS_LOG_INFO("Not requesting, found pending m_segmentId = " << m_segmentId);
        return;
    }
    m_RequestPending = true;

    if (m_connected == false)
    {
        NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] Not connected yet!");
        return;
    }
    
    if (m_maxSegments > 0)
    {
        if (m_segmentId >= m_maxSegments)
        {
            NS_LOG_INFO("Maximum segments reached, not requesting more segments.");
            return;
        }
    }

    NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] Requesting segment " << m_segmentId 
                 << " at bitrate " << m_bitRate);

    Ptr<Packet> packet = Create<Packet>(0);

    HTTPHeader httpHeader;
    httpHeader.SetSeq(1);
    httpHeader.SetMessageType(HTTP_REQUEST);
    httpHeader.SetVideoId(m_videoId);
    httpHeader.SetResolution(m_bitRate);
    httpHeader.SetSegmentId(m_segmentId++);
    packet->AddHeader(httpHeader);

    int res = 0;
    res = m_socket->Send(packet);
    if (res < 0)
    {
        NS_FATAL_ERROR("Oh oh. Couldn't send packet! res=" << res << " size=" << packet->GetSize());
    }
    else if ((unsigned)res != packet->GetSize())
    {
        // QUIC socket sent partial data, acceptable.
        NS_LOG_INFO("Partial send: res=" << res << " size=" << packet->GetSize());
    }

    // Fire Tx trace for logging
    m_txTrace(packet);

    m_requestTime = Simulator::Now();
    m_segment_bytes = 0;
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
    m_parser.ReadSocket(m_socket);
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
    NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] HandleRead called at " << Simulator::Now().GetSeconds() << "s");
    
    m_keepAliveTimer.Cancel();
    Time delay = MilliSeconds(300);
    m_keepAliveTimer = Simulator::Schedule(delay, &DashClient::KeepAliveTimeout, this);
    
    // Fire Rx trace for received video data (peek without consuming)
    uint32_t availableData = socket->GetRxAvailable();
    NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] Available data in socket: " << availableData << " bytes");
    
    if (availableData > 0)
    {
        // Create a dummy packet representing the available data for tracing
        Ptr<Packet> dummyPacket = Create<Packet>(availableData);
        Address peerAddress;
        socket->GetPeerName(peerAddress);
        NS_LOG_DEBUG("[DASH CLIENT " << m_id << "] Fired Rx trace for " << availableData << " bytes");
    }
    
    m_parser.ReadSocket(socket);
}

void
DashClient::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_LOGIC("DashClient Connection succeeded");
    m_connected = true;
    socket->SetCloseCallbacks(MakeCallback(&DashClient::ConnectionNormalClosed, this),
                              MakeCallback(&DashClient::ConnectionErrorClosed, this));

    // Start periodic CheckBuffer to ensure we read data even if callbacks stop firing
    // This is critical because socket recv callbacks may not fire when socket buffer is full
    Simulator::Schedule(MilliSeconds(50), &DashClient::CheckBuffer, this);

    RequestSegment();
}

void
DashClient::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_LOGIC("DashClient " << m_id << ", Connection Failed, retrying...");
    m_socket = 0;
    m_connected = false;
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
    m_socket = 0;
    m_connected = false;
    StartApplication();
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

    // Send the frame to the player
    // If it doesn't fit in the buffer, don't continue
    if (!m_player.ReceiveFrame(&message))
    {
        return false;
    }
    m_segment_bytes += message.GetSize();
    m_totBytes += message.GetSize();

    // Fire Rx trace for received video segment
    Ptr<const Packet> packet = message.Copy();
    m_rxTrace(packet);

    message.RemoveHeader(httpHeader);
    message.RemoveHeader(mpegHeader);

    // Calculate the buffering time
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

    NS_LOG_INFO("Received frame " << mpegHeader.GetFrameId() << " out of "
                                  << MPEG_FRAMES_PER_SEGMENT);

    // If we received the last frame of the segment
    if (mpegHeader.GetFrameId() == MPEG_FRAMES_PER_SEGMENT - 1)
    {
        m_RequestPending = false;
        m_segmentFetchTime = Simulator::Now() - m_requestTime;

        NS_LOG_INFO(Simulator::Now().GetSeconds()
                    << " bytes: " << m_segment_bytes
                    << " segmentTime: " << m_segmentFetchTime.GetSeconds()
                    << " segmentRate: " << 8 * m_segment_bytes / m_segmentFetchTime.GetSeconds());

        // Feed the bitrate info to the player
        AddBitRate(Simulator::Now(), 8 * m_segment_bytes / m_segmentFetchTime.GetSeconds());

        Time currDt = m_player.GetRealPlayTime(mpegHeader.GetPlaybackTime());
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

        if (bufferDelay == Seconds(0))
        {
            NS_LOG_INFO("Requesting frame immediately due zero buffer delay");
            RequestSegment();
        }
        else
        {
            Simulator::Schedule(bufferDelay, &DashClient::RequestSegment, this);
        }

        std::cout << Simulator::Now().GetSeconds() << " Node: " << m_id
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
    
    std::cout << " InterruptionTime: " << m_player.m_interruption_time.GetSeconds()
              << " interruptions: " << m_player.m_interrruptions
              << " avgRate: " << (1.0 * m_player.m_totalRate) / m_player.m_framesPlayed
              << " minRate: " << m_player.m_minRate
              << " AvgDt: " << m_sumDt.GetSeconds() / m_player.m_framesPlayed
              << " changes: " << m_rateChanges << std::endl;
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
