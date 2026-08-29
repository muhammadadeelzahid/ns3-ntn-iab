
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

// Set to false to disable expensive packet-level tracing/logging and speed up simulations.
static bool g_enableVerbosePacketTracing = false;

// Global file streams for each layer
std::ofstream tcpTxFile, tcpRxFile;
std::ofstream udpL4TxFile, udpL4RxFile;
std::ofstream ipv4L3TxFile, ipv4L3RxFile;
std::ofstream p2pTxFile, p2pRxFile;

// BBR CSV log file (shared across connections)
std::ofstream g_bbrStatsCsvFile;
std::mutex g_bbrStatsCsvMutex;

// DASH trace files
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
// descendant UE bearers from the source donor to the target donor (inter-donor IAB migration).
Ptr<NetDevice> g_hoSrcDonor;
Ptr<NetDevice> g_hoTgtDonor;
uint16_t g_hoOldIabRnti = 0;
// Modeled NTN handover-execution/sync delay [s]: extra interruption added on top of the intrinsic
// RA gap, representing TA/Doppler re-acquisition (SIB19 ephemeris/GNSS) plus the core-network S1 path
// switch required by 3GPP TR 38.821 but omitted by the bare X2 handover. Defers the descendant-UE
// data-plane migration, extending the downlink gap into the realistic LEO band (~50-300 ms) instead
// of the ~20-40 ms floor. Default 0 disables the extra delay.
double g_hoExecDelay = 0.0;
// True between an IAB backhaul handover's trigger and its completion (EndOk/EndError). The
// (src,tgt,oldRnti) tuple above is carried through these globals, so a second handover must not be
// triggered while one is still pending; guarded in TriggerIabBackhaulHandover.
bool g_hoPending = false;

