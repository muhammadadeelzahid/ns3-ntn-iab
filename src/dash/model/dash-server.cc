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

#include "dash-server.h"

#include "http-header.h"
#include "mpeg-header.h"

#include "ns3/address-utils.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket.h"
#include <ns3/double.h>
#include <ns3/random-variable-stream.h>
#include <ns3/tcp-socket.h>

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("DashServer");
NS_OBJECT_ENSURE_REGISTERED(DashServer);

TypeId
DashServer::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::DashServer")
                            .SetParent<Application>()
                            .AddConstructor<DashServer>()
                            .AddAttribute("Local",
                                          "The Address on which to Bind the rx socket.",
                                          AddressValue(),
                                          MakeAddressAccessor(&DashServer::m_local),
                                          MakeAddressChecker())
                            .AddAttribute("Protocol",
                                          "The type id of the protocol to use for the rx socket.",
                                          TypeIdValue(TcpSocketFactory::GetTypeId()),
                                          MakeTypeIdAccessor(&DashServer::m_tid),
                                          MakeTypeIdChecker())
                            .AddTraceSource("Rx",
                                            "A packet has been received",
                                            MakeTraceSourceAccessor(&DashServer::m_rxTrace),
                                            "ns3::Packet::TracedCallback");
    return tid;
}

DashServer::DashServer()
    : m_socket(0),
      m_totalRx(0)
{
    NS_LOG_FUNCTION(this);
}

DashServer::~DashServer()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Socket>
DashServer::GetListeningSocket(void) const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}

std::list<Ptr<Socket>>
DashServer::GetAcceptedSockets(void) const
{
    NS_LOG_FUNCTION(this);
    return m_socketList;
}

void
DashServer::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_socketList.clear();

    // chain up
    Application::DoDispose();
}

// Application Methods
void
DashServer::StartApplication() // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);
    // Create the socket if not already
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        m_socket->Bind(m_local);
        m_socket->Listen();
        // m_socket->ShutdownSend ();
        if (addressUtils::IsMulticast(m_local))
        {
            Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
            if (udpSocket)
            {
                // equivalent to setsockopt (MCAST_JOIN_GROUP)
                udpSocket->MulticastJoinGroup(0, m_local);
            }
            else
            {
                NS_FATAL_ERROR("Error: joining multicast on a non-UDP socket");
            }
        }
    }

    m_socket->SetRecvCallback(MakeCallback(&DashServer::HandleRead, this));

    m_socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                MakeCallback(&DashServer::HandleAccept, this));
    m_socket->SetCloseCallbacks(MakeCallback(&DashServer::HandlePeerClose, this),
                                MakeCallback(&DashServer::HandlePeerError, this));
}

void
DashServer::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);
    while (!m_socketList.empty()) // these are accepted sockets, close them
    {
        Ptr<Socket> acceptedSocket = m_socketList.front();
        m_socketList.pop_front();
        acceptedSocket->Close();
    }
    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }
}

void
DashServer::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_DEBUG("[DASH SERVER] HandleRead called at " << Simulator::Now().GetSeconds() << "s");
    
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        m_totalRx += packet->GetSize();
        NS_LOG_DEBUG("[DASH SERVER] Received packet: " << packet->GetSize() << " bytes, total Rx: " << m_totalRx);

        if (!m_pending_packet)
        {
            m_pending_packet = packet;
        }
        else
        {
            m_pending_packet->AddAtEnd(packet);
        }

        HTTPHeader header;
        NS_LOG_DEBUG("[DASH SERVER] Pending packet size: " << m_pending_packet->GetSize() 
                     << ", header size: " << header.GetSerializedSize());

        while (m_pending_packet->GetSize() >= header.GetSerializedSize())
        {
            m_pending_packet->RemoveHeader(header);
            NS_LOG_DEBUG("[DASH SERVER] Parsed header - Type: " << (int)header.GetMessageType() 
                         << " (HTTP_REQUEST=" << (int)HTTP_REQUEST << ")");
            
            if (header.GetMessageType() == HTTP_REQUEST)
            {
                NS_LOG_DEBUG("[DASH SERVER] Processing HTTP_REQUEST - VideoId: " << header.GetVideoId()
                             << ", Resolution: " << header.GetResolution()
                             << ", SegmentId: " << header.GetSegmentId());
                
                SendSegment(header.GetVideoId(),
                            header.GetResolution(),
                            header.GetSegmentId(),
                            socket);
            }
            else
            {
                NS_LOG_DEBUG("[DASH SERVER] WARNING: Message type is NOT HTTP_REQUEST!");
            }
        }

        if (InetSocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s packet sink received "
                                   << packet->GetSize() << " bytes from "
                                   << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(from).GetPort() << " total Rx "
                                   << m_totalRx << " bytes");
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("At time " << Simulator::Now().GetSeconds() << "s packet sink received "
                                   << packet->GetSize() << " bytes from "
                                   << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(from).GetPort()
                                   << " total Rx " << m_totalRx << " bytes");
        }
        m_rxTrace(packet, from);
    }
}

