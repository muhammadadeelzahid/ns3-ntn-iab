
 /* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
 /*
 *   Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *   Copyright (c) 2015, NYU WIRELESS, Tandon School of Engineering, New York University
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *   Author: Marco Miozzo <marco.miozzo@cttc.es>
 *           Nicola Baldo  <nbaldo@cttc.es>
 *
 *   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu>
 *              Sourjya Dutta <sdutta@nyu.edu>
 *              Russell Ford <russell.ford@nyu.edu>
 *              Menglei Zhang <menglei@nyu.edu>
 *
 *   Modified by:
 *              Muhammad Adeel Zahid <zahidma@myumanitoba.ca>
 *                 Integrating NTNs & Multilayer support with IAB with MPEG-DASH video streaming with quic derived from signetlabdei/ns3-mmwave-iab, Mattia Sandri/ns3-ntn, signetlabdei/ns3-mmwave-hbf, signetlabdei/quic-ns-3 and ssjShirley/mpquic-ns3
 *                  
 */
#include <cstdint>
#include <ns3/buildings-module.h>
#include "ns3/log.h"
#include "ns3/mmwave-helper.h"
#include "ns3/lte-module.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/epc-enb-application.h"
#include "ns3/quic-module.h"
#include "ns3/quic-socket-base.h"
#include "ns3/quic-header.h"
#include "ns3/quic-bbr.h"
#include "ns3/dash-module.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <mutex>
#include <regex>
#include <set>
#include <limits>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MmWaveNtnIabQuicDash");

// Global file streams for each layer
std::ofstream quicTxFile, quicRxFile;
std::ofstream udpL4TxFile, udpL4RxFile;
std::ofstream ipv4L3TxFile, ipv4L3RxFile;
std::ofstream p2pTxFile, p2pRxFile;

// DASH trace files (similar to QuicServerRx.txt)
std::map<uint32_t, std::ofstream*> g_dashClientTxFiles;  // DASH client requests (Tx)
std::map<uint32_t, std::ofstream*> g_dashClientRxFiles;  // DASH client video received via socket
std::ofstream g_dashServerRxFile;  // DASH server requests received

// Counters for DASH
std::map<uint32_t, uint32_t> g_dashClientTxPackets;
std::map<uint32_t, uint64_t> g_dashClientTxBytes;
std::map<uint32_t, uint32_t> g_dashClientRxPackets;
std::map<uint32_t, uint64_t> g_dashClientRxBytes;
uint32_t g_dashServerRxPackets = 0;
uint64_t g_dashServerRxBytes = 0;
std::map<uint64_t, uint32_t> g_imsiToNodeId;

// IAB backhaul handover: map donor (satellite) cellId -> its NetDevice, and the IAB device,
// so the HandoverStart trace callback can retune the IAB-MT beamforming to the target donor.
std::map<uint16_t, Ptr<NetDevice>> g_donorByCellId;
Ptr<NetDevice> g_iabHoDevice;

// Captured at handover trigger so the later HandoverEndOk callback can migrate the IAB-MT's
// descendant UE bearers from the source donor to the target donor (inter-donor IAB migration).
// Modeled NTN handover-execution / sync delay [s]: extra interruption added on top of the intrinsic
// RA gap (TA/Doppler re-acquisition + core-network S1 path switch per 3GPP TR 38.821). Defers the
// descendant-UE migration so the interruption reaches the realistic LEO band (~50-300 ms). 0 = off.
double g_hoExecDelay = 0.0;
Ptr<NetDevice> g_hoSrcDonor;
Ptr<NetDevice> g_hoTgtDonor;
uint16_t g_hoOldIabRnti = 0;

// BBR CSV log file (shared across connections)
std::ofstream g_bbrStatsCsvFile;
std::mutex g_bbrStatsCsvMutex;

// Robust QUIC layer trace hookup (context-based, resilient to late socket creation)
uint32_t g_quicServerNodeId = std::numeric_limits<uint32_t>::max();
bool g_quicRxTraceHooked = false;
bool g_quicCwndTraceHooked = false;
bool g_quicRttTraceHooked = false;
// Keyed by a composite (nodeId<<32 | connId) so each QUIC connection gets its own trace file
// (a multi-UE server hosts one connection per UE; keying by nodeId alone would collide them).
std::map<uint64_t, Ptr<OutputStreamWrapper>> g_quicRxStreams;
std::map<uint64_t, Ptr<OutputStreamWrapper>> g_quicCwndStreams;
std::map<uint64_t, Ptr<OutputStreamWrapper>> g_quicRttStreams;
// Specific (node,socket,metric) trace paths already connected, so a periodic rescan can hook
// late-created sockets (clients start staggered) without double-connecting an already hooked
// source (which would duplicate every trace line).
std::set<std::string> g_hookedQuicPaths;

// Helper function to dump full packet in hex
void DumpPacketHex(std::ofstream& file, Ptr<const Packet> packet, const std::string& prefix)
{
  file << prefix << " Size=" << packet->GetSize() << " bytes" << std::endl;
  
  // Create a copy to avoid modifying the original packet
  Ptr<Packet> copy = packet->Copy();
  
  file << "Full packet hex dump:" << std::endl;
  
  uint8_t buffer[16];
  uint32_t offset = 0;
  
  while (copy->GetSize() > 0)
  {
    uint32_t bytesToRead = std::min(16u, (uint32_t)copy->GetSize());
    copy->CopyData(buffer, bytesToRead);
    
    // Print offset
    file << std::hex << std::setw(8) << std::setfill('0') << offset << ": ";
    
    // Print hex bytes
    for (uint32_t i = 0; i < 16; i++)
    {
      if (i < bytesToRead)
        file << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
      else
        file << "   ";
    }
    
    // Print ASCII representation
    file << " |";
    for (uint32_t i = 0; i < bytesToRead; i++)
    {
      char c = buffer[i];
      file << (isprint(c) ? c : '.');
    }
    file << "|" << std::endl;
    
    copy->RemoveAtStart(bytesToRead);
    offset += bytesToRead;
  }
  file << std::endl;
}

// DASH Client Tx Trace (when client sends segment request)
void DashClientTxTrace(uint32_t nodeId, Ptr<const Packet> packet)
{
  if (g_dashClientTxFiles.find(nodeId) == g_dashClientTxFiles.end())
  {
    std::string filename = "DashClientTx_Node_" + std::to_string(nodeId) + ".txt";
    g_dashClientTxFiles[nodeId] = new std::ofstream(filename.c_str());
    *g_dashClientTxFiles[nodeId] << "# DASH Client Node " << nodeId << " - Segment Requests Transmitted" << std::endl;
    *g_dashClientTxFiles[nodeId] << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes" << std::endl;
    g_dashClientTxPackets[nodeId] = 0;
    g_dashClientTxBytes[nodeId] = 0;
  }
  
  g_dashClientTxPackets[nodeId]++;
  g_dashClientTxBytes[nodeId] += packet->GetSize();
  
  *g_dashClientTxFiles[nodeId] << Simulator::Now().GetSeconds() << "\t"
                               << packet->GetSize() << "\t"
                               << g_dashClientTxPackets[nodeId] << "\t"
                               << g_dashClientTxBytes[nodeId] << std::endl;
}

// DASH Client Rx Trace (when client receives video segments - MPEG frames)
void DashClientRxTrace(uint32_t nodeId, Ptr<const Packet> packet)
{
  if (g_dashClientRxFiles.find(nodeId) == g_dashClientRxFiles.end())
  {
    std::string filename = "DashClientRx_Node_" + std::to_string(nodeId) + ".txt";
    g_dashClientRxFiles[nodeId] = new std::ofstream(filename.c_str());
    *g_dashClientRxFiles[nodeId] << "# DASH Client Node " << nodeId << " - Video Segments (MPEG frames) Received" << std::endl;
    *g_dashClientRxFiles[nodeId] << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes" << std::endl;
    g_dashClientRxPackets[nodeId] = 0;
    g_dashClientRxBytes[nodeId] = 0;
  }
  
  g_dashClientRxPackets[nodeId]++;
  g_dashClientRxBytes[nodeId] += packet->GetSize();
  
  *g_dashClientRxFiles[nodeId] << Simulator::Now().GetSeconds() << "\t"
                               << packet->GetSize() << "\t"
                               << g_dashClientRxPackets[nodeId] << "\t"
                               << g_dashClientRxBytes[nodeId] << std::endl;
}

// DASH Server Rx Trace (when server receives segment request)
void DashServerRxTrace(Ptr<const Packet> packet, const Address& from)
{
  if (!g_dashServerRxFile.is_open())
  {
    g_dashServerRxFile.open("DashServerRx.txt");
    g_dashServerRxFile << "# DASH Server - Segment Requests Received from All Clients" << std::endl;
    g_dashServerRxFile << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes\tFromIP\tFromPort" << std::endl;
  }
  
  g_dashServerRxPackets++;
  g_dashServerRxBytes += packet->GetSize();
  
  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  g_dashServerRxFile << Simulator::Now().GetSeconds() << "\t"
                     << packet->GetSize() << "\t"
                     << g_dashServerRxPackets << "\t"
                     << g_dashServerRxBytes << "\t"
                     << addr.GetIpv4() << "\t"
                     << addr.GetPort() << std::endl;
}

// QUIC Socket Base Tx callback
void QuicSocketTxCallback(Ptr<const Packet> packet, const QuicHeader& header, Ptr<const QuicSocketBase> socket)
{
  
  NS_LOG_UNCOND("QuicSocketTxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, packet_number: " << header.GetPacketNumber());
  
  // Log detailed packet information
  NS_LOG_UNCOND("QuicSocketTxCallback Packet details - Size: " << packet->GetSize() 
            << ", Header size: " << header.GetSerializedSize()
            << ", Payload size: " << (packet->GetSize() - header.GetSerializedSize()));
  
  if (!quicTxFile.is_open())
  {
    quicTxFile.open("quic_socket_tx.txt", std::ios::out);
    NS_LOG_UNCOND("QUIC SOCKET TX file opened");
  }
  DumpPacketHex(quicTxFile, packet, "QUIC_SOCKET_TX PacketNumber=" + std::to_string(header.GetPacketNumber().GetValue()));
  quicTxFile.flush();
}

// QUIC Socket Base Rx callback
void QuicSocketRxCallback(Ptr<const Packet> packet, const QuicHeader& header, Ptr<const QuicSocketBase> socket)
{
  
  NS_LOG_UNCOND("QuicSocketRxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, packet_number: " << header.GetPacketNumber());
  
  // Log detailed packet information
  NS_LOG_UNCOND("QuicSocketRxCallback Packet details - Size: " << packet->GetSize() 
            << ", Header size: " << header.GetSerializedSize()
            << ", Payload size: " << (packet->GetSize() - header.GetSerializedSize()));
  
  if (!quicRxFile.is_open())
  {
    quicRxFile.open("quic_socket_rx.txt", std::ios::out);
    NS_LOG_UNCOND("QUIC SOCKET RX file opened");
  }
  DumpPacketHex(quicRxFile, packet, "QUIC_SOCKET_RX PacketNumber=" + std::to_string(header.GetPacketNumber().GetValue()));
  quicRxFile.flush();
}

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldRtt.GetSeconds () << "\t" << newRtt.GetSeconds () << std::endl;
}

