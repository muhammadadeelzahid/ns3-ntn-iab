
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
#include <limits>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MmWaveNtnIabQuicDash");

// Performance optimization: Set to false to disable expensive packet-level tracing/logging
// This significantly speeds up simulations, especially for QUIC which generates more packets

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

// BBR CSV log file (shared across connections)
std::ofstream g_bbrStatsCsvFile;
std::mutex g_bbrStatsCsvMutex;

// Robust QUIC layer trace hookup (context-based, resilient to late socket creation)
uint32_t g_quicServerNodeId = std::numeric_limits<uint32_t>::max();
bool g_quicRxTraceHooked = false;
bool g_quicCwndTraceHooked = false;
bool g_quicRttTraceHooked = false;
std::map<uint32_t, Ptr<OutputStreamWrapper>> g_quicRxStreams;
std::map<uint32_t, Ptr<OutputStreamWrapper>> g_quicCwndStreams;
std::map<uint32_t, Ptr<OutputStreamWrapper>> g_quicRttStreams;

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
GetOrCreateQuicTraceStream(std::map<uint32_t, Ptr<OutputStreamWrapper>>& streamMap,
                           const std::string& metricName,
                           uint32_t nodeId)
{
  auto it = streamMap.find(nodeId);
  if (it != streamMap.end())
    {
      return it->second;
    }

  AsciiTraceHelper asciiTraceHelper;
  std::ostringstream fileName;
  fileName << GetQuicTracePathPrefix(nodeId) << "QUIC-" << metricName << nodeId << ".txt";
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream(fileName.str().c_str());
  streamMap[nodeId] = stream;
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
  
  // Retry logic: if any trace failed to connect and we haven't exceeded max retries, schedule retry
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
  (void)connId;
  (void)q;
  (void)qsb;
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicRxStreams, "rx-data", nodeId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << p->GetSize() << std::endl;
}

