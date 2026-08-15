/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019 SIGNET Lab, Department of Information Engineering, University of Padova
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
 * Authors: Alvise De Biasio <alvise.debiasio@gmail.com>
 *          Federico Chiariotti <chiariotti.federico@gmail.com>
 *          Michele Polese <michele.polese@gmail.com>
 *          Davide Marcato <davidemarcato@outlook.com>
 *
 */

#include "ns3/packet.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
// #include "ns3/ipv4-end-point-demux.h"
// #include "ns3/ipv6-end-point-demux.h"
// #include "ns3/ipv4-end-point.h"
// #include "ns3/ipv6-end-point.h"
// #include "ns3/ipv4-l3-protocol.h"
// #include "ns3/ipv6-l3-protocol.h"
// #include "ns3/ipv6-routing-protocol.h"
#include <algorithm>
#include <sstream>
#include "quic-stream-rx-buffer.h"
#include "quic-subheader.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuicStreamRxBuffer");

QuicStreamRxItem::QuicStreamRxItem ()
  : m_packet (0),
  m_offset (0),
  m_fin (false)
{
}

QuicStreamRxItem::QuicStreamRxItem (const QuicStreamRxItem &other)
  : m_packet (other.m_packet),
  m_offset (other.m_offset),
  m_fin (other.m_fin)
{
}

void
QuicStreamRxItem::Print (std::ostream &os) const
{
  os << "[OFF " << m_offset << "]";

  if (m_fin)
    {
      os << "|fin|";
    }

}

NS_OBJECT_ENSURE_REGISTERED (QuicStreamRxBuffer);

TypeId
QuicStreamRxBuffer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::QuicStreamRxBuffer")
    .SetParent<Object> ()
    .SetGroupName ("Internet")
    .AddConstructor<QuicStreamRxBuffer> ()
    .AddTraceSource ("RxBuffer",
                    "The QUIC receive buffer",
                    MakeTraceSourceAccessor (&QuicStreamRxBuffer::m_numBytesInBuffer),
                    "ns3::TracedValueCallback::Uint32")
  ;
  return tid;
}

QuicStreamRxBuffer::QuicStreamRxBuffer ()
  : m_numBytesInBuffer (0),
  m_finalSize (0),
  m_maxBuffer (131072),
  m_recvFin (
    false)
{
  m_streamRecvList = QuicStreamRxPacketList ();
}

QuicStreamRxBuffer::~QuicStreamRxBuffer ()
{
}

void
QuicStreamRxBuffer::SetTraceContext (uint64_t connectionId, uint32_t nodeId, uint64_t streamId, const std::string &socketAddress)
{
  m_connectionId = connectionId;
  m_nodeId = nodeId;
  m_streamId = streamId;
  m_socketAddress = socketAddress;
}

bool
QuicStreamRxBuffer::Add (Ptr<Packet> p, const QuicSubheader& sub, uint64_t expectedOffset)
{
  NS_LOG_FUNCTION (this << p << sub);

  NS_LOG_INFO (
    "Try to append " << p->GetSize () << " bytes " << ", availSize=" << Available ());

  if (p->GetSize () == 0)
    {
      NS_LOG_WARN ("Discarded. Trying to insert empty packet.");
      return false;
    }

  // A FIN frame pins the final stream size even if the data itself is a
  // duplicate we end up discarding below.
  if (sub.IsStreamFin ())
    {
      NS_LOG_LOGIC ("FIN packet for the stream");
      m_finalSize = sub.GetOffset () + p->GetSize ();
      m_recvFin = true;
    }

  // Insert only the byte ranges NOT already covered (by delivered data or by
  // buffered items). Retransmission storms deliver the same ranges many times,
  // re-chunked at arbitrary boundaries; counting redundant copies as occupancy
  // exhausted the buffer, and the advertised flow-control credit
  // (m_recvSize + Available()) then lied to the sender: new UNIQUE data sent
  // within credit was rejected for room after being ACKed = bytes lost forever
  // = permanent stream stall. Trimming keeps occupancy == unique bytes, keeps
  // the credit honest, and makes the buffered items disjoint by construction.
  uint64_t start = sub.GetOffset ();
  uint64_t end = start + p->GetSize ();
  if (end <= expectedOffset)
    {
      NS_LOG_WARN ("Discarded fully-stale packet.");
      return false;
    }
  if (start < expectedOffset)
    {
      start = expectedOffset;
    }

  uint32_t insertedBytes = 0;
  QuicStreamRxPacketList::iterator it = m_streamRecvList.begin ();
  while (start < end)
    {
      // Skip items entirely below the current position
      while (it != m_streamRecvList.end ()
             && (*it)->m_offset + (*it)->m_packet->GetSize () <= start)
        {
          ++it;
        }
      if (it != m_streamRecvList.end () && (*it)->m_offset <= start)
        {
          // Covered by an existing item: jump past it
          start = (*it)->m_offset + (*it)->m_packet->GetSize ();
          ++it;
          continue;
        }
      // Uncovered up to the next item (or to the frame end)
      uint64_t pieceEnd = (it == m_streamRecvList.end () || (*it)->m_offset >= end)
        ? end : (*it)->m_offset;
      uint32_t pieceLen = (uint32_t) (pieceEnd - start);
      if (pieceLen > Available ())
        {
          // Genuine room exhaustion for unique data: with honest accounting this
          // means the sender overran its advertised credit.
          break;
        }
      QuicStreamRxItem *item = new QuicStreamRxItem ();
      item->m_packet = p->CreateFragment ((uint32_t) (start - sub.GetOffset ()), pieceLen);
      item->m_offset = start;
      item->m_fin = false;
      it = m_streamRecvList.insert (it, item);
      ++it;
      m_numBytesInBuffer += pieceLen;
      insertedBytes += pieceLen;
      start = pieceEnd;
    }

  if (insertedBytes > 0)
    {
      NS_LOG_INFO ("Update: Received Size = " << m_numBytesInBuffer);
      return true;
    }
  NS_LOG_WARN ("Discarded fully-covered packet.");
  return false;
}