static void
Rx (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << p->GetSize() << std::endl;
}

static std::string
GetQuicTracePathPrefix(uint32_t nodeId)
{
  return (nodeId == g_quicServerNodeId) ? "./server" : "./client";
}

static Ptr<OutputStreamWrapper>
GetOrCreateQuicTraceStream(std::map<uint64_t, Ptr<OutputStreamWrapper>>& streamMap,
                           const std::string& metricName,
                           uint32_t nodeId, uint32_t connId)
{
  uint64_t key = ((uint64_t)nodeId << 32) | connId;
  auto it = streamMap.find(key);
  if (it != streamMap.end())
    {
      return it->second;
    }

  AsciiTraceHelper asciiTraceHelper;
  std::ostringstream fileName;
  // e.g. serverQUIC-cwnd-change2-conn0.txt: per node and per connection so multi-UE server
  // connections don't share a file. Node-only basename kept as the prefix for compatibility.
  fileName << GetQuicTracePathPrefix(nodeId) << "QUIC-" << metricName << nodeId
           << "-conn" << connId << ".txt";
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream(fileName.str().c_str());
  streamMap[key] = stream;
  return stream;
}

static void
Traces(uint32_t serverId, std::string pathVersion, std::string finalPart, uint32_t retryCount)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream fileCW;
  fileCW << pathVersion << "QUIC-cwnd-change"  << serverId << "" << finalPart;

  std::ostringstream fileRTT;
  fileRTT << pathVersion << "QUIC-rtt"  << serverId << "" << finalPart;

  std::ostringstream fileName;
  fileName << pathVersion << "QUIC-rx-data" << serverId << "" << finalPart;

  // Connect traces with retry logic - keep trying until sockets are created
  // This ensures traces connect even if QUIC handshake takes longer than expected
  
  // Connect Rx trace (use wildcard to match any socket)
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  bool rxConnected = Config::ConnectWithoutContextFailSafe (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));
  if (rxConnected)
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Connected Rx trace (wildcard)");
  }
  else
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Rx trace not available yet, will retry");
  }

  // Connect CongestionWindow trace (use socket 0 - standard for QUIC examples)
  std::ostringstream pathCW;
  pathCW << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  bool cwConnected = Config::ConnectWithoutContextFailSafe (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));
  if (cwConnected)
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Connected CongestionWindow trace (socket 0)");
  }
  else
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - CongestionWindow trace not available yet, will retry");
  }

  // Connect RTT trace (use socket 0 - standard for QUIC examples)
  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";
  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  bool rttConnected = Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));
  if (rttConnected)
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Connected RTT trace (socket 0)");
  }
  else
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - RTT trace not available yet, will retry");
  }
  
  // Retry logic: if any trace failed to connect and the retry budget is not exhausted, reschedule.
  const uint32_t MAX_RETRIES = 10;
  const Time RETRY_INTERVAL = MilliSeconds(100);
  
  if ((!rxConnected || !cwConnected || !rttConnected) && retryCount < MAX_RETRIES)
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Scheduling retry " << (retryCount + 1) 
                  << " of " << MAX_RETRIES << " in " << RETRY_INTERVAL.GetSeconds() << "s");
    Simulator::Schedule(RETRY_INTERVAL, &Traces, serverId, pathVersion, finalPart, retryCount + 1);
  }
  else if (retryCount >= MAX_RETRIES)
  {
    NS_LOG_WARN("Node " << serverId << " (" << pathVersion << ") - Max retries (" << MAX_RETRIES 
                << ") reached. Some traces may not be connected.");
  }
  else
  {
    NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - All QUIC traces connected successfully");
  }
}

// Parse node_id and conn_id (socket index) from Config path
static void ParseNodeAndConnFromContext(const std::string& context, uint32_t& nodeId, uint32_t& connId)
{
  nodeId = 0;
  connId = 0;
  std::regex nodeRegex("/NodeList/(\\d+)/");
  std::regex socketRegex("/SocketList/(\\d+)/");
  std::smatch m;
  if (std::regex_search(context, m, nodeRegex) && m.size() > 1)
    nodeId = static_cast<uint32_t>(std::stoul(m[1].str()));
  if (std::regex_search(context, m, socketRegex) && m.size() > 1)
    connId = static_cast<uint32_t>(std::stoul(m[1].str()));
}

static void
QuicRxTraceWithContext(std::string context, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  (void)q;
  (void)qsb;
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicRxStreams, "rx-data", nodeId, connId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << p->GetSize() << std::endl;
}