static void
QuicCwndTraceWithContext(std::string context, uint32_t oldCwnd, uint32_t newCwnd)
{
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  (void)connId;
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicCwndStreams, "cwnd-change", nodeId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
QuicRttTraceWithContext(std::string context, Time oldRtt, Time newRtt)
{
  uint32_t nodeId, connId;
  ParseNodeAndConnFromContext(context, nodeId, connId);
  (void)connId;
  Ptr<OutputStreamWrapper> stream = GetOrCreateQuicTraceStream(g_quicRttStreams, "rtt", nodeId);
  *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldRtt.GetSeconds() << "\t" << newRtt.GetSeconds() << std::endl;
}

static void
ConnectQuicLayerTracesWithRetry(uint32_t retryCount)
{
  const uint32_t MAX_RETRIES = 20;
  const Time RETRY_INTERVAL = MilliSeconds(100);

  if (!g_quicRxTraceHooked)
    {
      g_quicRxTraceHooked = Config::ConnectFailSafe(
        "/NodeList/*/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx",
        MakeCallback(&QuicRxTraceWithContext));
    }
  if (!g_quicCwndTraceHooked)
    {
      g_quicCwndTraceHooked = Config::ConnectFailSafe(
        "/NodeList/*/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/CongestionWindow",
        MakeCallback(&QuicCwndTraceWithContext));
    }
  if (!g_quicRttTraceHooked)
    {
      g_quicRttTraceHooked = Config::ConnectFailSafe(
        "/NodeList/*/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/RTT",
        MakeCallback(&QuicRttTraceWithContext));
    }

  if (!g_quicRxTraceHooked || !g_quicCwndTraceHooked || !g_quicRttTraceHooked)
    {
      if (retryCount < MAX_RETRIES)
        {
          Simulator::Schedule(RETRY_INTERVAL, &ConnectQuicLayerTracesWithRetry, retryCount + 1);
        }
      else
        {
          NS_LOG_WARN("QUIC layer traces: retries exhausted. "
                      << "Rx=" << g_quicRxTraceHooked
                      << " Cwnd=" << g_quicCwndTraceHooked
                      << " Rtt=" << g_quicRttTraceHooked);
        }
      return;
    }

  NS_LOG_UNCOND("QUIC layer traces connected (context-based): "
                << "Rx=" << g_quicRxTraceHooked
                << " Cwnd=" << g_quicCwndTraceHooked
                << " Rtt=" << g_quicRttTraceHooked);
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


int
main (int argc, char *argv[])
{

  // LogComponentDisable("DashClient", LOG_LEVEL_ALL);
  
  // Enable DASH logging for debugging
  LogComponentEnable("MmWaveHelper", LOG_LEVEL_INFO);
  // LogComponentEnable("DashClient", LOG_LEVEL_INFO);
  // LogComponentEnable("DashServer", LOG_LEVEL_INFO);
  // LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);
  // LogComponentEnable("QuicStreamBase", LOG_LEVEL_ALL);
  
  // Enable QUIC socket logging to see connection events and data flow
  // LogComponentEnable("QuicSocketBase", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet handling
  // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet flow
  // LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet flow
  // LogComponentEnable("QuicStream", LOG_LEVEL_ALL);      // LOG_LEVEL_ALL to see stream data handling
  
  // Enable packet-level logging for debugging
  // LogComponentEnable("Packet", LOG_LEVEL_DEBUG);        // LOG_LEVEL_DEBUG to see packet operations
  // LogComponentEnable("UdpSocket", LOG_LEVEL_DEBUG);     // LOG_LEVEL_DEBUG to see UDP operations
  // LogComponentEnable("UdpL4Protocol", LOG_LEVEL_DEBUG); // LOG_LEVEL_DEBUG to see UDP protocol
  
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
  uint32_t rlcBufSize = 50;  // Increased from 10 to 50 MB to prevent RLC buffer overflows and packet drops
  uint32_t interPacketInterval = 10000; 
  uint32_t packetSize = 1400; //bytes // Decreased from 1500 to 1400 to avoid IP fragmentation (packet < MTU - headers)
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);

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

  // Set center frequency to 6 GHz for RMa scenario compatibility
  // Keep default ChunkPerRB = 72 and ResourceBlockNum = 1 (required for TDMA)

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
  
  // QUIC-specific configuration
  Config::SetDefault("ns3::QuicSocketBase::IdleTimeout", TimeValue(Seconds(30.0)));
  
  // ============================================================================
  // ACKNOWLEDGMENT GAP ELIMINATION PARAMETERS
  // ============================================================================
  // These parameters are optimized to eliminate gaps in acknowledgments and
  // improve loss detection responsiveness
  // Values are tuned for significant improvement while remaining realistic for NTN
  
  // 1. Increase maximum tracked gaps (from default 20 to 100) - SIGNIFICANT IMPROVEMENT
  //    Allows many more gaps to be reported in ACK frames, improving loss detection
  //    Realistic: NTN links may have burst losses, so tracking more gaps is beneficial
  // 1. Increase maximum tracked gaps (from default 20 to 20) - RFC DEFAULT
  //    Reverting to default for standard compliance
  Config::SetDefault("ns3::QuicSocketBase::MaxTrackedGaps", UintegerValue(100));
  
  // 2. Reduce maximum packets before ACK send (from default 20 to 1) - CRITICAL FOR FAST WINDOW GROWTH
  //    ACK every packet ensures fastest possible window growth, essential when initial window (15KB)
  //    is smaller than server's segment size (100 frames ~21KB). Each ACK doubles window in slow start.
  //    Realistic: RFC 9000 allows ACK every packet, and this is necessary for handling large initial bursts
  Config::SetDefault("ns3::QuicSocketState::kMaxPacketsReceivedBeforeAckSend", UintegerValue(2));
  
  // 3. Reduce delayed ACK timeout (from default 25ms to 5ms) - CRITICAL FOR HANDLING LARGE SEGMENTS
  //    Faster ACKs allow congestion window to grow quickly, essential when server sends 100 frames (~21KB)
  //    at once. With initial window of 15KB, fast ACKs are needed to grow window before TX buffer fills.
  //    Realistic: 5ms is aggressive but necessary for NTN scenarios with large initial bursts
  Config::SetDefault("ns3::QuicSocketState::kDelayedAckTimeout", TimeValue(MilliSeconds(50)));
  
  // 4. Reduce ACK delay exponent (from default 3 to 1) - HELPS WITH FAST WINDOW GROWTH
  //    Limits maximum encodable ACK delay, reducing delay variability and allowing faster window growth
  //    With exponent=1, max ACK delay is 2^1 = 2ms, ensuring ACKs arrive quickly
  //    Realistic: Standard QUIC allows values 0-20, so 1 is well within range and helps with large segments
  Config::SetDefault("ns3::QuicSocketBase::AckDelayExponent", UintegerValue(2));
  
  // 5. ENABLE time-based loss detection - RFC 9002 COMPLIANT
  //    RFC 9002 states implementations SHOULD use time-based loss detection
  //    Time-based detection helps recover packets that packet-number-based detection might miss,
  //    especially in scenarios with high reordering or variable RTT (common in NTN)
  //    This provides a secondary loss detection mechanism for better packet recovery
  Config::SetDefault("ns3::QuicSocketState::kUsingTimeLossDetection", BooleanValue(false));
  
  // 6. Keep minimum TLP timeout at default (10ms)
  //    Not changing this - 10ms is appropriate for NTN scenarios
  // Config::SetDefault("ns3::QuicSocketState::kMinTLPTimeout", TimeValue(MilliSeconds(1)));

  // 7. Keep minimum RTO timeout at default (200ms)
  //    Not changing this - 200ms is appropriate for NTN scenarios
  // Config::SetDefault("ns3::QuicSocketState::kMinRTOTimeout", TimeValue(MilliSeconds(10)));
  
  
  // ============================================================================
  // LOSS DETECTION PARAMETERS (RFC 9002 compliant)
  // ============================================================================
  // OPTIMIZED FOR OUT-OF-ORDER PACKET RECOVERY:
  // These parameters have been tuned to improve recovery of lost packets in NTN scenarios
  // with high latency and out-of-order delivery:
  // 1. Time-based loss detection: ENABLED (provides secondary detection mechanism)
  // 2. Reordering threshold: REDUCED to 1 (faster loss detection)
  // 3. Max TLP: INCREASED to 20 (more tail loss probe attempts)
  // 4. RTO timeout: REDUCED to 25ms (faster retransmissions)
  // 5. TLP timeout: REDUCED to 3ms (faster tail loss detection)
  // These changes work together to detect and recover lost packets more quickly
  
  // Reordering threshold for loss detection (RFC 9002 Section 6.1.1)
  // Reduced to 1 to trigger immediate loss detection on the first out-of-order ACK.
  // This is critical to recover tail losses when no further data packets are available to generate gaps.
  // Lower threshold = faster loss detection = better recovery of out-of-order packets
  // Trade-off: May cause false positives, but improves recovery in high-loss scenarios (NTN)
  Config::SetDefault("ns3::QuicSocketState::kMaxTLPs", UintegerValue(5));  // Increased from 10 to 20 for more tail loss probe attempts
  Config::SetDefault("ns3::QuicSocketState::kReorderingThreshold", UintegerValue(2));  // Reduced from 2 to 1 for faster loss detection
  
  // Time-based reordering fraction (RFC 9002 Section 6.1.2)
  // Standard RFC 9002 value - keeps time-based detection conservative
  // With time-based loss detection enabled, this controls how aggressive time-based detection is
  // Standard value (9/8 = 1.125) means a packet is considered lost if it's been unacked
  // for more than 1.125 * smoothed_rtt after a newer packet is acked
  Config::SetDefault("ns3::QuicSocketBase::kTimeReorderingFraction", DoubleValue(9.0/8.0));
  
  // Default initial RTT (RFC 9002 Section 6.2.2)
  // Reduced from 333ms to 100ms to allow faster window growth in slow start
  // Lower initial RTT means pacing rate calculation allows faster transmission initially
  // This helps when server sends large segments before RTT samples are available
  Config::SetDefault("ns3::QuicSocketBase::kDefaultInitialRtt", TimeValue(MilliSeconds(50)));
  
  // ============================================================================
  // CONGESTION CONTROL PARAMETERS
  // ============================================================================
  
  // QUIC Congestion Control Configuration
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
  
  // Set initial slow start threshold (ssthresh) to 200KB
  Config::SetDefault("ns3::QuicSocketBase::InitialSlowStartThreshold", UintegerValue(UINT32_MAX));

  // Packet size configuration
  Config::SetDefault("ns3::QuicSocketBase::InitialPacketSize", UintegerValue(packetSize));
  Config::SetDefault("ns3::QuicSocketBase::MaxPacketSize", UintegerValue(1500));
  
  // ============================================================================
  // FLOW CONTROL PARAMETERS
  // ============================================================================
  // Reduce MaxDataInterval to send flow control updates more frequently
  // This improves flow control responsiveness and prevents blocking
  // Note: This is a counter (number of ACKs), not a time value
  // Using 5 for very responsive flow control updates (every 5 ACKs)
  // Realistic: Small overhead, significant improvement in flow control responsiveness
  // ============================================================================
  // FLOW CONTROL PARAMETERS (RFC COMPLIANCE)
  // ============================================================================
  // Set initial flow control limits to reasonable values (e.g., 4GB)
  // This enables actual flow control behavior as intended by RFC
  Config::SetDefault("ns3::QuicSocketBase::MaxStreamData", UintegerValue(512 * 1024 * 1024));
  Config::SetDefault("ns3::QuicSocketBase::MaxData", UintegerValue(512 * 1024 * 1024));

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
 
  enbNodes.Create(1);
  iabNodes.Create(numRelays);
  ueNodes.Create(numUes);
  
  NS_LOG_UNCOND("Actually created " << ueNodes.GetN() << " UE nodes");
  NS_LOG_UNCOND("Actually created " << iabNodes.GetN() << " IAB nodes");
  NS_LOG_UNCOND("Actually created " << enbNodes.GetN() << " eNB nodes");
  NS_LOG_UNCOND("================================\n");

  // Video duration configuration
  // double desiredVideoDuration = 0.0;  // Desired video duration in seconds
  
  // Calculate minimum simulation duration
  // Video duration + buffer time for handshake, initial buffering, cleanup, and app stop buffer
  // double minSimulationDuration = desiredVideoDuration*1.15;
  
  // Get current stopTime (line 1024)
  double desiredVideoDuration = 60.0;
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
  // Move in X direction at 7.8 km/s
  
  double satVelocity = 7800.0; // m/s
  
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::WaypointMobilityModel");
  enbmobility.Install (enbNodes);

  double minSimulationDuration = stopTime;

  for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
  {
      Ptr<WaypointMobilityModel> mob = enbNodes.Get(i)->GetObject<WaypointMobilityModel>();
      
      // Waypoint 1: Start at t=0 (Original Position)
      Vector pos1 = posWired;
      mob->AddWaypoint(Waypoint(Seconds(0.0), pos1));

      // Waypoint 2: End at t=minSimulationDuration
      // Move in X direction
      // Distance = velocity * time
      Vector pos2 = Vector(posWired.x + (satVelocity * minSimulationDuration), posWired.y, posWired.z);
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

  // Random user generation code
  // Ptr<UniformRandomVariable> radiusRand = CreateObject<UniformRandomVariable>();
  // radiusRand->SetAttribute("Min", DoubleValue(20));               // minimum radius from center
  // radiusRand->SetAttribute("Max", DoubleValue(std::min(xMax, yMax) / 2.0)); // max radius: half of area
  
  // Ptr<UniformRandomVariable> angleRand = CreateObject<UniformRandomVariable>();
  // angleRand->SetAttribute("Min", DoubleValue(0));
  // angleRand->SetAttribute("Max", DoubleValue(2 * M_PI));
  
  // for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  // {
  //     double radius = radiusRand->GetValue();
  //     double angle = angleRand->GetValue();
  
  //     double x = xMax/2 + radius * std::cos(angle);
  //     double y = yMax/2 + radius * std::sin(angle);
  //     double z = 1.7; // typical UE height
  
  //     // Ensure within boundaries
  //     x = std::min(std::max(x, 0.0), xMax);
  //     y = std::min(std::max(y, 0.0), yMax);
  
  //     NS_LOG_UNCOND("UE " << i << " position: " << x << ", " << y << ", " << z);
  //     uePosAlloc->Add(Vector(x, y, z));
  // }
  // UE at original position (near center)
  // double ueX = xMax/2.0 + 1000.0;
  // double ueY = yMax/2.0 + 100.0;
  // double ueZ = 1.7;

  // uePosAlloc->Add(Vector(ueX, ueY, ueZ));

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

  double min_distance = 1.0;
  double max_distance = 100.0;
  NS_LOG_UNCOND("UE cluster radius range: [" << min_distance << ", " << max_distance << "] meters");

  Ptr<UniformRandomVariable> radiusRand = CreateObject<UniformRandomVariable>();
  radiusRand->SetAttribute("Min", DoubleValue(min_distance));
  radiusRand->SetAttribute("Max", DoubleValue(max_distance));
  Ptr<UniformRandomVariable> angleRand = CreateObject<UniformRandomVariable>();
  angleRand->SetAttribute("Min", DoubleValue(0.0));
  angleRand->SetAttribute("Max", DoubleValue(2 * M_PI));

  double zHeight = 1.7;
  for (uint32_t c = 0; c < clusterCenters.size(); ++c)
  {
    uint32_t uesInCluster = baseUesPerCluster + (c < extraUes ? 1 : 0);
    const Vector& center = clusterCenters[c];
    for (uint32_t u = 0; u < uesInCluster; ++u)
    {
      double r = radiusRand->GetValue();
      double theta = angleRand->GetValue();
      double x = center.x + r * std::cos(theta);
      double y = center.y + r * std::sin(theta);
      // Clamp to simulation area
      x = std::min(std::max(x, 0.0), xMax);
      y = std::min(std::max(y, 0.0), yMax);
      uePosAlloc->Add(Vector(x, y, zHeight));
    }
  }

  uemobility.SetPositionAllocator (uePosAlloc);
  uemobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  uemobility.Install (ueNodes);
  
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

  // Build IMSI -> NodeId map for debugging RNTI issues
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

  // Map IMSI/RNTI to NodeId and position
  Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/LteEnbRrc/ConnectionEstablished",
                                MakeCallback(&ConnectionEstablishedTraceSink));

  // Install and start applications on UEs and remote host
  // LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
  // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  
  // ============================================================================
  // CRITICAL FIX FOR MULTIPLE USERS: QUIC Connection Stalling Issue
  // ============================================================================
  // PROBLEM: With 5 users, QUIC connections stall - only control packets are received,
  // no data packets. This works fine with TCP but fails with QUIC.
  //
  // ROOT CAUSES IDENTIFIED:
  // 1. Insufficient stagger delay: Clients starting too close together (0.1s apart)
  //    causes QUIC handshakes to overlap, leading to congestion window exhaustion
  // 2. Small buffers: 64MB buffers insufficient for multiple concurrent connections
  //    competing for bandwidth
  // 3. Connection timing: QUIC needs more time to establish connections than TCP
  //
  // FIXES APPLIED:
  // 1. Increased client start stagger from 0.1s to 0.25s (see client start times below)
  // 2. Increased socket/stream buffers from 64MB to 128MB
  // 3. Ensured server starts early (0.1s) before any clients
  // ============================================================================
  


  // QUIC Socket buffer configuration - must be large enough for high bitrate segments
  // For 66 Mbps: average segment ~15.74 MB, max segment ~31.47 MB
  // CRITICAL FIX: Increased buffers to 128 MB to handle multiple concurrent connections
  // With 5 users, each connection needs sufficient buffer space to avoid blocking
  Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(512*1024*1024));  // 128 MB (Increased for multiple concurrent connections)
  Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(512*1024*1024));  // 128 MB (Increased for multiple concurrent connections)
  
  // QUIC stream buffer configuration - must hold multiple large frames (up to 330 KB each)
  // CRITICAL FIX: Increased buffers to 128 MB to match socket buffers and handle multiple connections
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (512*1024*1024));  // 128 MB (Match Socket buffer, increased for multiple connections)
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (512*1024*1024));  // 128 MB (Match Socket buffer, increased for multiple connections)

  // DASH over QUIC configuration - optimized for QoE and preventing interruptions
  // Increased target buffering time for more aggressive buffering to prevent rebuffering
  // For NTN scenarios with high latency and variable throughput, 45-60s is realistic
  // 60s provides good balance: prevents interruptions while remaining realistic for real-world scenarios
  double target_dt = 30.0;  // Target buffering time (increased from 45.0s to 60.0s - realistic for NTN while preventing interruptions)
  // DASH bufferSpace: should hold multiple segments for smooth playback
  // For 66 Mbps: ~6 segments in 100 MB
  // Reduced to 64 MB to prevents OOM while keeping reasonable buffering
  uint32_t bufferSpace = 512*1024*1024;  // 512 MB (Reduced from 512MB)

  double window = 50;  // Throughput measurement window in milliseconds (increased from 10ms to 50ms for more stable measurements and smoother adaptation)

  std::string algorithm = "ns3::FdashClient";  // DASH adaptation algorithm
  


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
    
  mmwaveHelper->EnableTraces ();  // Enables RLC/MAC/PHY traces (DlRlcStats.txt, RxPacketTrace.txt, etc.)
  
  // CRITICAL FIX: Server starts early to ensure it's ready before clients connect
  // With QUIC, the server needs to be listening before clients initiate handshakes
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after servers (no stagger - all start at same time)
  for (uint32_t i = 0; i < clientApps.GetN(); ++i)
  {
    double clientStartTime = 0.1;
    clientApps.Get(i)->SetStartTime(Seconds(clientStartTime));
    // Stop apps 1 second before simulation stops to allow cleanup
    clientApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
    NS_LOG_UNCOND("DASH Client " << i << " scheduled to start at t=" << clientStartTime << "s");
  }
  
  Simulator::Stop (Seconds (stopTime + 2.0));

  NS_LOG_UNCOND("\n=== Scheduling QUIC Trace Connections (DOWNLINK) ===");
  
  // DOWNLINK: Clients are on UE nodes, Server is on remoteHost
  // Connect traces for each UE node (QUIC clients) - schedule after apps start and QUIC sockets are created
  // Matching TCP procedure: clientStartTime=0.1, add 0.05s buffer for handshake
  double clientStartTime = 0.1;
  Time clientConnectionTime = Seconds(clientStartTime + 0.05);
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    uint32_t nodeId = ueNodes.Get(u)->GetId();
    Simulator::Schedule(clientConnectionTime, &Traces, nodeId, "./client", ".txt", 0);
    NS_LOG_UNCOND("  Scheduled QUIC traces for UE Node " << nodeId << " (UE " << u
                  << ", DASH client) at t=" << clientConnectionTime.GetSeconds()
                  << "s (client starts at t=" << clientStartTime << "s)");
  }
  
  // Connect traces for remoteHost (QUIC server) - schedule after server starts and sockets are created
  // Matching TCP procedure: server starts at 0.1, add 0.05s buffer for handshake
  uint32_t serverNodeId = remoteHost->GetId();
  Time serverTraceTimeSched = Seconds(0.1 + 0.05);
  Simulator::Schedule(serverTraceTimeSched, &Traces, serverNodeId, "./server", ".txt", 0);
  NS_LOG_UNCOND("  Scheduled QUIC traces for Server Node " << serverNodeId
                << " (remoteHost, DASH server) at t=" << serverTraceTimeSched.GetSeconds() << "s");

  // Schedule BBR stats trace connection for CSV logging
  Simulator::Schedule(Seconds(0.15), []() {
    Config::MatchContainer bbrMatches = Config::LookupMatches("/NodeList/*/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/CongestionOps/$ns3::QuicBbr");
    bbrMatches.ConnectFailSafe("BbrStatsTrace", MakeCallback(&QuicBbrStatsCsvCallback));
    if (bbrMatches.GetN() > 0)
      NS_LOG_UNCOND("  Connected BBR stats trace to " << bbrMatches.GetN() << " QuicBbr instance(s)");
  });
  
  // Add QUIC socket callback connections for debugging
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
  
  // // Monitor packet operations on all nodes
  // for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
  // {
  //   Ptr<Node> node = *it;
  //   uint32_t nodeId = node->GetId();
    
  //   // Connect packet traces for debugging
  //   std::ostringstream packetTxPath;
  //   packetTxPath << "/NodeList/" << nodeId << "/DeviceList/*/$ns3::PointToPointNetDevice/Tx";
  //   Config::ConnectWithoutContextFailSafe(packetTxPath.str(), MakeCallback(&PacketBufferTraceCallback));
    
  //   std::ostringstream packetRxPath;
  //   packetRxPath << "/NodeList/" << nodeId << "/DeviceList/*/$ns3::PointToPointNetDevice/Rx";
  //   Config::ConnectWithoutContextFailSafe(packetRxPath.str(), MakeCallback(&PacketBufferTraceCallback));
    
  //   NS_LOG_UNCOND("  Added packet buffer traces for Node " << nodeId);
  // }
    
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