// Dump a full packet as a hex/ASCII table.
void DumpPacketHex(std::ofstream& file, Ptr<const Packet> packet, const std::string& prefix)
{
  file << prefix << " Size=" << packet->GetSize() << " bytes" << std::endl;
  
  // Copy to avoid modifying the original packet.
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

// DASH client Tx trace: client sends a segment request.
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

// DASH client Rx trace: client receives video segments (MPEG frames).
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

// DASH server Rx trace: server receives a segment request.
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

// TcpSocketBase Tx callback.
void TcpSocketTxCallback(Ptr<const Packet> packet, const TcpHeader& header, Ptr<const TcpSocketBase> socket)
{
  if (!g_enableVerbosePacketTracing) return;
  
  NS_LOG_UNCOND("TcpSocketTxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, sequence_number: " << header.GetSequenceNumber());
  
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

// TcpSocketBase Rx callback.
void TcpSocketRxCallback(Ptr<const Packet> packet, const TcpHeader& header, Ptr<const TcpSocketBase> socket)
{
  if (!g_enableVerbosePacketTracing) return;
  
  NS_LOG_UNCOND("TcpSocketRxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, sequence_number: " << header.GetSequenceNumber());
  
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

// ---- Per-connection TCP cwnd/RTT trace hookup (context-based) -----------------------------------
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
  if (!g_enableVerbosePacketTracing) return;
  
  NS_LOG_UNCOND("UdpL4TxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, Interface: " << interface);
  
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
  if (!g_enableVerbosePacketTracing) return;
  
  NS_LOG_UNCOND("UdpL4RxCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() 
            << " bytes, Interface: " << interface);
  
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
  if (!g_enableVerbosePacketTracing) return;
  
  if (!ipv4L3TxFile.is_open())
  {
    ipv4L3TxFile.open("ipv4_l3_tx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(ipv4L3TxFile, packet, "IPV4_L3_TX Interface=" + std::to_string(interface));
  ipv4L3TxFile.flush();
}

void Ipv4L3RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;
  
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
  if (!g_enableVerbosePacketTracing) return;
  
  if (!p2pTxFile.is_open())
  {
    p2pTxFile.open("p2p_tx_TCP.txt", std::ios::out);
  }
  DumpPacketHex(p2pTxFile, packet, "P2P_TX");
  p2pTxFile.flush();
}

void P2PRxCallback(Ptr<const Packet> packet)
{
  if (!g_enableVerbosePacketTracing) return;
  
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
    std::ofstream outFile("connection_established_TCP.txt", std::ios_base::app);
    if (!outFile.is_open())
    {
        NS_LOG_ERROR("Can't open output file!");
        return;
    }
    double currentTime = Simulator::Now().GetSeconds();
    outFile << "Time: " << currentTime << "s, UE IMSI: " << imsi 
            << ", connected to CellId: " << cellId 
            << ", RNTI: " << rnti << "\n";
    outFile.close();
}

void PacketDropCallback(Ptr<const Packet> packet) {
  NS_LOG_UNCOND("PacketDropCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
}

// Custom packet trace callback to track buffer operations
void PacketBufferTraceCallback(Ptr<const Packet> packet) {
  if (!g_enableVerbosePacketTracing) return;
  
  NS_LOG_UNCOND("PacketBufferTraceCallback Time: " << Simulator::Now().GetSeconds() 
            << "s, Packet size: " << packet->GetSize() << " bytes");
  
  NS_LOG_UNCOND("PacketBufferTraceCallback Buffer details - Size: " << packet->GetSize()
            << ", Available: " << packet->GetSize());
}

// ============================================================================
// IAB backhaul handover (3GPP inter-donor IAB-MT migration, NTN elevation-CHO)
// ----------------------------------------------------------------------------
// Triggers re-parenting of the IAB node's backhaul (its MT, an LteUeRrc) from the
// serving donor satellite to a target donor satellite via the standard X2 handover
// path (LteEnbRrc::SendHandoverRequest). No A3 measurement algorithm is used: per
// 3GPP TR 38.821, LEO NTN uses elevation/time-based Conditional Handover, so the
// trigger time is pre-scheduled from the elevation-crossing geometry. Wiring matches
// the QUIC scenario so the TCP and QUIC results are directly comparable.
// ============================================================================
void
IabHandoverStart (uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCellId)
{
  std::cout << "IAB handover t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER START: IAB MT imsi=" << imsi << " rnti=" << rnti
            << " leaving cell " << cellId << " -> target physCell " << targetCellId << std::endl;

  // Retune the IAB-MT backhaul to the target donor so the non-contention random access to
  // the target cell can complete. The standard LteUeRrc handover does not update these
  // IAB-specific bindings, so replicate what AttachIabToClosestEnb does for the new donor:
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
          std::cout << "IAB handover   retuned + registered IAB-MT backhaul PHY to donor cellId "
                    << targetCellId << std::endl;
        }
    }
  else
    {
      std::cout << "IAB handover   WARN: no donor device for target cellId " << targetCellId
                << " - beamforming NOT retuned" << std::endl;
    }
}

// Migrate the IAB-MT's descendant UE bearers from the source donor to the target donor, so their
// downlink does not black-hole after the backhaul re-parents. Reads the relay state from the source
// donor's EpcEnbApplication and re-installs it on the target donor's, which drives an S1 path switch
// per UE (SGW/PGW re-tunnels each UE's downlink to the new donor). See
// EpcEnbApplication::Export/ImportIabDescendants.
void
MigrateIabDescendants (Ptr<NetDevice> srcDonor, Ptr<NetDevice> tgtDonor,
                       uint16_t oldIabRnti, uint16_t newIabRnti, uint64_t iabImsi)
{
  if (!srcDonor || !tgtDonor)
    {
      std::cout << "IAB migration ERROR: missing src/tgt donor at handover end - cannot migrate descendants" << std::endl;
      return;
    }
  Ptr<EpcEnbApplication> srcApp = srcDonor->GetNode ()->GetApplication (0)->GetObject<EpcEnbApplication> ();
  Ptr<EpcEnbApplication> tgtApp = tgtDonor->GetNode ()->GetApplication (0)->GetObject<EpcEnbApplication> ();
  if (!srcApp || !tgtApp)
    {
      std::cout << "IAB migration ERROR: could not retrieve donor EpcEnbApplication - descendants NOT migrated" << std::endl;
      return;
    }
  std::vector<EpcEnbApplication::IabDescendantContext> ctx = srcApp->ExportIabDescendants (oldIabRnti);
  tgtApp->ImportIabDescendants (newIabRnti, iabImsi, ctx);
  // Release the migrated descendants' relay state from the source donor now that the target has
  // imported them and the SGW downlink is re-pointed, so the source keeps no stale/duplicate state.
  srcApp->ReleaseIabDescendants (ctx);
}

void
IabHandoverEndOk (uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::cout << "IAB handover t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER END OK: IAB MT imsi=" << imsi
            << " now connected to cell " << cellId << " rnti=" << rnti << std::endl;

  // The IAB-MT backhaul has re-parented; migrate its descendant UEs' data plane to the new donor.
  // If a handover-execution/sync delay is modeled, defer the migration by that amount: the descendant
  // downlink stays interrupted until TA re-acquisition and the S1 path switch complete (realistic NTN).
  if (g_hoExecDelay > 0.0)
    {
      std::cout << "IAB handover   deferring descendant migration by hoExecDelay="
                << g_hoExecDelay * 1e3 << " ms (modeled NTN sync + path-switch interruption)" << std::endl;
      Simulator::Schedule (Seconds (g_hoExecDelay), &MigrateIabDescendants,
                           g_hoSrcDonor, g_hoTgtDonor, g_hoOldIabRnti, rnti, imsi);
    }
  else
    {
      MigrateIabDescendants (g_hoSrcDonor, g_hoTgtDonor, g_hoOldIabRnti, rnti, imsi);
    }
  g_hoPending = false;   // this handover's tuple has been consumed; a new handover may be triggered
}

// Fired if an IAB backhaul handover fails (random access to the target donor never completed after
// the preamble retransmissions are exhausted). Logged prominently so post-processing can detect and
// exclude runs with a failed handover (which would leave the IAB-MT's UEs black-holed).
void
IabHandoverEndError (uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::cout << "IAB handover t=" << Simulator::Now ().GetSeconds ()
            << "s HANDOVER FAILED: IAB MT imsi=" << imsi
            << " could not complete RA to target (was leaving cell " << cellId << ", rnti=" << rnti
            << ") - downstream UEs may lose service for this run" << std::endl;
  g_hoPending = false;   // failed handover: clear so a subsequent handover can still be triggered
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

  std::cout << "IAB handover t=" << Simulator::Now ().GetSeconds ()
            << "s: trigger IAB backhaul handover, MT rnti=" << rnti
            << " serving(backhaulRrc cellId)=" << iab->GetBackhaulRrc ()->GetCellId ()
            << " from donor cell " << src->GetCellId () << " -> target cell " << tgtCellId << std::endl;

  if (srcRrc->HasUeManager (rnti))
    {
      // The (src,tgt,oldRnti) tuple is carried through globals until HandoverEndOk consumes it, so
      // handovers must not overlap. Abort clearly if a new one is triggered while one is pending
      // (space handovers wider than one RA/EndOk latency, i.e. increase hoTime).
      NS_ABORT_MSG_IF (g_hoPending,
                       "IAB backhaul handover triggered while a previous one is still pending; "
                       "increase hoTime so handovers do not overlap");
      g_hoPending = true;
      // Capture (src donor, tgt donor, IAB-MT's pre-handover RNTI) so the HandoverEndOk callback can
      // migrate the descendant UE bearers once the IAB-MT's new RNTI on the target is known.
      g_hoSrcDonor = srcDonor;
      g_hoTgtDonor = tgtDonor;
      g_hoOldIabRnti = rnti;
      srcRrc->SendHandoverRequest (rnti, tgtCellId);
    }
  else
    {
      std::cout << "IAB handover ERROR: no UeManager for IAB MT rnti " << rnti
                << " at source donor cell " << src->GetCellId ()
                << " (IAB not connected?) - handover NOT triggered" << std::endl;
    }
}

// Print the IAB-MT's current serving (backhaul) cell as evidence of re-parenting.
void
PrintIabServingCell (Ptr<NetDevice> iabDev, std::string tag)
{
  Ptr<MmWaveIabNetDevice> iab = iabDev->GetObject<MmWaveIabNetDevice> ();
  if (iab && iab->GetBackhaulRrc ())
    {
      std::cout << "IAB handover t=" << Simulator::Now ().GetSeconds () << "s " << tag
                << ": IAB-MT backhaul RRC cellId=" << iab->GetBackhaulRrc ()->GetCellId ()
                << " rnti=" << iab->GetBackhaulRrc ()->GetRnti ()
                << " state=" << iab->GetBackhaulRrc ()->GetState () << std::endl;
    }
}

// Sample and print UE positions (via std::cout so they are visible in the optimized build, where
// NS_LOG is stripped) to document that the UEs move and stay within the disc.
void
DumpUePositions (NodeContainer ues)
{
  for (uint32_t i = 0; i < ues.GetN (); ++i)
    {
      Ptr<MobilityModel> m = ues.Get (i)->GetObject<MobilityModel> ();
      if (m)
        {
          Vector p = m->GetPosition ();
          std::cout << "UE position t=" << Simulator::Now ().GetSeconds () << " ue=" << ues.Get (i)->GetId ()
                    << " x=" << p.x << " y=" << p.y << std::endl;
        }
    }
}

int
main (int argc, char *argv[])
{
  LogComponentEnable("MmWaveHelper", LOG_LEVEL_INFO);

  CommandLine cmd; 
  unsigned run = 0;
  bool rlcAm = false;
  uint32_t numRelays = 1;
  uint32_t numUes = 10;  // Number of UE nodes/users
  uint32_t rlcBufSize = 50;  // RLC buffer [MB]; sized to prevent buffer overflows and packet drops
  uint32_t interPacketInterval = 10000; 
  uint32_t packetSize = 1400; // bytes; below 1500 to avoid IP fragmentation (MSS < MTU - Headers)
  std::string ccAlgorithm = "ns3::TcpBbr";
  // IAB backhaul handover knobs (matched to the QUIC scenario for a fair comparison).
  uint32_t numSatellites = 4;  // Number of donor satellites in the constellation (numSat-1 handovers)
  double hoTime = 10.0;        // Inter-handover interval [s]: handover k occurs at k*hoTime (0 = disabled)
  double simDuration = 60.0;   // Video/simulation duration [s]
  double targetDt = 30.0;      // DASH target buffer [s] (lower => continuous requests, to test data-plane recovery)
  double maxBufferS = 0.0;     // Hard playback-buffer cap [s] (models dash.js BufferController; 0 = unlimited)
  std::string backhaulRate = "100Mbps";  // LEO satellite backhaul capacity (S1-U feeder rate; arXiv 2012.02136)
  double feederDelay = 0.010;  // LEO feeder/S1-U one-way link delay [s]. Default 10ms (optimistic). Realistic
                               // LEO feeder+service propagation is ~20-40ms one-way; raising it lengthens the
                               // handover interruption into the realistic NTN band and makes it visible.
  std::string abrAlgorithm = "ns3::FdashClient";  // DASH ABR controller: ns3::FdashClient or ns3::BolaClient
  bool enableTraces = false;   // Heavy RLC/MAC/PHY ASCII traces (~12 MB/run); off by default
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
  cmd.AddValue("maxBufferS", "Hard playback-buffer cap [s] (models dash.js BufferController; 0 = unlimited)", maxBufferS);
  cmd.AddValue("backhaulRate", "LEO satellite backhaul capacity / S1-U feeder rate (e.g. 100Mbps)", backhaulRate);
  cmd.AddValue("feederDelay", "LEO feeder/S1-U one-way link delay [s] (default 0.010; realistic LEO ~0.02-0.04)", feederDelay);
  cmd.AddValue("hoExecDelay", "Modeled NTN handover-execution/sync delay [s] added to the interruption (TA re-acq + path switch; default 0; realistic ~0.02-0.07)", g_hoExecDelay);
  cmd.AddValue("abrAlgorithm", "DASH ABR algorithm TypeId (ns3::FdashClient or ns3::BolaClient)", abrAlgorithm);
  cmd.AddValue("traces", "Enable heavy RLC/MAC/PHY ASCII traces (slow; off for campaign)", enableTraces);
  cmd.AddValue("ueMobility", "UEs move randomly within a disc around the IAB (false = static placement)", ueMobility);
  cmd.AddValue("ueSpeed", "UE random-waypoint speed [m/s]", ueSpeed);
  cmd.AddValue("ueRadiusMax", "Radius [m] of the UE mobility boundary around the IAB", ueRadiusMax);
  cmd.Parse(argc, argv);

  // Validate CLI so out-of-range values fail cleanly instead of crashing or blowing up memory.
  NS_ABORT_MSG_IF (numSatellites < 1, "numSat must be >= 1 (>= 2 to enable backhaul handover)");
  NS_ABORT_MSG_IF (hoTime < 0.0, "hoTime must be >= 0 (0 disables handover)");
  NS_ABORT_MSG_IF (feederDelay < 0.0, "feederDelay must be >= 0");
  NS_ABORT_MSG_IF (g_hoExecDelay < 0.0, "hoExecDelay must be >= 0");
  NS_ABORT_MSG_IF (simDuration <= 0.0, "simDuration must be > 0");
  NS_ABORT_MSG_IF (ueSpeed < 0.0, "ueSpeed must be >= 0");
  NS_ABORT_MSG_IF (ueMobility && ueRadiusMax <= 0.0,
                   "ueRadiusMax must be > 0 when ueMobility is enabled (0 collapses waypoints and "
                   "generates ~simDuration/1e-3 waypoints per UE)");

  // RLC buffer configuration to prevent buffer overflow on NTN links.
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

	Config::SetDefault ("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue (2));  // matched to the QUIC scenario for a fair comparison
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
  // TCP parameters tuned for NTN scenarios (high RTT, potential packet loss) and matched to the
  // QUIC configuration for a fair comparison. Some QUIC parameters (MaxTrackedGaps, AckDelayExponent)
  // have no direct TCP equivalent, and some TCP parameters (TcpNoDelay) are protocol-specific.

  // ============================================================================
  // ACKNOWLEDGMENT PARAMETERS
  // ============================================================================
  
  // Delayed-ACK timeout set to 25 ms (RFC 9000 default; matches the QUIC kDelayedAckTimeout).
  Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(MilliSeconds(25)));

  // Delayed-ACK count set to 2 (RFC 9000 recommendation; matches QUIC).
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));
  
  // Disable Nagle's algorithm for low latency. QUIC has no Nagle equivalent, so disabling it makes
  // TCP more comparable and avoids delaying small packets.
  Config::SetDefault("ns3::TcpSocket::TcpNoDelay", BooleanValue(true));
  
  // ============================================================================
  // CONGESTION CONTROL PARAMETERS
  // ============================================================================
  
  // TCP Congestion Control Configuration - Dynamic Selection
  // QUIC: CcType = QuicNewReno (default)
  // Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TypeId::LookupByName(ccAlgorithm)));
  
  // Optional: cap the initial slow-start threshold at 32 KB (~21 packets) to enter congestion
  // avoidance sooner. Left disabled to match QUIC RFC compliance (default threshold is infinite).
  // Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue(32*1024));
  
  // Initial congestion window: 10 segments (matches QUIC's default 10*segmentSize).
  Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
  
  // Minimum RTO 200 ms (TCP default is 1 s; matches QUIC's kMinRTOTimeout for a fair comparison).
  Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(200)));
  
  // Connection timeout for NTN scenarios (high RTT)
  Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(6.0)));
  
  // Data retries
  Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(6));
  
  // Segment size (matches the QUIC InitialPacketSize).
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
  
  // ============================================================================
  // BUFFER PARAMETERS (MATCHES QUIC)
  // ============================================================================
  // TCP socket buffers: 64 MB, matched to the QUIC socket/stream buffers and flow-control windows
  // (ns3::QuicSocketBase::Socket*BufSize / MaxData / MaxStreamData, all 64 MB) so the comparison is
  // fair. 64 MB is ~120 s of buffering; TCP alone would be fine with far less.
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(64*1024*1024));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(64*1024*1024));
  
  // Enable pacing for TCP (to match QUIC).
  Config::SetDefault("ns3::TcpSocketState::EnablePacing", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocketState::PaceInitialWindow", BooleanValue(true));
  
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
  // These are protocol-specific features; TCP handles the equivalent behavior via its own mechanisms.
 
  // Enable multi-beam functionality
//  Config::SetDefault("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue(2));
  Config::SetDefault("ns3::MmWaveHelper::Scheduler", StringValue("ns3::MmWavePaddedHbfMacScheduler"));

  // Constrain the satellite backhaul to a realistic LEO capacity by rate-limiting the S1-U feeder
  // link between the donor (satellite) and the core. Makes the satellite backhaul the end-to-end
  // bottleneck so the handover's brief radio outage produces an observable congestion-window
  // collapse. Default 100 Mbps (5G-NR-NTN Ka-band; arXiv 2012.02136).
  Config::SetDefault("ns3::MmWavePointToPointEpcHelper::S1uLinkDataRate", DataRateValue(DataRate(backhaulRate)));
  // Raise the S1-U MTU above the largest tunneled datagram so nothing IP-fragments at the PGW.
  // (The default 2000 fragmented large QUIC datagrams, which ns-3 reassembly corrupted; TCP segments
  // never exceeded it, but the config is kept identical to the QUIC scenario.)
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

  // Satellite (eNB) mobility: WaypointMobilityModel moving in +X at 7.56 km/s (Starlink 550 km
  // circular-orbit ground speed: v = sqrt(mu/r), mu=398600 km^3/s^2, r=6921 km => ~7.59 km/s).
  double satVelocity = 7560.0; // m/s
  
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::WaypointMobilityModel");
  enbmobility.Install (enbNodes);
  
  double minSimulationDuration = stopTime;

  // Space the donor satellites along the orbital track by satVelocity*hoTime, so a new donor reaches
  // the zenith above the IAB every hoTime seconds. With the Starlink single-plane values
  // (v=7.56 km/s, hoTime=262 s) this gives ~1,980 km spacing (~22 satellites/plane).
  // Handovers fire at the equal-elevation crossover t=(k-0.5)*hoTime (~29 deg).
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
    // pedestrian random-waypoint speed (ueSpeed). Each UE gets a pre-computed random-waypoint track
    // (uniform-area points inside the disc, straight legs at ueSpeed) so the mmWave channel sees real
    // UE motion/Doppler. Deterministic per RngRun and matched to the QUIC scenario (same UE tracks).
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

  // --- IAB backhaul handover wiring ---------------------------------------------------------------
  // Set up X2 interfaces between donor satellites so the IAB backhaul can hand over between them.
  if (enbmmWaveDevs.GetN () > 1)
  {
    mmwaveHelper->AddX2Interface (enbNodes);
    NS_LOG_UNCOND("IAB handover X2 interfaces set up between " << enbNodes.GetN() << " donor satellites");
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
  // Schedule a chain of IAB backhaul handovers across the constellation: donor k is overhead at
  // t=k*hoTime; handover k (k=1..numSat-1) fires at the equal-elevation crossover t=(k-0.5)*hoTime
  // (~29 deg), when the setting donor k-1 and rising donor k are at equal elevation.
  // Time/ephemeris-scheduled (3GPP TR 38.821 NTN CHO), not a measured elevation threshold.
  if (hoTime > 0.0 && enbmmWaveDevs.GetN () > 1 && numRelays > 0)
  {
    for (uint32_t k = 1; k < enbmmWaveDevs.GetN (); ++k)
    {
      double t = ((double)k - 0.5) * hoTime;
      Simulator::Schedule (Seconds (t), &TriggerIabBackhaulHandover,
                           iabmmWaveDevs.Get (0), enbmmWaveDevs.Get (k - 1), enbmmWaveDevs.Get (k));
      Simulator::Schedule (Seconds (t + 0.5), &PrintIabServingCell, iabmmWaveDevs.Get (0),
                           std::string ("HO") + std::to_string (k) + "+0.5");
      NS_LOG_UNCOND("IAB handover Scheduled handover " << k << " at t=" << t
                    << "s (donor " << (k - 1) << " -> donor " << k << ")");
    }
  }
  // --- end IAB backhaul handover wiring -----------------------------------------------------------

  // Install and start applications on UEs and remote host.
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  
  // DASH-over-TCP configuration, tuned for QoE and matched to the QUIC scenario. A larger target
  // buffer reduces rebuffering; 45-60 s is realistic for high-latency, variable-throughput NTN links.
  double target_dt = targetDt;  // Target buffering time [s] (CLI-configurable; matches QUIC)
  // DASH playback buffer: holds targetDt (30 s, up to ~54 s with BOLA) of buffered video. With the
  // 15 Mbps-capped ladder that is <=~100 MB, so 128 MB suffices; matched to the QUIC scenario.
  uint32_t bufferSpace = 128*1024*1024;  // 128 MB (matches QUIC)

  double window = 50;  // Throughput measurement window [ms]; 50 ms gives stable, smooth adaptation (matches QUIC)

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
    
  if (enableTraces) { mmwaveHelper->EnableTraces (); }  // Heavy RLC/MAC/PHY ASCII traces; off by default (speed/disk)
  
  // Server starts early to ensure it's ready before clients connect
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after the server, staggered by 0.25 s each (matched to the QUIC scenario). TCP
  // tolerates simultaneous starts, but the offset is kept so both transports see the same client
  // arrival schedule.
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
  
  // Per-connection context-based TCP trace hookup. A wildcard SocketList/* matches each data socket;
  // scheduled at t=0.5 (after connections establish) and writes per-(node,conn) files, so a
  // multi-connection server does not merge all connections into one file.
  g_tcpServerNodeId = remoteHost->GetId();
  Simulator::Schedule(Seconds(0.5), &ConnectTcpLayerTracesWithRetry, 0);
  NS_LOG_UNCOND("  Scheduled context-based TCP trace hookup (wildcard SocketList/*) at t=0.5s, server node " << g_tcpServerNodeId);

  // BBR stats CSV output (bbr_stats_TCP.csv), gated on the BBR_STATS_CSV=1 environment variable.
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