static void
QuicCwndTraceWithContext(std::string context, uint32_t oldCwnd, uint32_t newCwnd)
{
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicCwndStreams, "cwnd-change", nodeId, connId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
QuicRttTraceWithContext(std::string context, Time oldRtt, Time newRtt)
{
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicRttStreams, "rtt", nodeId, connId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldRtt.GetSeconds() << "\t" << newRtt.GetSeconds() << std::endl;
}

static void
ConnectQuicLayerTracesWithRetry(uint32_t retryCount)
{
  // A wildcard SocketList/* connect only hooks sockets that already exist at call time. With staggered
  // client starts (0.1 + i*0.25 s) hooking once would miss late connections. Instead, periodically
  // rescan every node's QUIC sockets and hook each (node,socket,metric) source exactly once (tracked
  // in g_hookedQuicPaths to avoid duplicate trace lines), for long enough to cover the last client's
  // handshake.
  const uint32_t MAX_RETRIES = 60;            // 60 x 250 ms = 15 s window (covers last client start)
  const Time RETRY_INTERVAL = MilliSeconds(250);
  const uint32_t MAX_SOCKETS_PER_NODE = 64;   // server hosts one QUIC socket per UE + a listener

  uint32_t nNodes = NodeList::GetNNodes();
  for (uint32_t n = 0; n < nNodes; ++n)
    {
      for (uint32_t s = 0; s < MAX_SOCKETS_PER_NODE; ++s)
        {
          std::ostringstream base;
          base << "/NodeList/" << n << "/$ns3::QuicL4Protocol/SocketList/" << s << "/QuicSocketBase/";

          const std::string cw = base.str() + "CongestionWindow";
          if (!g_hookedQuicPaths.count(cw)
              && Config::ConnectFailSafe(cw, MakeCallback(&QuicCwndTraceWithContext)))
            g_hookedQuicPaths.insert(cw);

          const std::string rtt = base.str() + "RTT";
          if (!g_hookedQuicPaths.count(rtt)
              && Config::ConnectFailSafe(rtt, MakeCallback(&QuicRttTraceWithContext)))
            g_hookedQuicPaths.insert(rtt);

          const std::string rx = base.str() + "Rx";
          if (!g_hookedQuicPaths.count(rx)
              && Config::ConnectFailSafe(rx, MakeCallback(&QuicRxTraceWithContext)))
            g_hookedQuicPaths.insert(rx);
        }
    }

  if (retryCount < MAX_RETRIES)
    {
      Simulator::Schedule(RETRY_INTERVAL, &ConnectQuicLayerTracesWithRetry, retryCount + 1);
    }
  else
    {
      NS_LOG_UNCOND("QUIC layer traces: hooked " << g_hookedQuicPaths.size()
                    << " trace sources across all sockets");
    }
}

// BBR stats trace callback - logs to CSV with node_id and conn_id (csvLine: time,btlBw,...,state)
static void QuicBbrStatsCsvCallback(std::string context, std::string csvLine)
{
  std::lock_guard<std::mutex> lock(g_bbrStatsCsvMutex);
  if (!g_bbrStatsCsvFile.is_open())
    {
      g_bbrStatsCsvFile.open("bbr_stats_QUIC.csv");
      g_bbrStatsCsvFile << "protocol,node_id,conn_id,time_s,btlBw_bps,rtProp_s,pacingGain,cwndGain,pacingRate_bps,targetCwnd,cwnd,bytesInFlight,state" << std::endl;
    }
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  g_bbrStatsCsvFile << "QUIC," << nodeId << "," << connId << "," << csvLine << std::endl;
}

void
ConnectionEstablishedTraceSink(uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    NS_LOG_UNCOND("Connecting IMSI: " << imsi << " to ConnectionEstablished trace");
    // Open the file in append mode to log data
    std::ofstream outFile("connection_established.txt", std::ios_base::app);
    if (!outFile.is_open())
    {
        NS_LOG_ERROR("Can't open output file!");
        return;
    }
    // Log IMSI, CellId, RNTI, and simulation time
    double currentTime = Simulator::Now().GetSeconds();
    outFile << "Time: " << currentTime << "s, UE IMSI: " << imsi 
            << ", connected to CellId: " << cellId 
            << ", RNTI: " << rnti;
    auto it = g_imsiToNodeId.find(imsi);
    if (it != g_imsiToNodeId.end())
    {
        Ptr<Node> node = NodeList::GetNode(it->second);
        Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
        outFile << ", NodeId: " << it->second
                << ", Pos=(" << pos.x << "," << pos.y << "," << pos.z << ")";
    }
    outFile << "\n";
    // Close the file
    outFile.close();
}

void PacketDropCallback(Ptr<const Packet> packet) {
  NS_LOG_UNCOND("PacketDropCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
}

// Custom packet trace callback to track buffer operations
void PacketBufferTraceCallback(Ptr<const Packet> packet) {
  
  NS_LOG_UNCOND("PacketBufferTraceCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
  
  // Log detailed buffer information
  NS_LOG_UNCOND("PacketBufferTraceCallback Buffer details - Size: " << packet->GetSize() 
            << ", Available: " << packet->GetSize());
}

void LogTime()
{
  NS_LOG_UNCOND("Simulator Time: " << Simulator::Now().GetSeconds());
  Simulator::Schedule(Seconds(0.25), &LogTime);
}

// Sample and print UE positions (std::cout so it is visible in the optimized build, where NS_LOG is
// stripped) to document the random UE mobility.
void DumpUePositions (NodeContainer ues)
{
  for (uint32_t i = 0; i < ues.GetN (); ++i)
    {
      Ptr<MobilityModel> m = ues.Get (i)->GetObject<MobilityModel> ();
      if (m)
        {
          Vector p = m->GetPosition ();
          std::cout << "[UE-POS] t=" << Simulator::Now ().GetSeconds () << " ue=" << ues.Get (i)->GetId ()
                    << " x=" << p.x << " y=" << p.y << std::endl;
        }
    }
}

// ============================================================================
// IAB backhaul handover (3GPP inter-donor IAB-MT migration, NTN elevation-CHO)
// ----------------------------------------------------------------------------
// Manually triggers re-parenting of the IAB-node's backhaul (its MT, an LteUeRrc)
// from the serving donor satellite to a target donor satellite via the standard
// X2 handover path (LteEnbRrc::SendHandoverRequest). No A3 measurement algorithm
// is used: per 3GPP TR 38.821, LEO NTN uses elevation/time-based Conditional
// Handover, so the trigger time is pre-scheduled at the elevation crossing.
// ============================================================================
void
IabHandoverStart (uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCellId)
{
  std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER START: IAB MT imsi=" << imsi << " rnti=" << rnti
            << " leaving cell " << cellId << " -> target physCell " << targetCellId << std::endl;

  // Retune the IAB-MT backhaul to the target donor so the non-contention random access to
  // the target cell can complete. The standard LteUeRrc handover does not update these
  // IAB-specific bindings, so mirror what AttachIabToClosestEnb does for the new donor:
  //  (a) SetBackhaulTargetEnb  -> beamforming/channel target,
  //  (b) backhaul PHY RegisterToEnb -> so the IAB-MT listens to the target cell and receives
  //      the RAR (otherwise it keeps listening to the source donor and the RA never completes).
  auto it = g_donorByCellId.find (targetCellId);
  if (g_iabHoDevice && it != g_donorByCellId.end ())
    {
      Ptr<MmWaveIabNetDevice> iab = g_iabHoDevice->GetObject<MmWaveIabNetDevice> ();
      Ptr<MmWaveEnbNetDevice> tgtDonor = it->second->GetObject<MmWaveEnbNetDevice> ();
      if (iab && tgtDonor)
        {
          iab->SetBackhaulTargetEnb (it->second);
          Ptr<MmWavePhyMacCommon> cfg = tgtDonor->GetPhy ()->GetConfigurationParameters ();
          iab->GetBackhaulPhy ()->RegisterToEnb (targetCellId, cfg);
          std::cout << "[IAB-HO]   retuned + registered IAB-MT backhaul PHY to donor cellId "
                    << targetCellId << std::endl;
        }
    }
  else
    {
      std::cout << "[IAB-HO]   WARN: no donor device for target cellId " << targetCellId
                << " - beamforming NOT retuned" << std::endl;
    }
}

// Migrate the IAB-MT's descendant UE bearers from the source donor to the target donor, so their
// downlink does not black-hole after the backhaul re-parents. Reads the relay state from the source
// donor's EpcEnbApplication and re-installs it on the target donor's, which then drives a real S1 path
// switch per UE (SGW/PGW re-tunnels each UE's downlink to the new donor). See
// EpcEnbApplication::Export/ImportIabDescendants.
void
MigrateIabDescendants (Ptr<NetDevice> srcDonor, Ptr<NetDevice> tgtDonor,
                       uint16_t oldIabRnti, uint16_t newIabRnti, uint64_t iabImsi)
{
  if (!srcDonor || !tgtDonor)
    {
      std::cout << "[MIG] ERROR: missing src/tgt donor at handover end - cannot migrate descendants" << std::endl;
      return;
    }
  Ptr<EpcEnbApplication> srcApp = srcDonor->GetNode ()->GetApplication (0)->GetObject<EpcEnbApplication> ();
  Ptr<EpcEnbApplication> tgtApp = tgtDonor->GetNode ()->GetApplication (0)->GetObject<EpcEnbApplication> ();
  if (!srcApp || !tgtApp)
    {
      std::cout << "[MIG] ERROR: could not retrieve donor EpcEnbApplication - descendants NOT migrated" << std::endl;
      return;
    }
  std::vector<EpcEnbApplication::IabDescendantContext> ctx = srcApp->ExportIabDescendants (oldIabRnti);
  tgtApp->ImportIabDescendants (newIabRnti, iabImsi, ctx);
}

void
IabHandoverEndOk (uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER END OK: IAB MT imsi=" << imsi
            << " now connected to cell " << cellId << " rnti=" << rnti << std::endl;

  // The IAB-MT backhaul has re-parented; now migrate its descendant UEs' data plane to the new donor.
  // If a handover-execution/sync delay is modeled, defer the migration by that amount: the descendant
  // downlink stays interrupted until TA re-acquisition + the S1 path switch complete (realistic NTN).
  if (g_hoExecDelay > 0.0)
    {
      std::cout << "[IAB-HO]   deferring descendant migration by hoExecDelay="
                << g_hoExecDelay * 1e3 << " ms (modeled NTN sync + path-switch interruption)" << std::endl;
      Simulator::Schedule (Seconds (g_hoExecDelay), &MigrateIabDescendants,
                           g_hoSrcDonor, g_hoTgtDonor, g_hoOldIabRnti, rnti, imsi);
    }
  else
    {
      MigrateIabDescendants (g_hoSrcDonor, g_hoTgtDonor, g_hoOldIabRnti, rnti, imsi);
    }
}

// Fired if an IAB backhaul handover FAILS (random access to the target donor never completed after
// the preamble retransmissions are exhausted). Emitted prominently so the campaign post-processing
// can detect/exclude runs with a failed handover (which would leave the IAB-MT's UEs black-holed).
void
IabHandoverEndError (uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER FAILED: IAB MT imsi=" << imsi
            << " could not complete RA to target (was leaving cell " << cellId << ", rnti=" << rnti
            << ") - downstream UEs may lose service for this run" << std::endl;
}

void
TriggerIabBackhaulHandover (Ptr<NetDevice> iabDev, Ptr<NetDevice> srcDonor, Ptr<NetDevice> tgtDonor)
{
  Ptr<MmWaveIabNetDevice> iab = iabDev->GetObject<MmWaveIabNetDevice> ();
  Ptr<MmWaveEnbNetDevice> src = srcDonor->GetObject<MmWaveEnbNetDevice> ();
  Ptr<MmWaveEnbNetDevice> tgt = tgtDonor->GetObject<MmWaveEnbNetDevice> ();
  NS_ASSERT_MSG (iab && src && tgt, "TriggerIabBackhaulHandover: null device(s)");

  uint16_t rnti = iab->GetBackhaulRrc ()->GetRnti ();
  uint16_t tgtCellId = tgt->GetCellId ();
  Ptr<LteEnbRrc> srcRrc = src->GetRrc ();

  std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds ()
            << "s: trigger IAB backhaul handover, MT rnti=" << rnti
            << " serving(backhaulRrc cellId)=" << iab->GetBackhaulRrc ()->GetCellId ()
            << " from donor cell " << src->GetCellId () << " -> target cell " << tgtCellId << std::endl;

  if (srcRrc->HasUeManager (rnti))
    {
      // Capture the (src donor, tgt donor, IAB-MT's pre-handover RNTI) so the HandoverEndOk callback
      // can migrate the descendant UE bearers once the IAB-MT's new RNTI on the target is known.
      g_hoSrcDonor = srcDonor;
      g_hoTgtDonor = tgtDonor;
      g_hoOldIabRnti = rnti;
      srcRrc->SendHandoverRequest (rnti, tgtCellId);
    }
  else
    {
      std::cout << "[IAB-HO] ERROR: no UeManager for IAB MT rnti " << rnti
                << " at source donor cell " << src->GetCellId ()
                << " (IAB not connected?) - handover NOT triggered" << std::endl;
    }
}

// Print the IAB-MT's current serving (backhaul) cell - direct evidence of re-parenting.
void
PrintIabServingCell (Ptr<NetDevice> iabDev, std::string tag)
{
  Ptr<MmWaveIabNetDevice> iab = iabDev->GetObject<MmWaveIabNetDevice> ();
  if (iab && iab->GetBackhaulRrc ())
    {
      std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds () << "s " << tag
                << ": IAB-MT backhaul RRC cellId=" << iab->GetBackhaulRrc ()->GetCellId ()
                << " rnti=" << iab->GetBackhaulRrc ()->GetRnti ()
                << " state=" << iab->GetBackhaulRrc ()->GetState () << std::endl;
    }
}


int
main (int argc, char *argv[])
{

  LogComponentEnable("MmWaveHelper", LOG_LEVEL_INFO);
  // LogComponentEnable("DashClient", LOG_LEVEL_INFO);
  // LogComponentEnable("DashServer", LOG_LEVEL_INFO);
  // LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);
  // LogComponentEnable("QuicStreamBase", LOG_LEVEL_ALL);
  
  // Optional QUIC socket logging.
  // LogComponentEnable("QuicSocketBase", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicStream", LOG_LEVEL_ALL);

  // Optional packet-level logging.
  // LogComponentEnable("Packet", LOG_LEVEL_DEBUG);
  // LogComponentEnable("UdpSocket", LOG_LEVEL_DEBUG);
  // LogComponentEnable("UdpL4Protocol", LOG_LEVEL_DEBUG);

    // LogComponentEnable("DashServer", LOG_LEVEL_INFO);
    // LogComponentEnable("HttpParser", LOG_LEVEL_INFO);
    // LogComponentEnable("QuicSocketTxBuffer", LOG_LEVEL_INFO);
    // LogComponentEnable("QuicSocketRxBuffer", LOG_LEVEL_INFO);
    // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicStreamBase", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicStream", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicCongestionControl", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicSocketBase", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicBbr", LOG_LEVEL_ALL); // Uncomment if QuicBbr has its own log component
  // LogComponentEnable("MpQuicScheduler", LOG_LEVEL_ALL);
  // LogComponentEnableAll (LOG_PREFIX_TIME);
  // LogComponentEnableAll (LOG_PREFIX_FUNC);
  // LogComponentEnableAll (LOG_PREFIX_NODE);
  // LogComponentEnable("EpcEnbApplication", LOG_LEVEL_LOGIC);
  // LogComponentEnable("MmWaveEnbMac", LOG_ALL);
  // LogComponentEnable("MmWaveUeMac", LOG_ALL);
  // LogComponentEnable("MmWaveUePhy", LOG_ALL);
  // LogComponentEnable("EpcIabApplication", LOG_ALL);
  // LogComponentEnable("MmWave3gppChannel", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWave3gppPropagationLossModel", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveHelper", LOG_LEVEL_FUNCTION);  
  // LogComponentEnable("EpcSgwPgwApplication", LOG_LEVEL_LOGIC);
  // LogComponentEnable("EpcMmeApplication", LOG_LEVEL_LOGIC);
  // LogComponentEnable("EpcUeNas", LOG_LEVEL_LOGIC);
  // LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
  // LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
  // LogComponentEnable("MmWavePaddedHbfMacScheduler", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveSpectrumPhy", ns3::LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveEnbPhy", ns3::LOG_LEVEL_INFO);
  // LogComponentEnable("MmWaveUePhy", ns3::LOG_LEVEL_INFO);
  // LogComponentEnable("MmWavePointToPointEpcHelper", LOG_LEVEL_LOGIC);
  // LogComponentEnable("EpcS1ap", LOG_LEVEL_LOGIC);
  // LogComponentEnable("EpcTftClassifier", LOG_LEVEL_LOGIC);
  // LogComponentEnable("EpcGtpuHeader", LOG_LEVEL_INFO);
  // LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("UdpClient", LOG_ALL);
  // LogComponentEnable("UdpServer", LOG_ALL);
  // LogComponentEnable("QuicClient", LOG_ALL);
  // LogComponentEnable("QuicServer", LOG_ALL);
  // LogComponentEnable("QuicSubheader", LOG_ALL);
  // LogComponentEnable("QuicSocket", LOG_ALL);
  // LogComponentEnable("QuicL4Protocol", LOG_ALL);
  // LogComponentEnable("UdpSocket", LOG_ALL);
  // LogComponentEnable("UdpL4Protocol", LOG_ALL);
  // LogComponentEnable("Ipv4L3Protocol", LOG_ALL);
  // LogComponentEnable("Ipv4RoutingProtocol", LOG_ALL);
  // LogComponentEnable("MmWaveEnbNetDevice", LOG_ALL);
  // LogComponentEnable("MmWaveUeNetDevice", LOG_ALL);
  // LogComponentEnable("MmWaveEnbPhy", LOG_ALL);
  // LogComponentEnable("MmWaveUePhy", LOG_ALL);
  // LogComponentEnable("MmWaveEnbMac", LOG_ALL);
  // LogComponentEnable("MmWaveUeMac", LOG_ALL);
  // LogComponentEnable("MmWaveIabNetDevice", LOG_LEVEL_DEBUG);
  // LogComponentEnable("MmWaveSpectrumPhy", LOG_LEVEL_INFO);
  // LogComponentEnable("mmWaveInterference", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveChunkProcessor", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveUePhy", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveChunkProcessor", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveEnbPhy", LOG_LEVEL_INFO);
  // LogComponentEnable("MmWavePhy", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("SingleModelSpectrumChannel", LOG_LEVEL_INFO);
  // LogComponentEnable("MultiModelSpectrumChannel", LOG_LEVEL_INFO);
  // LogComponentEnable("MmWaveMiErrorModel", LOG_LEVEL_LOGIC);
  // LogComponentEnable("MmWaveHelper", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveIabNetDevice", LOG_LEVEL_ALL);
  // LogComponentEnable("EpcIabApplication", LOG_LEVEL_ALL);
  // LogComponentEnable("EpcEnbApplication", LOG_LEVEL_ALL);
  // LogComponentEnable("EpcUeNas", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveSpectrumPhy", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWavePaddedHbfMacScheduler", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveUePhy", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveEnbPhy", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveEnbMac", LOG_LEVEL_ALL);
  // LogComponentEnable("LteRlcAm", LOG_LEVEL_ALL);
  // LogComponentEnable("LteRlcUm", LOG_LEVEL_ALL);
  // LogComponentEnable("LteRlcUmLowLat", LOG_LEVEL_ALL);
  // LogComponentEnable("LteUeMac", LOG_LEVEL_ALL);
  // LogComponentEnable("LteRlc", LOG_LEVEL_ALL);
  // LogComponentEnable("LteUeMac", LOG_LEVEL_ALL);
  // LogComponentEnable("LtePdcp", LOG_LEVEL_ALL);
  // LogComponentEnable("EpcUeNas", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWave3gppChannel", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWave3gppPropagationLossModel", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveUePhy", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveUeMac", LOG_LEVEL_ALL);
  // LogComponentEnable("LteEnbRrc", LOG_LEVEL_ALL);
  // LogComponentEnable("LteUeRrc", LOG_LEVEL_ALL);

  // LogComponentDisableAll(LOG_LEVEL_ALL);
  
  // QUIC Layer
  // LogComponentEnable("QuicClient", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // //LogComponentEnable("Packet", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicServer", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicSocket", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicSocketBase", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicL4Protocol", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicL5Protocol", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicSubheader", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicHeader", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicStreamBase", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicStream", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
    
  // Enable additional QUIC classes that might be missing
  // LogComponentEnable("QuicStreamTxBuffer", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicStreamRxBuffer", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicSocketTxBuffer", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicSocketRxBuffer", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("QuicTransportParameters", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  
  // Enable UDP and IP layers for complete packet flow
  // LogComponentEnable("UdpSocket", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("UdpL4Protocol", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("Ipv4L3Protocol", LOG_LEVEL_FUNCTION);
  
  // UDP Layer
  // LogComponentEnable("UdpSocket", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("UdpL4Protocol", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("UdpSocketImpl", LOG_LEVEL_FUNCTION);
  
  // IP Layer
  // LogComponentEnable("Ipv4L3Protocol", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("Ipv4Interface", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("Ipv4RoutingProtocol", LOG_LEVEL_FUNCTION);
  
  // Traffic Control Layer
  // LogComponentEnable("TrafficControlLayer", LOG_LEVEL_FUNCTION);
  
  // LTE/EPC Layer
  // LogComponentEnable("EpcUeNas", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteUeRrc", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LtePdcp", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteRlc", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteRlcAm", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteRlcUm", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteRlcUmLowLat", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteUeMac", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("LteEnbRrc", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("EpcEnbApplication", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("EpcSgwPgwApplication", LOG_LEVEL_FUNCTION);
  
  // Physical Layer
  // LogComponentEnable("MmWaveEnbPhy", LOG_LEVEL_FUNCTION);
  // //LogComponentEnable("MmWaveUePhy", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveEnbMac", LOG_LEVEL_FUNCTION);
  // //LogComponentEnable("MmWaveUeMac", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveEnbNetDevice", LOG_LEVEL_FUNCTION);
  // //LogComponentEnable("MmWaveUeNetDevice", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("MmWaveSpectrumPhy", LOG_LEVEL_FUNCTION);
  
  // Network Devices
  // LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_FUNCTION);
  // LogComponentEnable("PointToPointChannel", LOG_LEVEL_FUNCTION);

  CommandLine cmd; 
  unsigned run = 0;
  bool rlcAm = false;
  uint32_t numRelays = 1;
  uint32_t numUes = 10;  // Number of UE nodes/users
  uint32_t numSatellites = 4;  // Number of donor satellites in the constellation (numSat-1 handovers)
  double hoTime = 10.0;        // Inter-handover interval [s]: handover k occurs at k*hoTime (0 = disabled)
  double simDuration = 60.0;   // Video/simulation duration [s] (shorten for fast handover iteration)
  double targetDt = 30.0;      // DASH target buffer [s] (lower => continuous requests, to test data-plane recovery)
  double maxBufferS = 0.0;     // Hard playback-buffer cap [s] (dash.js BufferController model; 0 = unlimited)
  std::string backhaulRate = "100Mbps";  // LEO satellite backhaul capacity (S1-U feeder rate). Realistic for a single
                                          // rural IAB (5G-NR-NTN Ka-band LEO ~100-300 Mbps; arXiv 2012.02136). Makes the
                                          // satellite backhaul the bottleneck so the handover transient is observable.
  double feederDelay = 0.010;  // LEO feeder/S1-U one-way link delay [s]. Default 10ms (optimistic). Realistic
                               // LEO feeder+service propagation is ~20-40ms one-way; raising it lengthens the
                               // handover interruption into the realistic NTN band and makes it visible.
  std::string abrAlgorithm = "ns3::FdashClient";  // DASH ABR controller: ns3::FdashClient or ns3::BolaClient
  bool enableTraces = false;   // Heavy RLC/MAC/PHY ASCII traces (DlRlcStats/RxPacketTrace ~12MB/run): off for the campaign
  uint32_t rlcBufSize = 50;  // RLC TX buffer [MB]; large enough to prevent overflows/drops on NTN links
  uint32_t interPacketInterval = 10000;
  uint32_t packetSize = 1400; // bytes; below MTU minus headers to avoid IP fragmentation
  bool ueMobility = true;      // UEs move randomly within a disc around the IAB (false = static placement)
  double ueSpeed = 1.5;        // UE random-waypoint speed [m/s] (pedestrian)
  double ueRadiusMax = 500.0;  // radius [m] of the circular boundary the UEs roam within, centred on the IAB
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);
  cmd.AddValue("numSat", "Number of donor satellites (>=2 enables backhaul handover)", numSatellites);
  cmd.AddValue("hoTime", "IAB backhaul handover trigger time [s] (0 = disabled)", hoTime);
  cmd.AddValue("simDuration", "Video/simulation duration [s] (shorten for fast handover iteration)", simDuration);
  cmd.AddValue("targetDt", "DASH target buffer [s] (low => continuous requests, tests data-plane recovery)", targetDt);
  cmd.AddValue("maxBufferS", "Hard playback-buffer cap [s] (models dash.js BufferController; 0 = unlimited)", maxBufferS);
  cmd.AddValue("backhaulRate", "LEO satellite backhaul capacity / S1-U feeder rate (e.g. 100Mbps)", backhaulRate);
  cmd.AddValue("feederDelay", "LEO feeder/S1-U one-way link delay [s] (default 0.010; realistic LEO ~0.02-0.04)", feederDelay);
  cmd.AddValue("hoExecDelay", "Modeled NTN handover-execution/sync delay [s] added to the interruption (TA re-acq + path switch; default 0; realistic ~0.02-0.07)", g_hoExecDelay);
  cmd.AddValue("abrAlgorithm", "DASH ABR algorithm TypeId (ns3::FdashClient or ns3::BolaClient)", abrAlgorithm);
  cmd.AddValue("traces", "Enable heavy RLC/MAC/PHY ASCII traces (slow; off for campaign)", enableTraces);
  cmd.AddValue("ueMobility", "UEs move randomly within a disc around the IAB (false = static placement)", ueMobility);
  cmd.AddValue("ueSpeed", "UE random-waypoint speed [m/s]", ueSpeed);
  cmd.AddValue("ueRadiusMax", "Radius [m] of the UE mobility boundary around the IAB", ueRadiusMax);

  // Config::SetDefault("ns3::MmWavePhyMacCommon::UlSchedDelay", UintegerValue(1));
  // RLC buffer sizing to prevent buffer overflow on NTN links.
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
  // Config::SetDefault ("ns3::LteRlcAm::PollRetransmitTimer", TimeValue(MilliSeconds(1.0)));
  // Config::SetDefault ("ns3::LteRlcAm::ReorderingTimer", TimeValue(MilliSeconds(2.0)));
  // Config::SetDefault ("ns3::LteRlcAm::StatusProhibitTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::LteRlcUm::ReportBufferStatusTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::MmWavePhyMacCommon::SubcarriersPerChunk", UintegerValue (12));
  
  Config::SetDefault ("ns3::MmWavePhyMacCommon::ChunkWidth", DoubleValue (1.389e6)); 

  // Keep default ChunkPerRB = 72 and ResourceBlockNum = 1 (required for TDMA).

	Config::SetDefault ("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue (2));
// 	//Config::SetDefault ("ns3::MmWaveBeamforming::LongTermUpdatePeriod", TimeValue (MilliSeconds (100.0)));
// 	Config::SetDefault ("ns3::LteEnbRrc::SystemInformationPeriodicity", TimeValue (MilliSeconds (5.0)));
// //	Config::SetDefault ("ns3::MmWavePropagationLossModel::ChannelStates", StringValue ("n"));
// 	Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
//   Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
//   Config::SetDefault ("ns3::LteRlcUm::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
  
//   Config::SetDefault ("ns3::LteRlcUmLowLat::ReorderingTimeExpires", TimeValue (MilliSeconds (10.0)));
//   Config::SetDefault ("ns3::LteRlcUm::ReorderingTimer", TimeValue (MilliSeconds (10.0)));
// 	Config::SetDefault ("ns3::LteRlcAm::ReorderingTimer", TimeValue (MilliSeconds (10.0)));
  
//   Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (10 * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
//   Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (10 * 1024 * 1024));
//   Config::SetDefault ("ns3::MmWavePaddedHbfMacScheduler::HarqEnabled", BooleanValue (true));
//   Config::SetDefault ("ns3::MmWavePaddedHbfMacScheduler::CqiTimerThreshold", UintegerValue (100));

  Config::SetDefault ("ns3::MmWaveHelper::RlcAmEnabled", BooleanValue(rlcAm));
  // Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::CqiTimerThreshold", UintegerValue(100));
  Config::SetDefault("ns3::MmWaveHelper::PathlossModel", StringValue("ns3::MmWave3gppPropagationLossModel"));
  //Config::SetDefault("ns3::MmWaveHelper::PathlossModel", StringValue("ns3::FriisPropagationLossModel"));  
  //Config::SetDefault("ns3::MmWaveHelper::ChannelModel", StringValue("ns3::MmWaveChannelRaytracing"));
  Config::SetDefault("ns3::MmWaveHelper::ChannelModel", StringValue("ns3::MmWave3gppChannel"));
  Config::SetDefault("ns3::MmWave3gppPropagationLossModel::NTNScenario", StringValue("Rural"));
  //Config::SetDefault("ns3::MmWave3gppPropagationLossModel::Scenario", StringValue("RMa"));
  
  // QUIC-specific configuration.
  // For a persistent video-streaming session the connection must outlive buffer-full idle periods.
  // If the idle timeout were near the DASH target buffer, a client that filled its playback buffer
  // would let the connection go idle and hit the idle-timeout Close, forcing a reconnect (+ handshake)
  // when the buffer later drains, stalling playback and churning sockets. Set the idle timeout well
  // beyond the whole simulation (a real client would send keepalives).
  Config::SetDefault("ns3::QuicSocketBase::IdleTimeout", TimeValue(Seconds(simDuration + 120.0)));
  
  // ============================================================================
  // ACKNOWLEDGMENT / LOSS-DETECTION PARAMETERS
  // ============================================================================
  // Tuned to reduce ACK gaps and improve loss-detection responsiveness while
  // remaining realistic for NTN.

  // Maximum tracked gaps reported in ACK frames. NTN links may have burst losses,
  // so tracking more gaps improves loss detection.
  Config::SetDefault("ns3::QuicSocketBase::MaxTrackedGaps", UintegerValue(100));

  // Maximum packets received before an ACK is sent. Small value grows the window quickly when the
  // initial window is smaller than the server's segment size. RFC 9000 allows ACK every packet.
  Config::SetDefault("ns3::QuicSocketState::kMaxPacketsReceivedBeforeAckSend", UintegerValue(2));

  // Delayed-ACK timeout. Faster ACKs let the congestion window grow quickly during large initial bursts.
  Config::SetDefault("ns3::QuicSocketState::kDelayedAckTimeout", TimeValue(MilliSeconds(50)));

  // ACK delay exponent (RFC 9000). Bounds the maximum encodable ACK delay; a small value reduces
  // delay variability and helps window growth.
  Config::SetDefault("ns3::QuicSocketBase::AckDelayExponent", UintegerValue(2));

  // Time-based loss detection toggle (RFC 9002). Provides a secondary loss-detection mechanism useful
  // under high reordering or variable RTT (common in NTN).
  Config::SetDefault("ns3::QuicSocketState::kUsingTimeLossDetection", BooleanValue(false));

  // Minimum TLP/RTO timeouts left at defaults (appropriate for NTN scenarios).
  // Config::SetDefault("ns3::QuicSocketState::kMinTLPTimeout", TimeValue(MilliSeconds(1)));
  // Config::SetDefault("ns3::QuicSocketState::kMinRTOTimeout", TimeValue(MilliSeconds(10)));


  // ============================================================================
  // LOSS DETECTION PARAMETERS (RFC 9002)
  // ============================================================================
  // Tuned to improve recovery of lost packets in NTN scenarios with high latency
  // and out-of-order delivery.

  // Max TLPs and reordering threshold (RFC 9002 Section 6.1.1). A low reordering threshold triggers
  // loss detection on the first out-of-order ACK, which helps recover tail losses when no further data
  // packets are available to generate gaps. Trade-off: may cause false positives, but improves recovery
  // in high-loss scenarios (NTN).
  Config::SetDefault("ns3::QuicSocketState::kMaxTLPs", UintegerValue(5));
  Config::SetDefault("ns3::QuicSocketState::kReorderingThreshold", UintegerValue(2));

  // Time-based reordering fraction (RFC 9002 Section 6.1.2). Standard value (9/8 = 1.125): a packet is
  // considered lost if it has been unacked for more than 1.125 * smoothed_rtt after a newer packet is acked.
  Config::SetDefault("ns3::QuicSocketBase::kTimeReorderingFraction", DoubleValue(9.0/8.0));

  // Default initial RTT (RFC 9002 Section 6.2.2). A low initial RTT lets the pacing-rate calculation
  // allow faster transmission before RTT samples are available (large initial segments).
  Config::SetDefault("ns3::QuicSocketBase::kDefaultInitialRtt", TimeValue(MilliSeconds(50)));
  
  // ============================================================================
  // CONGESTION CONTROL PARAMETERS
  // ============================================================================
  
  // QUIC Congestion Control Configuration
  std::string ccAlgorithm = "ns3::QuicBbr";
  cmd.AddValue("ccAlgorithm", "QUIC Congestion Control Algorithm (ns3::QuicBbr or ns3::QuicCongestionControl)", ccAlgorithm);
  cmd.Parse(argc, argv);

  // Set the socket type based on the command line argument
  Config::SetDefault("ns3::QuicL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(ccAlgorithm)));

  // QUIC BBR parameters - match TCP BBR for fair comparison
  if (ccAlgorithm.find("Bbr") != std::string::npos)
    {
      Config::SetDefault("ns3::QuicBbr::HighGain", DoubleValue(2.89));  // Match TcpBbr default
      Config::SetDefault("ns3::QuicBbr::BwWindowLength", UintegerValue(10));  // Match TcpBbr default
      Config::SetDefault("ns3::QuicBbr::RttWindowLength", TimeValue(Seconds(10)));  // Match TcpBbr default
      Config::SetDefault("ns3::QuicBbr::ProbeRttDuration", TimeValue(MilliSeconds(200)));  // Match TcpBbr default
    }
  // Config::SetDefault("ns3::QuicSocketBase::CcType", IntegerValue(QuicSocketBase::QuicNewReno)); // Use New Reno
  // Config::SetDefault("ns3::QuicL4Protocol::SocketType", TypeIdValue(QuicBbr::GetTypeId())); // Use BBR
  Config::SetDefault("ns3::QuicSocketBase::LegacyCongestionControl", BooleanValue(true));
  
  // Initial slow-start threshold (ssthresh).
  Config::SetDefault("ns3::QuicSocketBase::InitialSlowStartThreshold", UintegerValue(UINT32_MAX));

  // Packet size configuration
  Config::SetDefault("ns3::QuicSocketBase::InitialPacketSize", UintegerValue(packetSize));
  Config::SetDefault("ns3::QuicSocketBase::MaxPacketSize", UintegerValue(1500));
  
  // ============================================================================
  // FLOW CONTROL PARAMETERS
  // ============================================================================
  // Connection/stream flow-control window = 64 MB, matched to the 64 MB socket/stream buffers. A smaller
  // window (e.g. 4 MB) makes all flows block on connection flow control near-simultaneously and the sim
  // run dry; NewReno needs the larger window for multi-user operation. The BBR burst-storm is instead
  // bounded at its source by the pacing-rate ceiling in QuicBbr::SetPacingRate, which is independent of
  // the flow-control window size.
  Config::SetDefault("ns3::QuicSocketBase::MaxStreamData", UintegerValue(64 * 1024 * 1024));
  Config::SetDefault("ns3::QuicSocketBase::MaxData", UintegerValue(64 * 1024 * 1024));

  // ============================================================================
  // NOTE: Parameters Set in Constructors (Not Configurable via Config::SetDefault)
  // ============================================================================
  // The following parameters are set in constructors and cannot be changed via
  // Config::SetDefault. To modify these, you would need to edit the source code:
  //
  // 1. m_kLossReductionFactor (default: 0.5)
  //    Location: quic-socket-base.cc line 418
  //    Description: Reduction factor applied to congestion window on loss detection
  //    Impact: Decreasing makes congestion response more aggressive
  //
  // 2. m_initialCWnd (default: 10 * segmentSize)
  //    Location: quic-socket-base.cc line 436, mp-quic-subflow.cc line 102
  //    Description: Initial congestion window size
  //    Impact: Decreasing makes initial sending more conservative
  //
  // 3. m_kMinimumWindow (default: 2 * segmentSize)
  //    Location: quic-socket-base.cc line 416-417, mp-quic-subflow.cc line 103
  //    Description: Minimum congestion window size
  //    Impact: Increasing prevents window from getting too small after losses
  //
  // For MP-QUIC: These values are set when MpQuicSubFlow creates QuicSocketState
  // objects (see mp-quic-subflow.cc lines 76-79, 102-103)
 
  // Enable multi-beam functionality
  Config::SetDefault("ns3::MmWaveHelper::Scheduler", StringValue("ns3::MmWavePaddedHbfMacScheduler"));

  // Constrain the satellite backhaul to a realistic LEO capacity by rate-limiting the S1-U feeder
  // link between the donor (satellite) and the core. This makes the satellite backhaul the
  // end-to-end bottleneck (the access link and Internet segment stay fast), so the handover's brief
  // radio outage produces an observable congestion-window collapse and recovery. Default 100 Mbps is
  // representative of a single rural IAB LEO backhaul (5G-NR-NTN Ka-band ~100-300 Mbps; arXiv 2012.02136).
  Config::SetDefault("ns3::MmWavePointToPointEpcHelper::S1uLinkDataRate", DataRateValue(DataRate(backhaulRate)));
  // S1-U MTU must exceed the largest tunneled datagram, with headroom for GTP/UDP/IP overhead. A small
  // MTU (e.g. 2000) IP-fragments full-size QUIC data+ACK datagrams (~2399 B with tunnel headers) at the
  // PGW; ns-3 fragment reassembly can corrupt bytes at the fragment seam, causing receiver parse
  // failures and ACKed-but-undelivered stream gaps. GTP transport networks use jumbo frames for this
  // reason; 9000 (jumbo) covers the largest datagrams including loss-lengthened ACK lists at 10 UEs.
  Config::SetDefault("ns3::MmWavePointToPointEpcHelper::S1uLinkMtu", UintegerValue(9000));
  NS_LOG_UNCOND("LEO backhaul (S1-U feeder) rate-limited to " << backhaulRate);
  
  RngSeedManager::SetSeed (1);
  RngSeedManager::SetRun (run);
  // Config::SetDefault ("ns3::MmWavePhyMacCommon::SymbolsPerSubframe", UintegerValue(240));
  // Config::SetDefault ("ns3::MmWavePhyMacCommon::SubframePeriod", DoubleValue(1000));
  // Config::SetDefault ("ns3::MmWavePhyMacCommon::SymbolPeriod", DoubleValue(1000/240));
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  Ptr<MmWavePointToPointEpcHelper>  epcHelper = CreateObject<MmWavePointToPointEpcHelper> ();
  mmwaveHelper->SetEpcHelper (epcHelper);
  mmwaveHelper->Initialize();
  
  // Add bandwidth verification
  Ptr<MmWavePhyMacCommon> phyMacConfig = mmwaveHelper->GetPhyMacConfigurable();
  NS_LOG_UNCOND("=== BANDWIDTH VERIFICATION ===");
  NS_LOG_UNCOND("ChunkWidth: " << phyMacConfig->GetChunkWidth() / 1e6 << " MHz");
  NS_LOG_UNCOND("ChunkPerRB: " << phyMacConfig->GetNumChunkPerRb());
  NS_LOG_UNCOND("ResourceBlockNum: " << phyMacConfig->GetNumRb());
  NS_LOG_UNCOND("Total Bandwidth: " << (phyMacConfig->GetChunkWidth() * phyMacConfig->GetNumChunkPerRb() * phyMacConfig->GetNumRb()) / 1e6 << " MHz");
  NS_LOG_UNCOND("Center Frequency: " << phyMacConfig->GetCenterFrequency() / 1e9 << " GHz");
  NS_LOG_UNCOND("================================");
  
  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults();
  // parse again so you can override default values from the command line
  cmd.Parse(argc, argv);
  NS_LOG_UNCOND("Inter-packet interval: "<<interPacketInterval<<" us, Packet size: "<<packetSize<<" bytes");
 
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  // Create a single RemoteHost
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  
  // Install QUIC stack on remote host (instead of Internet stack)
  QuicHelper quicHelper;

  quicHelper.InstallQuic (remoteHostContainer);
  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (9000));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (feederDelay)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);  // Needed for DASH clients
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  double xMax = 1000.0;
  double yMax = xMax;

  // Altitudes
  double gnbHeight = 550000.0;
  double iabHeight = 10.0;

  // Offsets as fractions of total area (adjust as needed)
  double xOffset = 200;//xMax*0.36;  // ~30% from center to left/right
  double yOffset = 200;//yMax*0.40;  // ~30% from center to top/bottom
  //double gnbX = xMax/2.0;
  //double gnbY = yMax/2.0;
  // Center Donor Node
  Vector posWired = Vector(xMax / 2.0, yMax / 2.0, gnbHeight);

  // Symmetric IAB positions
  Vector posIab1 = Vector(xMax / 2.0 , yMax / 2.0, iabHeight);        // Mid
  Vector posIab2 = Vector((xMax / 2.0) + xOffset, (yMax / 2.0) - yOffset, iabHeight);        // Bottom-right
  Vector posIab3 = Vector((xMax / 2.0) - xOffset, (yMax / 2.0) + yOffset, iabHeight);        // Top-left
  Vector posIab4 = Vector((xMax / 2.0) - xOffset, (yMax / 2.0) - yOffset, iabHeight);        // Bottom-left
  Vector posIab5 = Vector((xMax / 2.0) + xOffset, (yMax / 2.0) + yOffset, iabHeight);                  // Top-right
  Vector posIab6 = Vector((xMax / 2.0) + xOffset, (yMax / 2.0) + yOffset, iabHeight);                  // Top-right (alt)

  NS_LOG_UNCOND("wired " << posWired << 
              " iab1 " << posIab1 <<
              " iab2 " << posIab2 << 
              " iab3 " << posIab3 << 
              " iab4 " << posIab4 <<
              " iab5 "  << posIab5<<
              " iab6 "<<posIab6
              );
  
  NS_LOG_UNCOND("\n=== Creating Network Nodes ===");
  NS_LOG_UNCOND("Number of UEs to create: " << numUes);
  NS_LOG_UNCOND("Number of Relays to create: " << numRelays);
  
  NodeContainer ueNodes;
  NodeContainer enbNodes;
  NodeContainer iabNodes;
 
  enbNodes.Create(numSatellites);
  iabNodes.Create(numRelays);
  ueNodes.Create(numUes);
  
  NS_LOG_UNCOND("Actually created " << ueNodes.GetN() << " UE nodes");
  NS_LOG_UNCOND("Actually created " << iabNodes.GetN() << " IAB nodes");
  NS_LOG_UNCOND("Actually created " << enbNodes.GetN() << " eNB nodes");
  NS_LOG_UNCOND("================================\n");

  double desiredVideoDuration = simDuration;
  double stopTime = desiredVideoDuration;
  
  // Install WaypointMobilityModel for the satellite (eNB): start overhead and move in +X at 7.56 km/s
  // (Starlink 550 km circular-orbit ground speed: v = sqrt(mu/r), mu=398600 km^3/s^2, r=6921 km => 7.59 km/s).
  double satVelocity = 7560.0; // m/s
  
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::WaypointMobilityModel");
  enbmobility.Install (enbNodes);

  double minSimulationDuration = stopTime;

  // Space the donor satellites along the orbital track by satVelocity*hoTime, so a new donor reaches the
  // zenith above the IAB every hoTime seconds (the overhead-pass interval). With the realistic Starlink
  // single-plane values (v=7.56 km/s, hoTime=262 s) this gives ~1,980 km spacing, matching the ~22
  // satellites/plane in-plane spacing (circumference 2*pi*6921 km / 22). Handovers fire at the
  // equal-elevation CROSSOVER (midway between two donors being overhead), where both are at horizontal
  // distance satSpacing/2 = 990 km => elevation atan(550/990) ~= 29 deg (see the handover schedule below).
  double satSpacing = satVelocity * (hoTime > 0.0 ? hoTime : minSimulationDuration);
  for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
  {
      Ptr<WaypointMobilityModel> mob = enbNodes.Get(i)->GetObject<WaypointMobilityModel>();

      // Waypoint 1: Start at t=0. Donor 0 is at the zenith; later donors trail in -X.
      Vector pos1 = Vector(posWired.x - (double)i * satSpacing, posWired.y, posWired.z);
      mob->AddWaypoint(Waypoint(Seconds(0.0), pos1));

      // Waypoint 2: End at t=minSimulationDuration, having moved +X at satVelocity.
      Vector pos2 = Vector(pos1.x + (satVelocity * minSimulationDuration), posWired.y, posWired.z);
      mob->AddWaypoint(Waypoint(Seconds(minSimulationDuration), pos2));
  }

  if(numRelays > 0)
  { 
    Ptr<ListPositionAllocator> iabPositionAlloc = CreateObject<ListPositionAllocator> ();
    iabPositionAlloc->Add (posIab1);
    iabPositionAlloc->Add (posIab2);
    iabPositionAlloc->Add (posIab3);
    iabPositionAlloc->Add (posIab4);
    iabPositionAlloc->Add (posIab5);
    iabPositionAlloc->Add (posIab6);
    MobilityHelper iabmobility;
    iabmobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    iabmobility.SetPositionAllocator (iabPositionAlloc);
    iabmobility.Install (iabNodes);
  }

  MobilityHelper uemobility;
  Ptr<ListPositionAllocator> uePosAlloc = CreateObject<ListPositionAllocator>();

  // Place UEs evenly across IAB clusters, random within a circle around each IAB
  std::vector<Vector> allIabCenters = { posIab1, posIab2, posIab3, posIab4, posIab5, posIab6 };
  std::vector<Vector> clusterCenters;
  uint32_t iabCentersToUse = std::min(numRelays, static_cast<uint32_t>(allIabCenters.size()));
  for (uint32_t i = 0; i < iabCentersToUse; ++i)
  {
    clusterCenters.push_back(allIabCenters[i]);
  }
  if (clusterCenters.empty())
  {
    clusterCenters.push_back(posWired); // fallback when no IABs are created
  }

  uint32_t totalUes = ueNodes.GetN();
  uint32_t clusterCount = clusterCenters.size();
  uint32_t baseUesPerCluster = totalUes / clusterCount;
  uint32_t extraUes = totalUes % clusterCount;

  double zHeight = 1.7;

  if (ueMobility)
  {
    // UEs move randomly within a disc of radius ueRadiusMax (default 500 m) centred on their IAB, at a
    // pedestrian random-waypoint speed (ueSpeed). Implemented with WaypointMobilityModel: each UE gets a
    // pre-computed random-waypoint track (uniform-area points inside the disc, straight legs at ueSpeed),
    // so the mmWave channel sees real UE motion/Doppler. Deterministic per RngRun, matching the TCP
    // scratch so the paired TCP-vs-QUIC comparison uses the same UE tracks.
    NS_LOG_UNCOND("UE mobility: random-waypoint within " << ueRadiusMax << " m disc, speed " << ueSpeed << " m/s");
    uemobility.SetMobilityModel ("ns3::WaypointMobilityModel");
    uemobility.Install (ueNodes);

    Ptr<UniformRandomVariable> uni = CreateObject<UniformRandomVariable>();
    uni->SetAttribute("Min", DoubleValue(0.0));
    uni->SetAttribute("Max", DoubleValue(1.0));

    uint32_t ueIdx = 0;
    for (uint32_t c = 0; c < clusterCenters.size(); ++c)
    {
      uint32_t uesInCluster = baseUesPerCluster + (c < extraUes ? 1 : 0);
      const Vector& center = clusterCenters[c];
      for (uint32_t u = 0; u < uesInCluster && ueIdx < ueNodes.GetN(); ++u, ++ueIdx)
      {
        Ptr<WaypointMobilityModel> mob = ueNodes.Get(ueIdx)->GetObject<WaypointMobilityModel>();
        // uniform-area random start point inside the disc, clamped to the scene
        double r0 = ueRadiusMax * std::sqrt(uni->GetValue());
        double a0 = 2.0 * M_PI * uni->GetValue();
        double cx = std::min(std::max(center.x + r0 * std::cos(a0), 0.0), xMax);
        double cy = std::min(std::max(center.y + r0 * std::sin(a0), 0.0), yMax);
        double t = 0.0;
        mob->AddWaypoint(Waypoint(Seconds(t), Vector(cx, cy, zHeight)));
        // random-waypoint legs until the sim end (last leg may extend past the end - node keeps moving)
        while (t < stopTime)
        {
          double r = ueRadiusMax * std::sqrt(uni->GetValue());
          double a = 2.0 * M_PI * uni->GetValue();
          double nx = std::min(std::max(center.x + r * std::cos(a), 0.0), xMax);
          double ny = std::min(std::max(center.y + r * std::sin(a), 0.0), yMax);
          double d = std::sqrt((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy));
          double dt = (ueSpeed > 0.0) ? d / ueSpeed : stopTime;
          if (dt < 1e-3) dt = 1e-3;  // avoid zero-duration legs
          t += dt;
          mob->AddWaypoint(Waypoint(Seconds(t), Vector(nx, ny, zHeight)));
          cx = nx; cy = ny;
        }
      }
    }
  }
  else
  {
    // Static placement (regression / handover-isolation baseline): UEs fixed at random points 1-100 m
    // from the IAB.
    Ptr<UniformRandomVariable> radiusRand = CreateObject<UniformRandomVariable>();
    radiusRand->SetAttribute("Min", DoubleValue(1.0));
    radiusRand->SetAttribute("Max", DoubleValue(100.0));
    Ptr<UniformRandomVariable> angleRand = CreateObject<UniformRandomVariable>();
    angleRand->SetAttribute("Min", DoubleValue(0.0));
    angleRand->SetAttribute("Max", DoubleValue(2 * M_PI));
    for (uint32_t c = 0; c < clusterCenters.size(); ++c)
    {
      uint32_t uesInCluster = baseUesPerCluster + (c < extraUes ? 1 : 0);
      const Vector& center = clusterCenters[c];
      for (uint32_t u = 0; u < uesInCluster; ++u)
      {
        double r = radiusRand->GetValue();
        double theta = angleRand->GetValue();
        double x = std::min(std::max(center.x + r * std::cos(theta), 0.0), xMax);
        double y = std::min(std::max(center.y + r * std::sin(theta), 0.0), yMax);
        uePosAlloc->Add(Vector(x, y, zHeight));
      }
    }
    uemobility.SetPositionAllocator (uePosAlloc);
    uemobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    uemobility.Install (ueNodes);
  }
  
  // Install mmWave Devices to the nodes
  NetDeviceContainer enbmmWaveDevs = mmwaveHelper->InstallSatelliteEnbDevice (enbNodes);
  NetDeviceContainer iabmmWaveDevs;
  if(numRelays > 0)
  {
    iabmmWaveDevs = mmwaveHelper->InstallIabDevice (iabNodes);
  }
  NetDeviceContainer uemmWaveDevs = mmwaveHelper->InstallUeDevice (ueNodes);
  // Install QUIC stack on UE nodes (instead of Internet stack)
  quicHelper.InstallQuic (ueNodes);

  // Build IMSI -> NodeId map (used when resolving RNTI/IMSI in trace callbacks).
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    Ptr<Node> ueNode = ueNodes.Get (u);
    for (uint32_t d = 0; d < ueNode->GetNDevices (); ++d)
    {
      Ptr<MmWaveUeNetDevice> ueDev = ueNode->GetDevice (d)->GetObject<MmWaveUeNetDevice> ();
      if (ueDev)
      {
        g_imsiToNodeId[ueDev->GetImsi ()] = ueNode->GetId ();
        break;
      }
    }
  }
  
  // Assign IP addresses to UEs using EPC helper
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (uemmWaveDevs));
  
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }
  NetDeviceContainer possibleBaseStations(enbmmWaveDevs, iabmmWaveDevs);
  NS_LOG_UNCOND("number of IAB devs " << iabmmWaveDevs.GetN() << " num of possibleBaseStations " 
    << possibleBaseStations.GetN());

    if(numRelays > 0)
  {
    mmwaveHelper->AttachIabToClosestSatelliteEnb (iabmmWaveDevs, enbmmWaveDevs);
  }
  mmwaveHelper->AttachToClosestEnb (uemmWaveDevs, possibleBaseStations);

  // Set up X2 interfaces between donor satellites so the IAB backhaul can hand over
  // between them (3GPP inter-donor IAB-MT migration over X2).
  if (enbmmWaveDevs.GetN () > 1)
  {
    mmwaveHelper->AddX2Interface (enbNodes);
    std::cout << "[IAB-HO] X2 interfaces set up between " << enbNodes.GetN() << " donor satellites" << std::endl;
  }

  // Build the donor cellId -> device map (used by the HandoverStart callback to retune
  // the IAB-MT beamforming target) and connect handover traces on the IAB backhaul RRC.
  for (uint32_t s = 0; s < enbmmWaveDevs.GetN (); ++s)
  {
    Ptr<MmWaveEnbNetDevice> donor = enbmmWaveDevs.Get (s)->GetObject<MmWaveEnbNetDevice> ();
    if (donor)
    {
      g_donorByCellId[donor->GetCellId ()] = enbmmWaveDevs.Get (s);
      std::cout << "[IAB-HO] donor " << s << " cellId=" << donor->GetCellId () << std::endl;
    }
  }
  if (numRelays > 0)
  {
    g_iabHoDevice = iabmmWaveDevs.Get (0);
    Ptr<MmWaveIabNetDevice> iab0 = iabmmWaveDevs.Get (0)->GetObject<MmWaveIabNetDevice> ();
    if (iab0 && iab0->GetBackhaulRrc ())
    {
      iab0->GetBackhaulRrc ()->TraceConnectWithoutContext ("HandoverStart", MakeCallback (&IabHandoverStart));
      iab0->GetBackhaulRrc ()->TraceConnectWithoutContext ("HandoverEndOk", MakeCallback (&IabHandoverEndOk));
      iab0->GetBackhaulRrc ()->TraceConnectWithoutContext ("HandoverEndError", MakeCallback (&IabHandoverEndError));
    }
  }

  // Schedule a CHAIN of IAB backhaul handovers across the satellite constellation, modelling a LEO pass
  // with periodic inter-satellite backhaul handovers. Donor k reaches the zenith above the IAB at
  // t = k*hoTime (by the satSpacing construction above); handover k (k = 1..numSat-1) fires at the
  // EQUAL-ELEVATION CROSSOVER t = (k-0.5)*hoTime, when the setting donor k-1 and the rising donor k are at
  // the same elevation (~29 deg for the realistic single-plane params) - the physically natural handover
  // instant, vs. handing over at the target's zenith. The trigger is time/ephemeris-scheduled (deterministic
  // LEO ephemeris, per 3GPP TR 38.821 NTN Conditional Handover), not a runtime-measured elevation threshold.
  if (hoTime > 0.0 && enbmmWaveDevs.GetN () > 1 && numRelays > 0)
  {
    for (uint32_t k = 1; k < enbmmWaveDevs.GetN (); ++k)
    {
      double t = ((double)k - 0.5) * hoTime;
      Simulator::Schedule (Seconds (t), &TriggerIabBackhaulHandover,
                           iabmmWaveDevs.Get (0), enbmmWaveDevs.Get (k - 1), enbmmWaveDevs.Get (k));
      // Probe the IAB-MT serving cell shortly after each handover to confirm completion.
      Simulator::Schedule (Seconds (t + 0.5), &PrintIabServingCell, iabmmWaveDevs.Get (0),
                           std::string ("HO") + std::to_string (k) + "+0.5");
      std::cout << "[IAB-HO] Scheduled handover " << k << " at t=" << t
                << "s (donor " << (k - 1) << " -> donor " << k << ")" << std::endl;
    }
  }

  // Map IMSI/RNTI to NodeId and position
  Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/LteEnbRrc/ConnectionEstablished",
                                MakeCallback(&ConnectionEstablishedTraceSink));

  // Install and start applications on UEs and remote host
  // LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
  // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  
  // Multi-user QUIC stability: clients are started with a stagger and the server starts before any
  // client, so concurrent QUIC handshakes do not overlap and exhaust the congestion window (which
  // otherwise leaves some UEs unable to establish a connection).

  // QUIC socket + stream buffers: 64 MB, matched to the TCP socket buffers (ns3::TcpSocket::Snd/RcvBufSize,
  // also 64 MB) for a fair comparison. At the realistic ~4.2 Mbps ladder, 64 MB is ~120 s of buffering per
  // layer and bounds worst-case RSS well under the SLURM memory cap; a much larger value can OOM at 10 UEs.
  Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(64*1024*1024));
  Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(64*1024*1024));
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (64*1024*1024));
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (64*1024*1024));

  // DASH over QUIC configuration.
  double target_dt = targetDt;  // Target buffering time [s] (CLI-configurable; a low value keeps DASH requesting continuously, useful to test data-plane recovery across a handover)
  // DASH playback buffer holding target_dt seconds of buffered video; 128 MB avoids frame rejection and
  // matches the TCP scratch.
  uint32_t bufferSpace = 128*1024*1024;  // 128 MB DASH frame buffer (matches TCP scratch)

  double window = 50;  // Throughput measurement window [ms]; larger window gives more stable measurements and smoother adaptation

  std::string algorithm = abrAlgorithm;  // DASH adaptation algorithm (--abrAlgorithm: FdashClient/BolaClient)
  


  // Create a DASH server on each UE (listening on port 80)
  DashServerHelper dashServer ("ns3::QuicSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), 80));
  serverApps.Add (dashServer.Install (remoteHost));
  NS_LOG_UNCOND("DASH Server installed on remoteHost (IP=" << remoteHostAddr << ") port 80");
  
  // Create DASH clients on each UE node (connecting to remoteHost server)
  // This simulates DOWNLINK: users download video from remote server
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    DashClientHelper dashClient ("ns3::QuicSocketFactory",
                                  InetSocketAddress(remoteHostAddr, 80),
                                  algorithm);
    dashClient.SetAttribute ("VideoId", UintegerValue(u + 1));
    dashClient.SetAttribute ("TargetDt", TimeValue(Seconds(target_dt)));
    dashClient.SetAttribute ("MaxBufferS", DoubleValue(maxBufferS));
    dashClient.SetAttribute ("window", TimeValue(MilliSeconds(window)));
    dashClient.SetAttribute ("bufferSpace", UintegerValue(bufferSpace));
    
    clientApps.Add (dashClient.Install (ueNodes.Get(u)));
    NS_LOG_UNCOND("DASH Client " << u << " installed on UE " << u << " (IP=" << ueIpIface.GetAddress(u) 
                  << ") -> server IP=" << remoteHostAddr << ":80");
  }
  
  // Connect DASH trace sources for clients (now on UE nodes - DOWNLINK)
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
    if (dashClient)
    {
      uint32_t nodeId = ueNodes.Get(u)->GetId();
      dashClient->TraceConnectWithoutContext("Tx", MakeBoundCallback(&DashClientTxTrace, nodeId));
      dashClient->TraceConnectWithoutContext("Rx", MakeBoundCallback(&DashClientRxTrace, nodeId));
      NS_LOG_UNCOND("Connected DASH Client Tx and Rx traces for UE " << u);
    }
  }
  
  // Connect server Rx traces for remoteHost server (DOWNLINK)
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    Ptr<Application> app = serverApps.Get(i);
    Ptr<DashServer> srv = DynamicCast<DashServer>(app);
    if (srv)
    {
      srv->TraceConnectWithoutContext("Rx", MakeCallback(&DashServerRxTrace));
      NS_LOG_UNCOND("Connected DASH Server Rx traces for remoteHost server");
    }
  }
  
  NS_LOG_UNCOND("\n=== Node Coordinates ===");
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
  {
    Ptr<Node> node = *it;
    uint32_t nodeId = node->GetId();
    Vector pos = node->GetObject<MobilityModel> () ? node->GetObject<MobilityModel> ()->GetPosition () : Vector (0,0,0);
    bool printed = false;
    int nDevs = node->GetNDevices ();
    for (int j = 0; j < nDevs; j++)
    {
      Ptr<LteUeNetDevice> uedev = node->GetDevice (j)->GetObject <LteUeNetDevice> ();
      Ptr<MmWaveUeNetDevice> mmuedev = node->GetDevice (j)->GetObject <MmWaveUeNetDevice> ();
      Ptr<McUeNetDevice> mcuedev = node->GetDevice (j)->GetObject <McUeNetDevice> ();
      Ptr<LteEnbNetDevice> enbdev = node->GetDevice (j)->GetObject <LteEnbNetDevice> ();
      Ptr<MmWaveEnbNetDevice> mmdev = node->GetDevice (j)->GetObject <MmWaveEnbNetDevice> ();
      Ptr<MmWaveIabNetDevice> mmIabdev = node->GetDevice (j)->GetObject <MmWaveIabNetDevice> ();
      if (uedev || mmuedev || mcuedev)
      {
        NS_LOG_UNCOND("UE Node ID: " << nodeId << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")");
        printed = true;
        break;
      }
      else if (enbdev || mmdev)
      {
        NS_LOG_UNCOND("ENB Node ID: " << nodeId << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")");
        printed = true;
        break;
      }
      else if (mmIabdev)
      {
        NS_LOG_UNCOND("IAB Node ID: " << nodeId << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")");
        printed = true;
        break;
      }
    }
    if (!printed)
    {
      // Optionally print other nodes
    }
  }
  NS_LOG_UNCOND("=======================\n");
    
  if (enableTraces) { mmwaveHelper->EnableTraces (); }  // Heavy RLC/MAC/PHY ASCII traces - off by default (campaign speed/disk)
  
  // Server starts early so it is listening before clients initiate their QUIC handshakes.
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after the server, staggered by 0.25 s each, so their QUIC handshakes (1-RTT over the
  // satellite backhaul) do not all collide at t=0.1 s. Simultaneous handshakes plus many concurrent
  // slow-starts can leave some UEs unable to establish. The per-client offset matches the TCP scratch
  // to keep the comparison fair.
  for (uint32_t i = 0; i < clientApps.GetN(); ++i)
  {
    double clientStartTime = 0.1 + i * 0.25;
    clientApps.Get(i)->SetStartTime(Seconds(clientStartTime));
    // Stop apps 1 second before simulation stops to allow cleanup
    clientApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
    NS_LOG_UNCOND("DASH Client " << i << " scheduled to start at t=" << clientStartTime << "s");
  }
  
  // Sample UE positions at 0/25/50/75% of the run so the log documents the random UE mobility.
  if (ueMobility)
    {
      for (int s = 0; s < 4; ++s)
        Simulator::Schedule (Seconds (0.25 * s * stopTime), &DumpUePositions, ueNodes);
    }

  Simulator::Stop (Seconds (stopTime + 2.0));

  NS_LOG_UNCOND("\n=== Scheduling QUIC Trace Connections (DOWNLINK) ===");
  
  // DOWNLINK: clients are on UE nodes, server is on remoteHost.
  // ---------------------------------------------------------------------------
  // QUIC cwnd/RTT/Rx trace hookup.
  // On the QUIC server, socket 0 is the listening socket (no congestion window);
  // the data connection is a higher socket index. Use the context-based wildcard
  // hookup (SocketList/*), which matches the data sockets and writes per-node
  // files (server<id>/client<id>), parsing the node id from the trace context.
  // Scheduled after the QUIC connections are established so the data sockets
  // exist at connect time.
  // ---------------------------------------------------------------------------
  g_quicServerNodeId = remoteHost->GetId();
  Simulator::Schedule(Seconds(0.5), &ConnectQuicLayerTracesWithRetry, 0);
  NS_LOG_UNCOND("  Scheduled context-based QUIC trace hookup (wildcard SocketList/*) at t=0.5s"
                << ", server node " << g_quicServerNodeId);

  // Schedule BBR stats trace connection for CSV logging
  Simulator::Schedule(Seconds(0.15), []() {
    Config::MatchContainer bbrMatches = Config::LookupMatches("/NodeList/*/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/CongestionOps/$ns3::QuicBbr");
    bbrMatches.ConnectFailSafe("BbrStatsTrace", MakeCallback(&QuicBbrStatsCsvCallback));
    if (bbrMatches.GetN() > 0)
      NS_LOG_UNCOND("  Connected BBR stats trace to " << bbrMatches.GetN() << " QuicBbr instance(s)");
  });
  
  // Add QUIC socket Tx/Rx callback connections.
  NS_LOG_UNCOND("\n=== Adding QUIC Socket Callback Connections ===");
  
  // Connect QUIC socket callbacks for all nodes
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
  {
    Ptr<Node> node = *it;
    uint32_t nodeId = node->GetId();
    
    // Connect QUIC socket Tx/Rx traces
    std::ostringstream quicTxPath;
    quicTxPath << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Tx";
    // Note: QuicSocketBase currently exposes Rx trace; Tx may be disabled. Guard by lookup.
    if (Config::LookupMatches(quicTxPath.str().c_str()).GetN() > 0)
      {
        Config::ConnectWithoutContextFailSafe(quicTxPath.str(), MakeCallback(&QuicSocketTxCallback));
      }
    
    std::ostringstream quicRxPath;
    quicRxPath << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
    Config::ConnectWithoutContextFailSafe(quicRxPath.str(), MakeCallback(&QuicSocketRxCallback));
    
    NS_LOG_UNCOND("  Added QUIC socket traces for Node " << nodeId);
  }
  
  // Add packet buffer monitoring traces
  NS_LOG_UNCOND("\n=== Adding Packet Buffer Monitoring Traces ===");

  std::string tracePrefix = "ntn_iab_quic_dash";  // Keep variable for log statements
  NS_LOG_UNCOND("\n=== Trace Configuration ===");
  NS_LOG_UNCOND("QUIC traces: Using quic-variants-comparison example approach");
  NS_LOG_UNCOND("DASH application traces: ENABLED");
  NS_LOG_UNCOND("RLC/MAC/PHY layer traces: ENABLED");
  NS_LOG_UNCOND("============================\n");
  
  NS_LOG_UNCOND("\n=== DASH over QUIC Simulation Parameters (DOWNLINK) ===");
  NS_LOG_UNCOND("Direction: DOWNLINK (Server on remoteHost, Clients on UE nodes)");
  NS_LOG_UNCOND("Number of UEs: " << ueNodes.GetN());
  NS_LOG_UNCOND("Simulation time: " << stopTime << " seconds");
  NS_LOG_UNCOND("DASH algorithm: " << algorithm);
  NS_LOG_UNCOND("Target buffering time: " << target_dt << " seconds");
  
  Simulator::Schedule(Seconds(0.25), &LogTime);
  Simulator::Run();
  
  // Print DASH statistics for each UE (DOWNLINK: clients are on UE nodes)
  NS_LOG_UNCOND("\n========== DASH over QUIC Results (DOWNLINK) ==========");
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
    if (dashClient)
    {
      uint32_t nodeId = ueNodes.Get(u)->GetId();
      NS_LOG_UNCOND("\nUE " << u << " (NodeId=" << nodeId << ", VideoId=" << (u+1) << ", DASH Client):");
      dashClient->GetStats();
      
      // DASH trace maps are keyed by nodeId (see DashClientTxTrace/DashClientRxTrace), not loop index u
      if (g_dashClientTxPackets.find(nodeId) != g_dashClientTxPackets.end())
      {
        NS_LOG_UNCOND("  DASH Requests sent: " << g_dashClientTxPackets[nodeId] 
                     << " packets (" << g_dashClientTxBytes[nodeId] << " bytes)");
      }
      if (g_dashClientRxPackets.find(nodeId) != g_dashClientRxPackets.end())
      {
        NS_LOG_UNCOND("  DASH Video received: " << g_dashClientRxPackets[nodeId] 
                     << " packets (" << g_dashClientRxBytes[nodeId] << " bytes)");
        double avgThroughput = (g_dashClientRxBytes[nodeId] * 8.0) / (stopTime * 1000000.0);
        NS_LOG_UNCOND("  Average throughput: " << avgThroughput << " Mbps");
      }
    }
  }
  
  NS_LOG_UNCOND("\nDASH Server Statistics:");
  NS_LOG_UNCOND("  Total requests received: " << g_dashServerRxPackets 
               << " packets (" << g_dashServerRxBytes << " bytes)");
  
  
  /*GtkConfigStore config;
  config.ConfigureAttributes();*/
  Simulator::Destroy();
  
  // Close all trace files
  if (quicTxFile.is_open()) quicTxFile.close();
  if (quicRxFile.is_open()) quicRxFile.close();
  if (udpL4TxFile.is_open()) udpL4TxFile.close();
  if (udpL4RxFile.is_open()) udpL4RxFile.close();
  if (ipv4L3TxFile.is_open()) ipv4L3TxFile.close();
  if (ipv4L3RxFile.is_open()) ipv4L3RxFile.close();
  if (p2pTxFile.is_open()) p2pTxFile.close();
  if (p2pRxFile.is_open()) p2pRxFile.close();
  
  // Close DASH trace files
  for (auto& pair : g_dashClientTxFiles)
  {
    if (pair.second && pair.second->is_open())
    {
      pair.second->close();
      delete pair.second;
    }
  }
  for (auto& pair : g_dashClientRxFiles)
  {
    if (pair.second && pair.second->is_open())
    {
      pair.second->close();
      delete pair.second;
    }
  }
  if (g_dashServerRxFile.is_open())
  {
    g_dashServerRxFile.close();
  }
  if (g_bbrStatsCsvFile.is_open())
  {
    g_bbrStatsCsvFile.close();
  }
    
  return 0;
}
