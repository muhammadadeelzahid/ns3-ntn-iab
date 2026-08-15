
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
#include <vector>
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
#include "ns3/tcp-socket-factory.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-bbr.h"
#include "ns3/dash-module.h"
#include <iomanip>
#include <fstream>
#include <sstream>
#include <mutex>
#include <regex>
#include <set>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MmWaveNtnIabTcpDash");

// Performance optimization: Set to false to disable expensive packet-level tracing/logging
// This significantly speeds up simulations
static bool g_enableVerbosePacketTracing = false;

// Global file streams for each layer
std::ofstream tcpTxFile, tcpRxFile;
std::ofstream udpL4TxFile, udpL4RxFile;
std::ofstream ipv4L3TxFile, ipv4L3RxFile;
std::ofstream p2pTxFile, p2pRxFile;

// BBR CSV log file (shared across connections)
std::ofstream g_bbrStatsCsvFile;
std::mutex g_bbrStatsCsvMutex;

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

// IAB backhaul handover: map donor (satellite) cellId -> its NetDevice, and the IAB device,
// so the HandoverStart trace callback can retune the IAB-MT beamforming to the target donor.
std::map<uint16_t, Ptr<NetDevice>> g_donorByCellId;
Ptr<NetDevice> g_iabHoDevice;

// Captured at handover trigger so the (later) HandoverEndOk callback can migrate the IAB-MT's
// descendant UE bearers from the source donor to the target donor (genuine inter-donor IAB migration).
Ptr<NetDevice> g_hoSrcDonor;
Ptr<NetDevice> g_hoTgtDonor;
uint16_t g_hoOldIabRnti = 0;

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
    std::string filename = "DashClientTx_TCP_Node_" + std::to_string(nodeId) + ".txt";
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
    std::string filename = "DashClientRx_TCP_Node_" + std::to_string(nodeId) + ".txt";
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
    g_dashServerRxFile.open("DashServerRx_TCP.txt");
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

// TCP Socket Base Tx callback
void TcpSocketTxCallback(Ptr<const Packet> packet, const TcpHeader& header, Ptr<const TcpSocketBase> socket)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  NS_LOG_UNCOND("TcpSocketTxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, sequence_number: " << header.GetSequenceNumber());
  
  // Log detailed packet information
  NS_LOG_UNCOND("TcpSocketTxCallback Packet details - Size: " << packet->GetSize() 
            << ", Header size: " << header.GetSerializedSize()
            << ", Payload size: " << (packet->GetSize() - header.GetSerializedSize()));
  
  if (!tcpTxFile.is_open())
  {
    tcpTxFile.open("tcp_socket_tx.txt", std::ios::out);
    NS_LOG_UNCOND("TCP SOCKET TX file opened");
  }
  DumpPacketHex(tcpTxFile, packet, "TCP_SOCKET_TX SequenceNumber=" + std::to_string(header.GetSequenceNumber().GetValue()));
  tcpTxFile.flush();
}