void
DashServer::HandlePeerClose(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
}

void
DashServer::HandlePeerError(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
}

void
DashServer::HandleAccept(Ptr<Socket> s, const Address& from)
{
    NS_LOG_FUNCTION(this << s << from);
    s->SetRecvCallback(MakeCallback(&DashServer::HandleRead, this));
    s->SetSendCallback(MakeCallback(&DashServer::DataSend, this));
    m_socketList.push_back(s);
}

void
DashServer::DataSend(Ptr<Socket> socket, uint32_t)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("[DASH SERVER] DataSend called at " << Simulator::Now().GetSeconds() << "s");
    NS_LOG_DEBUG("[DASH SERVER] Queue size: " << m_queues[socket].size() << " frames");

    // for (std::map<Ptr<Socket>, std::queue<Packet>>::iterator iter = m_queues.begin ();
    //      iter != m_queues.end (); ++iter)
    //   {
    //     HTTPHeader httpHeader;
    //     MPEGHeader mpegHeader;

    //     if (iter->second.size ())
    //       {
    //         Ptr<Packet> frame = iter->second.front ().Copy ();

    //         frame->RemoveHeader (mpegHeader);
    //         frame->RemoveHeader (httpHeader);

    //         NS_LOG_INFO ("VidId: " << httpHeader.GetVideoId ()
    //                                << " rxAv= " << iter->first->GetRxAvailable ()
    //                                << " queue= " << iter->second.size ()
    //                                << " res= " << httpHeader.GetResolution ());
    //       }
    //   }

    uint32_t frames_sent = 0;
    uint32_t bytes_sent = 0;
    
    while (!m_queues[socket].empty())
    {
        uint32_t max_tx_size = socket->GetTxAvailable();

        if (max_tx_size <= 0)
        {
            NS_LOG_DEBUG("[DASH SERVER] Socket Send buffer is full! Frames remaining: " 
                         << m_queues[socket].size());
            NS_LOG_INFO("Socket Send buffer is full");
            return;
        }
        
        if (frames_sent == 0)
        {
            NS_LOG_DEBUG("[DASH SERVER] Socket Tx buffer available: " << max_tx_size << " bytes");
        }

        Ptr<Packet> frame = m_queues[socket].front().Copy();
        m_queues[socket].pop_front();

        uint32_t init_size = frame->GetSize();

        if (max_tx_size < init_size)
        {
            NS_LOG_INFO("Insufficient space in send buffer, fragmenting");
            Ptr<Packet> frag0 = frame->CreateFragment(0, max_tx_size);
            Ptr<Packet> frag1 = frame->CreateFragment(max_tx_size, init_size - max_tx_size);

            m_queues[socket].push_front(*frag1);
            frame = frag0;
        }

        // FIX for QUIC: Handle partial sends
        // Like the client, the server must handle partial sends from QUIC sockets.
        // QUIC's flow control may limit how much data can be sent at once, but
        // the protocol guarantees that all data will eventually be delivered.
        int bytes;
        bytes = socket->Send(frame);
        if (bytes < 0)
        {
            NS_LOG_DEBUG("[DASH SERVER] ERROR: Send failed with bytes = " << bytes);
            NS_FATAL_ERROR("Couldn't send packet, though space should be available, bytes = " << bytes);
            exit(1);
        }
        else if ((uint32_t)bytes < frame->GetSize())
        {
            // QUIC socket sent partial data, acceptable.
            NS_LOG_DEBUG("[DASH SERVER] Partial send: " << bytes << " out of " << frame->GetSize());
            frames_sent++;
            bytes_sent += bytes;
        }
        else
        {
            frames_sent++;
            bytes_sent += bytes;
            if (frames_sent <= 2 || frames_sent == MPEG_FRAMES_PER_SEGMENT)
            {
                NS_LOG_DEBUG("[DASH SERVER] Frame sent successfully: " << bytes << " bytes");
            }
        }
    }
    
    NS_LOG_DEBUG("[DASH SERVER] DataSend COMPLETE - Sent " << frames_sent << " frames, " 
                 << bytes_sent << " bytes, Queue remaining: " << m_queues[socket].size());
    
}

