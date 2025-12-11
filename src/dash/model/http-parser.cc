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

#include "http-parser.h"

#include "dash-client.h"
#include "http-header.h"
#include "mpeg-header.h"

#include "ns3/address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"

NS_LOG_COMPONENT_DEFINE("HttpParser");

namespace ns3
{

HttpParser::HttpParser()
    : m_app(nullptr),
      m_lastmeasurement("0s")
{
    NS_LOG_FUNCTION(this);
}

HttpParser::~HttpParser()
{
    NS_LOG_FUNCTION(this);
}

void
HttpParser::SetApp(DashClient* app)
{
    NS_LOG_FUNCTION(this << app);
    m_app = app;
}

void
HttpParser::ReadSocket(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (m_pending_packet)
    {
        TryToPushToPlayer();
    }

    if (m_pending_packet && m_pending_packet->GetSize() >= m_pending_message_size)
    {
        NS_LOG_INFO("Not reading socket, our play buffer is likely full ");
        // Send dummy packet to prevent deadlock
        // due to TCP "Silly Window"
        // See also: https://www.nsnam.org/bugzilla/show_bug.cgi?id=1565
        m_app->SendBlank();
        return;
    }

    if (socket->GetRxAvailable() <= 0)
    {
        NS_LOG_INFO("Not reading socket, nothing to read");
        return;
    }

    Ptr<Packet> pkt = socket->Recv();

    if (!pkt)
    {
        NS_LOG_INFO("Received NULL packet");
        return;
    }

    // Skip packets that are too small to be valid DASH packets
    // Minimum DASH packet = HTTP header (28 bytes) + MPEG header (32 bytes) = 60 bytes
    // Very small packets are likely QUIC control packets, not DASH data
    if (pkt->GetSize() < 60)
    {
        NS_LOG_DEBUG("Skipping small packet (size=" << pkt->GetSize() 
                    << " bytes) - likely QUIC control packet, not DASH data");
        return;
    }

    if (!m_pending_packet)
    {
        m_pending_packet = pkt;
    }
    else
    {
        m_pending_packet->AddAtEnd(pkt);
    }

    TryToPushToPlayer();
}

void
HttpParser::TryToPushToPlayer()
{
    NS_LOG_FUNCTION(this);

    // Safety check: ensure we have a valid pending packet
    if (!m_pending_packet)
    {
        return;
    }

    while (true)
    {
        MPEGHeader mpeg_header;
        HTTPHeader http_header;
        uint32_t httpHeaderSize = http_header.GetSerializedSize();
        uint32_t mpegHeaderSize = mpeg_header.GetSerializedSize();
        uint32_t headersize = httpHeaderSize + mpegHeaderSize;

        // Safety check: ensure packet exists and has minimum size
        if (!m_pending_packet || m_pending_packet->GetSize() < headersize)
        {
            NS_LOG_INFO("### Headers incomplete (packet size: " 
                       << (m_pending_packet ? m_pending_packet->GetSize() : 0) 
                       << ", need: " << headersize << ")");
            return;
        }

        // Peek headers by copying and removing from copy (don't modify original)
        Ptr<Packet> headerPacket = m_pending_packet->Copy();
        
        // Remove headers to deserialize them - this will assert if packet is corrupted
        // but we've checked size above, so this should be safe
        headerPacket->RemoveHeader(http_header);
        headerPacket->RemoveHeader(mpeg_header);

        // Validate frame size is reasonable before using it
        uint32_t frameSize = mpeg_header.GetSize();
        if (frameSize == 0 || frameSize > 10 * 1024 * 1024) // 10MB max frame size
        {
            NS_LOG_WARN("### Invalid frame size detected: " << frameSize 
                       << " bytes. Packet may be corrupted or not a DASH packet. "
                       << "Clearing pending packet.");
            m_pending_packet = nullptr;
            return;
        }

        m_pending_message_size = headersize + frameSize;

        NS_LOG_INFO("Total size is " << m_pending_packet->GetSize() << " pending message is "
                                     << m_pending_message_size);

        if (m_pending_packet->GetSize() < m_pending_message_size)
        {
            NS_LOG_INFO("### Packet incomplete ");
            return;
        }

        Ptr<Packet> message = m_pending_packet->CreateFragment(0, m_pending_message_size);
        uint32_t total_size = m_pending_packet->GetSize();
        if (m_app->MessageReceived(*message))
        {
            NS_LOG_INFO("### Message received by mpeg_player ");
            Ptr<Packet> remainder =
                m_pending_packet->CreateFragment(m_pending_message_size,
                                                 total_size - m_pending_message_size);
            m_pending_packet = remainder;
        }
        else
        {
            NS_LOG_INFO("### Player Buffer is full ");
            return;
        }
    }
}

} // namespace ns3