// TCP Socket Base Rx callback
void TcpSocketRxCallback(Ptr<const Packet> packet, const TcpHeader& header, Ptr<const TcpSocketBase> socket)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  NS_LOG_UNCOND("TcpSocketRxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, sequence_number: " << header.GetSequenceNumber());
  
  // Log detailed packet information
  NS_LOG_UNCOND("TcpSocketRxCallback Packet details - Size: " << packet->GetSize() 
            << ", Header size: " << header.GetSerializedSize()
            << ", Payload size: " << (packet->GetSize() - header.GetSerializedSize()));
  
  if (!tcpRxFile.is_open())
  {
    tcpRxFile.open("tcp_socket_rx.txt", std::ios::out);
    NS_LOG_UNCOND("TCP SOCKET RX file opened");
  }
  DumpPacketHex(tcpRxFile, packet, "TCP_SOCKET_RX SequenceNumber=" + std::to_string(header.GetSequenceNumber().GetValue()));
  tcpRxFile.flush();
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
Rx (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const TcpHeader& t, Ptr<const TcpSocketBase> tsb)
{
  (void)t;
  (void)tsb;
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << p->GetSize() << std::endl;
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

// BBR stats trace callback - logs to CSV with node_id and conn_id (csvLine: time,btlBw,...,state)
static void TcpBbrStatsCsvCallback(std::string context, std::string csvLine)
{
  std::lock_guard<std::mutex> lock(g_bbrStatsCsvMutex);
  if (!g_bbrStatsCsvFile.is_open())
    {
      g_bbrStatsCsvFile.open("bbr_stats_TCP.csv");
      g_bbrStatsCsvFile << "protocol,node_id,conn_id,time_s,btlBw_bps,rtProp_s,pacingGain,cwndGain,pacingRate_bps,targetCwnd,cwnd,bytesInFlight,state" << std::endl;
    }
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  g_bbrStatsCsvFile << "TCP," << nodeId << "," << connId << "," << csvLine << std::endl;
}

static void
Traces(uint32_t serverId, std::string pathVersion, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW << "/NodeList/" << serverId << "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";
  uint32_t cwMatches = Config::LookupMatches(pathCW.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - TCP CongestionWindow matches: " << cwMatches);

  std::ostringstream fileCW;
  fileCW << pathVersion << "TCP-cwnd-change"  << serverId << "" << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << serverId << "/$ns3::TcpL4Protocol/SocketList/*/RTT";
  uint32_t rttMatches = Config::LookupMatches(pathRTT.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - TCP RTT matches: " << rttMatches);

  std::ostringstream fileRTT;
  fileRTT << pathVersion << "TCP-rtt"  << serverId << "" << finalPart;

  std::ostringstream fileName;
  fileName << pathVersion << "TCP-rx-data" << serverId << "" << finalPart;
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::TcpL4Protocol/SocketList/*/Rx";
  uint32_t rxMatches = Config::LookupMatches(pathRx.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - TCP Rx matches: " << rxMatches);

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));
}

// ---- Per-connection TCP cwnd/RTT trace hookup (context-based; mirrors the QUIC scratch) ----------
// Keyed by (nodeId<<32 | connId) so each TCP connection gets its own file; otherwise a multi-UE
// server (one connection per UE) would merge all connections' cwnd/RTT into a single file.
static uint32_t g_tcpServerNodeId = 0xFFFFFFFFu;
static bool g_tcpCwndHooked = false, g_tcpRttHooked = false;
static std::map<uint64_t, Ptr<OutputStreamWrapper>> g_tcpCwndStreams, g_tcpRttStreams;
// Specific (node,socket,metric) paths already hooked, so a periodic rescan can catch late-created
// (staggered) server sockets without double-connecting an already-hooked source.
static std::set<std::string> g_hookedTcpPaths;

static std::string GetTcpTracePathPrefix(uint32_t nodeId)
{
  return (nodeId == g_tcpServerNodeId) ? "./server" : "./client";
}

static Ptr<OutputStreamWrapper>
GetOrCreateTcpTraceStream(std::map<uint64_t, Ptr<OutputStreamWrapper>>& streamMap,
                          const std::string& metricName, uint32_t nodeId, uint32_t connId)
{
  uint64_t key = ((uint64_t)nodeId << 32) | connId;
  auto it = streamMap.find(key);
  if (it != streamMap.end()) return it->second;
  AsciiTraceHelper asciiTraceHelper;
  std::ostringstream fileName;
  fileName << GetTcpTracePathPrefix(nodeId) << "TCP-" << metricName << nodeId << "-conn" << connId << ".txt";
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream(fileName.str().c_str());
  streamMap[key] = stream;
  return stream;
}

static void CwndChangeWithContext(std::string context, uint32_t oldCwnd, uint32_t newCwnd)
{
  uint32_t nodeId, connId; ParseNodeAndConnFromContext(context, nodeId, connId);
  Ptr<OutputStreamWrapper> stream = GetOrCreateTcpTraceStream(g_tcpCwndStreams, "cwnd-change", nodeId, connId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void RttChangeWithContext(std::string context, Time oldRtt, Time newRtt)
{
  uint32_t nodeId, connId; ParseNodeAndConnFromContext(context, nodeId, connId);
  Ptr<OutputStreamWrapper> stream = GetOrCreateTcpTraceStream(g_tcpRttStreams, "rtt", nodeId, connId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldRtt.GetSeconds() << "\t" << newRtt.GetSeconds() << std::endl;
}

// Connect via the context wildcard so the server's DATA sockets (one per UE) are each matched.
// Scheduled after connections establish (t=0.5) so the data sockets exist; retries until hooked.
static void ConnectTcpLayerTracesWithRetry(uint32_t retryCount)
{
  // Clients start staggered (0.1 + i*0.25 s, last at ~2.35 s), so the server's per-UE data sockets are
  // forked at different times. A latched wildcard connect at t=0.5 would only catch the early ones.
  // Instead rescan every node's TCP sockets and hook each (node,socket,metric) source exactly once
  // (tracked in g_hookedTcpPaths to avoid duplicate trace lines), long enough to cover the last connect.
  const uint32_t MAX_RETRIES = 60;            // 60 x 250 ms = 15 s window (>> last client start ~2.35 s)
  const Time RETRY_INTERVAL = MilliSeconds(250);
  const uint32_t MAX_SOCKETS_PER_NODE = 64;

  uint32_t nNodes = NodeList::GetNNodes();
  for (uint32_t n = 0; n < nNodes; ++n)
    {
      for (uint32_t s = 0; s < MAX_SOCKETS_PER_NODE; ++s)
        {
          std::ostringstream base;
          base << "/NodeList/" << n << "/$ns3::TcpL4Protocol/SocketList/" << s << "/";

          const std::string cw = base.str() + "CongestionWindow";
          if (!g_hookedTcpPaths.count(cw)
              && Config::ConnectFailSafe(cw, MakeCallback(&CwndChangeWithContext)))
            g_hookedTcpPaths.insert(cw);

          const std::string rtt = base.str() + "RTT";
          if (!g_hookedTcpPaths.count(rtt)
              && Config::ConnectFailSafe(rtt, MakeCallback(&RttChangeWithContext)))
            g_hookedTcpPaths.insert(rtt);
        }
    }

  if (retryCount < MAX_RETRIES)
    Simulator::Schedule(RETRY_INTERVAL, &ConnectTcpLayerTracesWithRetry, retryCount + 1);
  else
    NS_LOG_UNCOND("TCP layer traces: hooked " << g_hookedTcpPaths.size() << " trace sources across all sockets");
}

void UdpL4TxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  NS_LOG_UNCOND("UdpL4TxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, Interface: " << interface);
  
  // Log packet buffer state before processing
  NS_LOG_UNCOND("UdpL4TxCallback Packet buffer state - Size: " << packet->GetSize() 
            << ", Available: " << packet->GetSize());
  
  if (!udpL4TxFile.is_open())
  {
    udpL4TxFile.open("udp_l4_tx_TCP.txt", std::ios::out);
    NS_LOG_UNCOND("UDP L4 TX file opened");
  }
  DumpPacketHex(udpL4TxFile, packet, "UDP_L4_TX Interface=" + std::to_string(interface));
  udpL4TxFile.flush();
}

void UdpL4RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  NS_LOG_UNCOND("UdpL4RxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, Interface: " << interface);
  
  // Log packet buffer state before processing
  NS_LOG_UNCOND("UdpL4RxCallback Packet buffer state - Size: " << packet->GetSize() 
            << ", Available: " << packet->GetSize());
  
  if (!udpL4RxFile.is_open())
  {
    udpL4RxFile.open("udp_l4_rx_TCP.txt", std::ios::out);
    NS_LOG_UNCOND("UDP L4 RX file opened");
  }
  DumpPacketHex(udpL4RxFile, packet, "UDP_L4_RX Interface=" + std::to_string(interface));
  udpL4RxFile.flush();
}

// IPv4 L3 layer callbacks
void Ipv4L3TxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!ipv4L3TxFile.is_open())
  {
    ipv4L3TxFile.open("ipv4_l3_tx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(ipv4L3TxFile, packet, "IPV4_L3_TX Interface=" + std::to_string(interface));
  ipv4L3TxFile.flush();
}

void Ipv4L3RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!ipv4L3RxFile.is_open())
  {
    ipv4L3RxFile.open("ipv4_l3_rx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(ipv4L3RxFile, packet, "IPV4_L3_RX Interface=" + std::to_string(interface));
  ipv4L3RxFile.flush();
}

// Point-to-Point NetDevice callbacks
void P2PTxCallback(Ptr<const Packet> packet)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!p2pTxFile.is_open())
  {
    p2pTxFile.open("p2p_tx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(p2pTxFile, packet, "P2P_TX");
  p2pTxFile.flush();
}

void P2PRxCallback(Ptr<const Packet> packet)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!p2pRxFile.is_open())
  {
    p2pRxFile.open("p2p_rx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(p2pRxFile, packet, "P2P_RX");
  p2pRxFile.flush();
}

void
ConnectionEstablishedTraceSink(uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    NS_LOG_UNCOND("Connecting IMSI: " << imsi << " to ConnectionEstablished trace");
    // Open the file in append mode to log data
    std::ofstream outFile("connection_established_TCP.txt", std::ios_base::app);
    if (!outFile.is_open())
    {
        NS_LOG_ERROR("Can't open output file!");
        return;
    }
    // Log IMSI, CellId, RNTI, and simulation time
    double currentTime = Simulator::Now().GetSeconds();
    outFile << "Time: " << currentTime << "s, UE IMSI: " << imsi 
            << ", connected to CellId: " << cellId 
            << ", RNTI: " << rnti << "\n";
    // Close the file
    outFile.close();
}

void PacketDropCallback(Ptr<const Packet> packet) {
  NS_LOG_UNCOND("PacketDropCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
}

// Custom packet trace callback to track buffer operations
void PacketBufferTraceCallback(Ptr<const Packet> packet) {
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  NS_LOG_UNCOND("PacketBufferTraceCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
  
  // Log detailed buffer information
  NS_LOG_UNCOND("PacketBufferTraceCallback Buffer details - Size: " << packet->GetSize()
            << ", Available: " << packet->GetSize());
}

// ============================================================================
// IAB backhaul handover (3GPP inter-donor IAB-MT migration, NTN elevation-CHO)
// ----------------------------------------------------------------------------
// Manually triggers re-parenting of the IAB-node's backhaul (its MT, an LteUeRrc)
// from the serving donor satellite to a target donor satellite via the standard
// X2 handover path (LteEnbRrc::SendHandoverRequest). No A3 measurement algorithm
// is used: per 3GPP TR 38.821, LEO NTN uses elevation/time-based Conditional
// Handover, so the trigger time is pre-scheduled (see Phase 3 for the elevation
// crossing computation). Mirrors the wiring in ntn-iab-quic-dash.cc identically
// so the TCP and QUIC scenarios are matched for a fair comparison.
// ============================================================================
void
IabHandoverStart (uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCellId)
{
  std::cout << "[IAB-HO] t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER START: IAB MT imsi=" << imsi << " rnti=" << rnti
            << " leaving cell " << cellId << " -> target physCell " << targetCellId << std::endl;

  // Retune the IAB-MT backhaul to the target donor so the non-contention random access to
  // the target cell can complete. The standard LteUeRrc handover does not update these
  // IAB-specific bindings, so we mirror what AttachIabToClosestEnb does for the new donor:
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
// switch per UE (SGW/PGW re-tunnels each UE's downlink to the new donor). See EpcEnbApplication::
// Export/ImportIabDescendants.
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
  MigrateIabDescendants (g_hoSrcDonor, g_hoTgtDonor, g_hoOldIabRnti, rnti, imsi);
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

// Sample and print UE positions (std::cout so it is visible in the optimized build, where NS_LOG is
// stripped) to document the random UE mobility - that the UEs actually move and stay within the disc.
void
DumpUePositions (NodeContainer ues)
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

int
main (int argc, char *argv[])
{
  // Enable DASH logging for debugging
  // LogComponentEnable("DashClient", LOG_LEVEL_ALL);  // LOG_LEVEL_LOGIC to see ConnectionSucceeded/Failed
  // LogComponentEnable("DashServer", LOG_LEVEL_ALL);
  // LogComponentEnable("HttpParser", LOG_LEVEL_INFO);
  // LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);
  
  // Enable TCP socket logging to see connection events and data flow
  // LogComponentEnable("TcpSocketBase", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet handling
  // LogComponentEnable("TcpL4Protocol", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet flow
  // LogComponentEnable("TcpSocket", LOG_LEVEL_ALL);      // LOG_LEVEL_ALL to see socket data handling
  
  // Enable packet-level logging for debugging
  // LogComponentEnable("Packet", LOG_LEVEL_DEBUG);        // LOG_LEVEL_DEBUG to see packet operations
  // LogComponentEnable("UdpSocket", LOG_LEVEL_DEBUG);     // LOG_LEVEL_DEBUG to see UDP operations
  // LogComponentEnable("UdpL4Protocol", LOG_LEVEL_DEBUG); // LOG_LEVEL_DEBUG to see UDP protocol
  
    // LogComponentEnable("DashServer", LOG_LEVEL_ALL);
    // LogComponentEnable("DashClient", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicSocketTxBuffer", LOG_LEVEL_INFO);
    // LogComponentEnable("QuicSocketRxBuffer", LOG_LEVEL_INFO);
    // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicStreamBase", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicStream", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicCongestionControl", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicSocket", LOG_LEVEL_ALL);
    // LogComponentEnable("QuicSocketBase", LOG_LEVEL_INFO);
  // LogComponentEnable("QuicStreamBase", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicCongestionControl", LOG_LEVEL_ALL);
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
  LogComponentEnable("MmWaveHelper", LOG_LEVEL_INFO);
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
  
  // TCP Layer
  // LogComponentEnable("TcpSocket", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("TcpSocketBase", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("TcpL4Protocol", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("TcpHeader", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("TcpNewReno", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  // LogComponentEnable("TcpCongestionOps", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
  
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
  uint32_t rlcBufSize = 50;  // Increased from 10 to 50 MB to prevent RLC buffer overflows and packet drops (matches QUIC)
  uint32_t interPacketInterval = 10000; 
  uint32_t packetSize = 1400; //bytes // Decreased from 1500 to 1400 to avoid IP fragmentation (MSS < MTU - Headers)
  std::string ccAlgorithm = "ns3::TcpBbr";
  // IAB backhaul handover knobs (identical to ntn-iab-quic-dash.cc for a matched TCP-vs-QUIC scenario)
  uint32_t numSatellites = 4;  // Number of donor satellites in the constellation (numSat-1 handovers)
  double hoTime = 10.0;        // Inter-handover interval [s]: handover k occurs at k*hoTime (0 = disabled)
  double simDuration = 60.0;   // Video/simulation duration [s]
  double targetDt = 30.0;      // DASH target buffer [s] (lower => continuous requests, to test data-plane recovery)
  std::string backhaulRate = "100Mbps";  // LEO satellite backhaul capacity (S1-U feeder rate; arXiv 2012.02136)
  std::string abrAlgorithm = "ns3::FdashClient";  // DASH ABR controller: ns3::FdashClient or ns3::BolaClient
  bool enableTraces = false;   // Heavy RLC/MAC/PHY ASCII traces (~12MB/run): off for the campaign
  bool ueMobility = true;      // UEs move randomly within a disc around the IAB (false = static placement)
  double ueSpeed = 1.5;        // UE random-waypoint speed [m/s] (pedestrian)
  double ueRadiusMax = 500.0;  // radius [m] of the circular boundary the UEs roam within, centred on the IAB
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);  
  cmd.AddValue("ccAlgorithm", "TCP Congestion Control Algorithm", ccAlgorithm);
  cmd.AddValue("numSat", "Number of donor satellites (>=2 enables backhaul handover)", numSatellites);
  cmd.AddValue("hoTime", "Inter-handover interval [s] (handover k at k*hoTime; 0 = disabled)", hoTime);
  cmd.AddValue("simDuration", "Video/simulation duration [s]", simDuration);
  cmd.AddValue("targetDt", "DASH target buffer [s]", targetDt);
  cmd.AddValue("backhaulRate", "LEO satellite backhaul capacity / S1-U feeder rate (e.g. 100Mbps)", backhaulRate);
  cmd.AddValue("abrAlgorithm", "DASH ABR algorithm TypeId (ns3::FdashClient or ns3::BolaClient)", abrAlgorithm);
  cmd.AddValue("traces", "Enable heavy RLC/MAC/PHY ASCII traces (slow; off for campaign)", enableTraces);
  cmd.AddValue("ueMobility", "UEs move randomly within a disc around the IAB (false = static placement)", ueMobility);
  cmd.AddValue("ueSpeed", "UE random-waypoint speed [m/s]", ueSpeed);
  cmd.AddValue("ueRadiusMax", "Radius [m] of the UE mobility boundary around the IAB", ueRadiusMax);
  cmd.Parse(argc, argv);

  //   if(rlcAm)
  // {
  //LogComponentEnable("LteRlcAm", LOG_LEVEL_LOGIC); 
  // }
  // else
  // {
  // LogComponentEnable("MmWaveFlexTtiMacScheduler", LOG_LEVEL_DEBUG);
  // // LogComponentEnable("MmWaveSpectrumPhy", LOG_LEVEL_INFO);
  // LogComponentEnable("MmWaveEnbPhy", LOG_LEVEL_DEBUG);
  // LogComponentEnable("MmWaveUeMac", LOG_LEVEL_DEBUG);
  // LogComponentEnable("MmWaveEnbMac", LOG_LEVEL_DEBUG);
  // }
  // Config::SetDefault("ns3::MmWavePhyMacCommon::UlSchedDelay", UintegerValue(1));
  // Enable RLC buffer configuration to prevent buffer overflow on NTN links
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
  // Config::SetDefault ("ns3::LteRlcAm::PollRetransmitTimer", TimeValue(MilliSeconds(1.0)));
  // Config::SetDefault ("ns3::LteRlcAm::ReorderingTimer", TimeValue(MilliSeconds(2.0)));
  // Config::SetDefault ("ns3::LteRlcAm::StatusProhibitTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::LteRlcUm::ReportBufferStatusTimer", TimeValue(MicroSeconds(500)));
  // Config::SetDefault ("ns3::MmWavePhyMacCommon::SubcarriersPerChunk", UintegerValue (12));
  
  Config::SetDefault ("ns3::MmWavePhyMacCommon::ChunkWidth", DoubleValue (1.389e6)); 

  // Keep default ChunkPerRB = 72 and ResourceBlockNum = 1 (required for TDMA)

	Config::SetDefault ("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue (2));  // aligned with QUIC scratch for a fair TCP-vs-QUIC comparison (was 4)
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
  
  // ============================================================================
  // TCP-SPECIFIC CONFIGURATION (Aligned with QUIC for Fair Comparison)
  // ============================================================================
  // TCP parameters optimized for NTN scenarios with high RTTs and potential packet loss
  // Values are tuned to match QUIC configuration for fair comparison
  // Note: Some QUIC-specific parameters (MaxTrackedGaps, AckDelayExponent) don't have
  //       direct TCP equivalents, and some TCP-specific parameters (TcpNoDelay) are
  //       protocol-specific optimizations
  
  // ============================================================================
  // ACKNOWLEDGMENT PARAMETERS
  // ============================================================================
  
  // 1. Reduce delayed ACK timeout (from default 200ms to 10ms) - MATCHES QUIC CONGESTION AVOIDANCE
  //    QUIC: kDelayedAckTimeout = 10ms (reduced from 15ms for congestion avoidance)
  //    Sends ACKs much more frequently, reducing acknowledgment delays by 60% for faster congestion detection
  //    Realistic: 10ms is still safe for NTN and critical for detecting congestion quickly
  // 1. Reduce delayed ACK timeout (from default 200ms to 25ms) - MATCHES QUIC RFC 9000 DEFAULT
  //    QUIC: kDelayedAckTimeout = 25ms (RFC 9000 default)
  Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(MilliSeconds(25)));

  // 1b. Set Delayed Ack Count to 2 - MATCHES QUIC RFC 9000 RECOMMENDATION
  //     QUIC: kMaxPacketsReceivedBeforeAckSend = 2
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));
  
  // 2. Disable Nagle's algorithm for low latency (TCP-specific optimization)
  //    QUIC doesn't have Nagle's algorithm, so disabling it makes TCP more comparable
  //    Prevents delay in sending small packets, improving responsiveness
  Config::SetDefault("ns3::TcpSocket::TcpNoDelay", BooleanValue(true));
  
  // ============================================================================
  // CONGESTION CONTROL PARAMETERS
  // ============================================================================
  
  // TCP Congestion Control Configuration - Dynamic Selection
  // QUIC: CcType = QuicNewReno (default)
  // Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(ccAlgorithm)));
  
  // Reduce initial slow start threshold to enter congestion avoidance sooner (MATCHES QUIC CONGESTION AVOIDANCE)
  // QUIC: InitialSlowStartThreshold = 32KB (reduced from unlimited for congestion avoidance)
  // SIGNIFICANT CHANGE: Reduced from unlimited (65535) to 32KB (21 packets) for much more conservative behavior
  // This forces the connection to exit slow start after ~21 packets, preventing congestion buildup
  // Realistic: 32KB is conservative but prevents the exponential growth that causes congestion
  // Reduce initial slow start threshold to enter congestion avoidance sooner (MATCHES QUIC CONGESTION AVOIDANCE)
  // QUIC: InitialSlowStartThreshold = 32KB (reduced from unlimited for congestion avoidance)
  // SIGNIFICANT CHANGE: Reduced from unlimited (65535) to 32KB (21 packets) for much more conservative behavior
  // This forces the connection to exit slow start after ~21 packets, preventing congestion buildup
  // Realistic: 32KB is conservative but prevents the exponential growth that causes congestion
  // Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue(32*1024)); // Commented out to match QUIC RFC compliance (default is infinite)
  
  // Initial congestion window (MATCHES QUIC)
  // QUIC: m_initialCWnd = 10 * segmentSize (default)
  // TCP: Set to 10 segments for NTN scenarios
  Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
  
  // Minimum RTO - Set to match QUIC's kMinRTOTimeout (200ms)
  // QUIC: kMinRTOTimeout = 200ms
  // TCP default is 1s, but setting to 200ms for fair comparison with QUIC
  Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(200)));
  
  // Connection timeout for NTN scenarios (high RTT)
  Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(6.0)));
  
  // Data retries
  Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(6));
  
  // Packet size configuration (segment size) - MATCHES QUIC
  // QUIC: InitialPacketSize = packetSize (1500)
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
  
  // ============================================================================
  // BUFFER PARAMETERS (MATCHES QUIC)
  // ============================================================================
  // TCP socket buffers: 64 MB, MATCHED to the QUIC socket/stream buffers + flow-control windows
  // (ns3::QuicSocketBase::Socket*BufSize / MaxData / MaxStreamData, all 64 MB) so the TCP-vs-QUIC comparison
  // is fair. Reduced from 128 MB in lockstep with the QUIC buffers (which had to drop from a latent 512 MB
  // to avoid OOM once the realistic 4.2 Mbps ladder let flows run); 64 MB is ~120 s of buffering and TCP
  // itself would be fine with far less.
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(64*1024*1024));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(64*1024*1024));
  
  // Enable Pacing for TCP (to match QUIC)
  Config::SetDefault("ns3::TcpSocketState::EnablePacing", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocketState::PaceInitialWindow", BooleanValue(true));  // 64 MB (2x max segment)
  
  // ============================================================================
  // NOTE: QUIC-Specific Parameters (No TCP Equivalent)
  // ============================================================================
  // The following QUIC parameters don't have direct TCP equivalents:
  // - MaxTrackedGaps (100): QUIC-specific ACK gap tracking
  // - kMaxPacketsReceivedBeforeAckSend (10): QUIC-specific ACK frequency control
  // - AckDelayExponent (2): QUIC-specific ACK delay encoding
  // - kTimeReorderingFraction (9.0/8.0): QUIC-specific loss detection
  // - kDefaultInitialRtt (333ms): QUIC initial RTT estimate (TCP measures from first packet)
  //
  // These are protocol-specific features that reflect QUIC's design advantages.
  // TCP's equivalent behavior is handled differently through its own mechanisms.
 
  // Enable multi-beam functionality
//  Config::SetDefault("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue(2));
  Config::SetDefault("ns3::MmWaveHelper::Scheduler", StringValue("ns3::MmWavePaddedHbfMacScheduler"));

  // Constrain the satellite backhaul to a realistic LEO capacity by rate-limiting the S1-U feeder
  // link between the donor (satellite) and the core (identical to ntn-iab-quic-dash.cc). Makes the
  // satellite backhaul the end-to-end bottleneck so the handover's brief radio outage produces an
  // observable congestion-window collapse. Default 100 Mbps (5G-NR-NTN Ka-band; arXiv 2012.02136).
  Config::SetDefault("ns3::MmWavePointToPointEpcHelper::S1uLinkDataRate", DataRateValue(DataRate(backhaulRate)));
  // Match the QUIC scratch: S1-U MTU raised above the largest tunneled datagram so nothing
  // IP-fragments at the PGW (the default 2000 fragmented large QUIC datagrams and ns-3's
  // reassembly corrupted them; TCP segments never exceeded it, but keep configs identical).
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
  
  // Install Internet stack on remote host
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);
  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (9000));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
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
  
  // Get current stopTime (line 1024)
  double desiredVideoDuration = simDuration;
  double stopTime = desiredVideoDuration;  // Minimal time for testing
  
  // // Check if current stopTime is less than minimum, and adjust if needed
  // if (stopTime < minSimulationDuration)
  // {
  //     NS_LOG_UNCOND("Adjusting simulation duration: " << stopTime << "s -> " 
  //                  << minSimulationDuration << "s (required for video duration " 
  //                  << desiredVideoDuration << "s)");
  //     stopTime = minSimulationDuration;
  // }
  // else
  // {
  //     NS_LOG_UNCOND("Simulation duration: " << stopTime << "s (video duration: " 
  //                  << desiredVideoDuration << "s, minimum required: " 
  //                  << minSimulationDuration << "s)");
  // }

  // Install Mobility Model
  
  // Install WaypointMobilityModel for satellite (eNB)
  // Start at original position (Overhead)
  // Move in X direction at 7.56 km/s (realistic Starlink 550 km circular-orbit ground speed:
  // v = sqrt(mu/r), mu=398600 km^3/s^2, r=6921 km => 7.59 km/s; adopt 7.56 km/s). Identical to QUIC scratch.

  double satVelocity = 7560.0; // m/s
  
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::WaypointMobilityModel");
  enbmobility.Install (enbNodes);
  
  double minSimulationDuration = stopTime;

  // Space the donor satellites along the orbital track by satVelocity*hoTime, so a new donor reaches the
  // zenith above the IAB every hoTime seconds. With the realistic Starlink single-plane values
  // (v=7.56 km/s, hoTime=262 s) this gives ~1,980 km spacing (~22 satellites/plane in-plane spacing).
  // Handovers fire at the equal-elevation CROSSOVER t=(k-0.5)*hoTime (~29 deg). Identical to ntn-iab-quic-dash.cc.
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
    // UEs move RANDOMLY within a disc of radius ueRadiusMax (default 500 m) centred on their IAB, at a
    // pedestrian random-waypoint speed (ueSpeed). Implemented with WaypointMobilityModel: for each UE we
    // pre-compute a random-waypoint track (uniform-area points inside the disc, straight legs at ueSpeed),
    // so the mmWave channel sees real UE motion/Doppler. Deterministic per RngRun and byte-identical to the
    // QUIC scratch => the same UE tracks for the paired TCP-vs-QUIC comparison.
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
  // Install Internet stack on UE nodes
  internet.Install (ueNodes);
  
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

  // --- IAB backhaul handover wiring (identical to ntn-iab-quic-dash.cc) ---------------------------
  // Set up X2 interfaces between donor satellites so the IAB backhaul can hand over between them.
  if (enbmmWaveDevs.GetN () > 1)
  {
    mmwaveHelper->AddX2Interface (enbNodes);
    NS_LOG_UNCOND("[IAB-HO] X2 interfaces set up between " << enbNodes.GetN() << " donor satellites");
  }
  // Build the donor cellId -> device map and connect handover traces on the IAB backhaul RRC.
  for (uint32_t s = 0; s < enbmmWaveDevs.GetN (); ++s)
  {
    Ptr<MmWaveEnbNetDevice> donor = enbmmWaveDevs.Get (s)->GetObject<MmWaveEnbNetDevice> ();
    if (donor)
    {
      g_donorByCellId[donor->GetCellId ()] = enbmmWaveDevs.Get (s);
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
  // Schedule a CHAIN of IAB backhaul handovers across the constellation: donor k is overhead at t=k*hoTime;
  // handover k (k=1..numSat-1) fires at the EQUAL-ELEVATION CROSSOVER t=(k-0.5)*hoTime (~29 deg for the
  // realistic single-plane params), when the setting donor k-1 and rising donor k are at equal elevation.
  // Time/ephemeris-scheduled (3GPP TR 38.821 NTN CHO), not a measured elevation threshold. Identical to QUIC.
  if (hoTime > 0.0 && enbmmWaveDevs.GetN () > 1 && numRelays > 0)
  {
    for (uint32_t k = 1; k < enbmmWaveDevs.GetN (); ++k)
    {
      double t = ((double)k - 0.5) * hoTime;
      Simulator::Schedule (Seconds (t), &TriggerIabBackhaulHandover,
                           iabmmWaveDevs.Get (0), enbmmWaveDevs.Get (k - 1), enbmmWaveDevs.Get (k));
      Simulator::Schedule (Seconds (t + 0.5), &PrintIabServingCell, iabmmWaveDevs.Get (0),
                           std::string ("HO") + std::to_string (k) + "+0.5");
      NS_LOG_UNCOND("[IAB-HO] Scheduled handover " << k << " at t=" << t
                    << "s (donor " << (k - 1) << " -> donor " << k << ")");
    }
  }
  // --- end IAB backhaul handover wiring -----------------------------------------------------------

  // Install and start applications on UEs and remote host
  // LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
  // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  
  // DASH over TCP configuration - optimized for QoE and preventing interruptions (matches QUIC)
  // Increased target buffering time for more aggressive buffering to prevent rebuffering
  // For NTN scenarios with high latency and variable throughput, 45-60s is realistic
  // 60s provides good balance: prevents interruptions while remaining realistic for real-world scenarios
  double target_dt = targetDt;  // Target buffering time [s] (CLI-configurable; matches QUIC)
  // DASH playback buffer: holds the targetDt (30 s, up to ~54 s with BOLA) of buffered video. With the
  // 15 Mbps-capped ladder that is <=~100 MB, so 128 MB suffices; MATCHED to the QUIC scratch.
  uint32_t bufferSpace = 128*1024*1024;  // 128 MB (matches QUIC)

  double window = 50;  // Throughput measurement window in milliseconds (increased from 5ms to 50ms for more stable measurements and smoother adaptation, matches QUIC)

  std::string algorithm = abrAlgorithm;  // DASH adaptation algorithm (--abrAlgorithm: FdashClient/BolaClient)
  


  // DOWNLINK simulation: DASH server on remoteHost, clients on UE nodes
  // Create a single DASH server on remoteHost (listening on port 80)
  DashServerHelper dashServer ("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), 80));
  serverApps.Add (dashServer.Install (remoteHost));
  NS_LOG_UNCOND("DASH Server installed on remoteHost (IP=" << remoteHostAddr << ") port 80");
  
  // Create DASH clients on each UE node (connecting to remoteHost server)
  // This simulates DOWNLINK: users download video from remote server
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    DashClientHelper dashClient ("ns3::TcpSocketFactory",
                                  InetSocketAddress(remoteHostAddr, 80),
                                  algorithm);
    dashClient.SetAttribute ("VideoId", UintegerValue(u + 1));
    dashClient.SetAttribute ("TargetDt", TimeValue(Seconds(target_dt)));
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
      NS_LOG_UNCOND("Connected DASH Client Tx and Rx traces for UE " << u << " (Node " << nodeId << ")");
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
  
  // Server starts early to ensure it's ready before clients connect
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after the server, staggered by 0.25 s each (identical to the QUIC scratch for a fair
  // comparison). TCP tolerates simultaneous starts, but the offset is matched so both transports see the
  // same client arrival schedule.
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

  NS_LOG_UNCOND("\n=== Scheduling TCP Trace Connections (DOWNLINK) ===");
  
  // Per-connection context-based TCP trace hookup. The previous per-node Traces() used
  // ConnectWithoutContext + wildcard, merging all server connections into one file. The wildcard
  // SocketList/* matches each data socket; scheduled at t=0.5 (after connections establish) and
  // writes per-(node,conn) files. Mirrors the QUIC scratch.
  g_tcpServerNodeId = remoteHost->GetId();
  Simulator::Schedule(Seconds(0.5), &ConnectTcpLayerTracesWithRetry, 0);
  NS_LOG_UNCOND("  Scheduled context-based TCP trace hookup (wildcard SocketList/*) at t=0.5s, server node " << g_tcpServerNodeId);

  // BBR stats CSV output (bbr_stats_TCP.csv) -- ENABLED 2026-07-30 for the QUIC-vs-TCP BtlBw diff
  // (env-gate BBR_STATS_CSV=1). Mirrors the QUIC scratch which has this on unconditionally.
  if (getenv("BBR_STATS_CSV"))
    {
      Simulator::Schedule(Seconds(3.0), []() {
        Config::MatchContainer bbrMatches = Config::LookupMatches("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/CongestionOps/$ns3::TcpBbr");
        bbrMatches.ConnectFailSafe("BbrStatsTrace", MakeCallback(&TcpBbrStatsCsvCallback));
        if (bbrMatches.GetN() > 0)
          NS_LOG_UNCOND("  Connected BBR stats trace to " << bbrMatches.GetN() << " TcpBbr instance(s)");
      });
    }
  
  // Add TCP socket callback connections for debugging
  NS_LOG_UNCOND("\n=== Adding TCP Socket Callback Connections ===");
  
  // Connect TCP socket callbacks for all nodes
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
  {
    Ptr<Node> node = *it;
    uint32_t nodeId = node->GetId();
    
    // Connect TCP socket Tx/Rx traces
    std::ostringstream tcpTxPath;
    tcpTxPath << "/NodeList/" << nodeId << "/$ns3::TcpL4Protocol/SocketList/*/Tx";
    // Note: TcpSocketBase exposes Tx trace; guard by lookup.
    if (Config::LookupMatches(tcpTxPath.str().c_str()).GetN() > 0)
      {
        Config::ConnectWithoutContextFailSafe(tcpTxPath.str(), MakeCallback(&TcpSocketTxCallback));
      }
    
    std::ostringstream tcpRxPath;
    tcpRxPath << "/NodeList/" << nodeId << "/$ns3::TcpL4Protocol/SocketList/*/Rx";
    Config::ConnectWithoutContextFailSafe(tcpRxPath.str(), MakeCallback(&TcpSocketRxCallback));
    
    NS_LOG_UNCOND("  Added TCP socket traces for Node " << nodeId);
  }
  
  // Add packet buffer monitoring traces
  NS_LOG_UNCOND("\n=== Adding Packet Buffer Monitoring Traces ===");
  
  // Monitor packet operations on all nodes
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
  {
    Ptr<Node> node = *it;
    uint32_t nodeId = node->GetId();
    
    // Connect packet traces for debugging
    std::ostringstream packetTxPath;
    packetTxPath << "/NodeList/" << nodeId << "/DeviceList/*/$ns3::PointToPointNetDevice/Tx";
    Config::ConnectWithoutContextFailSafe(packetTxPath.str(), MakeCallback(&PacketBufferTraceCallback));
    
    std::ostringstream packetRxPath;
    packetRxPath << "/NodeList/" << nodeId << "/DeviceList/*/$ns3::PointToPointNetDevice/Rx";
    Config::ConnectWithoutContextFailSafe(packetRxPath.str(), MakeCallback(&PacketBufferTraceCallback));
    
    NS_LOG_UNCOND("  Added packet buffer traces for Node " << nodeId);
  }
    
  std::string tracePrefix = "ntn_iab_tcp_dash";  // Keep variable for log statements
  NS_LOG_UNCOND("\n=== Trace Configuration ===");
  NS_LOG_UNCOND("TCP traces: Using TCP trace approach");
  NS_LOG_UNCOND("DASH application traces: ENABLED");
  NS_LOG_UNCOND("RLC/MAC/PHY layer traces: ENABLED");
  NS_LOG_UNCOND("============================\n");
  
  NS_LOG_UNCOND("\n=== DASH over TCP Simulation Parameters (DOWNLINK) ===");
  NS_LOG_UNCOND("Direction: DOWNLINK (Server on remoteHost, Clients on UE nodes)");
  NS_LOG_UNCOND("Number of UEs: " << ueNodes.GetN());
  NS_LOG_UNCOND("Simulation time: " << stopTime << " seconds");
  NS_LOG_UNCOND("DASH algorithm: " << algorithm);
  NS_LOG_UNCOND("Target buffering time: " << target_dt << " seconds");
  
  Simulator::Run();
  
  // Print DASH statistics for each UE (DOWNLINK: clients are on UE nodes)
  NS_LOG_UNCOND("\n========== DASH over TCP Results (DOWNLINK) ==========");
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
    if (dashClient)
    {
      NS_LOG_UNCOND("\nUE " << u << " (VideoId=" << (u+1) << ", DASH Client):");
      dashClient->GetStats();
      
      // Print DASH trace statistics
      uint32_t nodeId = ueNodes.Get(u)->GetId();
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
  if (tcpTxFile.is_open()) tcpTxFile.close();
  if (tcpRxFile.is_open()) tcpRxFile.close();
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