Ptr<Packet>
QuicStreamRxBuffer::Extract (uint32_t maxSize, uint64_t fromOffset)
{
  NS_LOG_FUNCTION (this << maxSize << fromOffset);

  NS_LOG_INFO (
    "Requested to extract " << maxSize << " bytes from offset " << fromOffset
                            << ", QuicStreamRxBuffer size = " << m_numBytesInBuffer);

  Ptr<Packet> outPkt = Create<Packet> ();
  uint64_t pos = fromOffset;
  uint32_t remaining = maxSize;

  // The list is sorted by offset. Walk from the head: discard items entirely
  // below the extraction point (already-delivered stale retransmissions),
  // head-trim items straddling it (overlapping retransmissions), and merge
  // in-order bytes until the gap or the requested size.
  while (remaining > 0 && !m_streamRecvList.empty ())
    {
      QuicStreamRxPacketList::iterator it = m_streamRecvList.begin ();
      QuicStreamRxItem *item = *it;
      uint64_t itemEnd = item->m_offset + item->m_packet->GetSize ();

      if (itemEnd <= pos)
        {
          // Stale: everything in this item was already delivered
          NS_LOG_LOGIC ("Dropping stale packet at offset " << item->m_offset);
          m_numBytesInBuffer -= item->m_packet->GetSize ();
          delete item;
          m_streamRecvList.erase (it);
          continue;
        }
      if (item->m_offset > pos)
        {
          // Gap: nothing more deliverable
          break;
        }

      // item->m_offset <= pos < itemEnd: deliverable (possibly after head-trim)
      uint32_t trim = (uint32_t) (pos - item->m_offset);
      uint32_t usable = item->m_packet->GetSize () - trim;
      if (usable > remaining)
        {
          // Caller asked for less than this item holds; deliver whole items only
          break;
        }
      Ptr<Packet> part = item->m_packet;
      if (trim > 0)
        {
          part = item->m_packet->Copy ();
          part->RemoveAtStart (trim);
        }
      outPkt->AddAtEnd (part);
      pos += usable;
      remaining -= usable;
      m_numBytesInBuffer -= item->m_packet->GetSize ();
      NS_LOG_LOGIC ("Extracted packet at offset " << item->m_offset
                    << " (trimmed " << trim << "), new pos " << pos);
      delete item;
      m_streamRecvList.erase (it);
    }

  if (outPkt->GetSize () == 0)
    {
      NS_LOG_INFO ("Nothing extracted.");
      return 0;
    }

  return outPkt;
}

std::pair<uint64_t, uint64_t>
QuicStreamRxBuffer::GetDeliverable (uint64_t currRecvOffset)
{
  NS_LOG_FUNCTION (this);
  uint64_t chainPos = currRecvOffset;
  NS_LOG_LOGIC ("Calculating deliverable size");

  QuicStreamRxPacketList::iterator i;

  // The list is sorted by offset and may contain stale or overlapping items
  // (retransmissions re-chunked at different boundaries). Chain through every
  // item whose range touches the current position, advancing by its tail.
  for (i = m_streamRecvList.begin (); i != m_streamRecvList.end (); ++i)
    {
      uint64_t itemEnd = (*i)->m_offset + (*i)->m_packet->GetSize ();
      if (itemEnd <= chainPos)
        {
          // Stale item, already delivered
          continue;
        }
      if ((*i)->m_offset > chainPos)
        {
          // Gap: chain ends
          break;
        }
      chainPos = itemEnd;
      NS_LOG_LOGIC ("Chained packet with offset " << (*i)->m_offset);
    }

  return std::make_pair (currRecvOffset, chainPos - currRecvOffset);
}

uint32_t
QuicStreamRxBuffer::Size (void) const
{
  NS_LOG_FUNCTION (this);

  // uint32_t inFlight = 0;
  // for (auto recv_it = m_streamRecvList.begin (); recv_it != m_streamRecvList.end () and !m_streamRecvList.empty (); ++recv_it)
  //   {
  //     inFlight += (*recv_it)->m_packet->GetSize ();
  //   }

  return m_numBytesInBuffer;
}

uint32_t
QuicStreamRxBuffer::Available (void) const
{
  return m_maxBuffer - m_numBytesInBuffer;
}

uint32_t
QuicStreamRxBuffer::GetMaxBufferSize (void) const
{
  return m_maxBuffer;
}

void
QuicStreamRxBuffer::SetMaxBufferSize (uint32_t s)
{
  m_maxBuffer = s;
}

uint32_t
QuicStreamRxBuffer::GetFinalSize () const
{
  return m_finalSize;
}

void
QuicStreamRxBuffer::Print (std::ostream & os) const
{
  NS_LOG_FUNCTION (this);
  QuicStreamRxBuffer::QuicStreamRxPacketList::const_iterator it;
  std::stringstream ss;
  std::stringstream as;

  for (it = m_streamRecvList.begin (); it != m_streamRecvList.end (); ++it)
    {
      (*it)->Print (ss);
    }

  os << "Stream Recv list: \n" << ss.str () << "\n\nCurrent Status: "
     << "\nNumber of receptions = " << m_streamRecvList.size ()
     << "\nReceived Size = " << m_numBytesInBuffer;
  if (m_recvFin)
    {
      os << "\nFinal Size = " << m_finalSize;

    }

}

} //namepsace ns3