void
DashServer::SendSegment(uint32_t video_id,
                        uint32_t resolution,
                        uint32_t segment_id,
                        Ptr<Socket> socket)
{
    NS_LOG_DEBUG("[DASH SERVER] SendSegment START - VideoId: " << video_id 
                 << ", Resolution: " << resolution << ", SegmentId: " << segment_id);
    
    int fps = 1000 / MPEG_TIME_BETWEEN_FRAMES;
    int avg_packetsize = resolution / (fps * 8);
    NS_LOG_DEBUG("[DASH SERVER] Average packet size: " << avg_packetsize << " bytes");

    HTTPHeader http_header_tmp;
    MPEGHeader mpeg_header_tmp;

    Ptr<UniformRandomVariable> frame_size_gen = CreateObject<UniformRandomVariable>();

    frame_size_gen->SetAttribute("Min", DoubleValue(0));
    frame_size_gen->SetAttribute(
        "Max",
        DoubleValue(std::max(2 * avg_packetsize - (int)(mpeg_header_tmp.GetSerializedSize() +
                                                        http_header_tmp.GetSerializedSize()),
                             1)));

    uint32_t total_segment_bytes = 0;
    for (uint32_t f_id = 0; f_id < MPEG_FRAMES_PER_SEGMENT; f_id++)
    {
        uint32_t frame_size = (unsigned)frame_size_gen->GetValue();

        HTTPHeader http_header;
        http_header.SetMessageType(HTTP_RESPONSE);
        http_header.SetVideoId(video_id);
        http_header.SetResolution(resolution);
        http_header.SetSegmentId(segment_id);

        MPEGHeader mpeg_header;
        mpeg_header.SetFrameId(f_id);
        mpeg_header.SetPlaybackTime(MilliSeconds((f_id + (segment_id * MPEG_FRAMES_PER_SEGMENT)) *
                                                 MPEG_TIME_BETWEEN_FRAMES));
        mpeg_header.SetType('B');
        mpeg_header.SetSize(frame_size);
        NS_LOG_DEBUG("[TEMP_LOGS] SERVER_SET_FRAME_SIZE: frame_id=" << f_id 
                      << " segment_id=" << segment_id 
                      << " frame_size=" << frame_size 
                      << " resolution=" << resolution
                      << " avg_packetsize=" << avg_packetsize
                      << " time=" << Simulator::Now().GetSeconds());

        Ptr<Packet> frame = Create<Packet>(frame_size);
        frame->AddHeader(mpeg_header);
        frame->AddHeader(http_header);
        total_segment_bytes += frame->GetSize();
        
        if (f_id == 0 || f_id == MPEG_FRAMES_PER_SEGMENT - 1)
        {
            NS_LOG_DEBUG("[DASH SERVER] Frame " << f_id << " created: " << frame->GetSize() 
                         << " bytes (payload: " << frame_size << ")");
        }

        m_queues[socket].push_back(*frame);
    }
    
    NS_LOG_DEBUG("[DASH SERVER] SendSegment COMPLETE - Created " << MPEG_FRAMES_PER_SEGMENT 
                 << " frames, total " << total_segment_bytes << " bytes");
    NS_LOG_DEBUG("[DASH SERVER] Queue size for this socket: " << m_queues[socket].size() << " frames");
    NS_LOG_DEBUG("[DASH SERVER] Calling DataSend to transmit queued frames...");
    
    DataSend(socket, 0);
}

} // Namespace ns3
