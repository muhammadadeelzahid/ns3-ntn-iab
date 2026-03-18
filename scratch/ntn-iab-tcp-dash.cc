
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
void DashClientTxTrace(uint32_t ueId, Ptr<const Packet> packet)
{
  if (g_dashClientTxFiles.find(ueId) == g_dashClientTxFiles.end())
  {
    std::string filename = "DashClientTx_TCP_UE_" + std::to_string(ueId) + ".txt";
    g_dashClientTxFiles[ueId] = new std::ofstream(filename.c_str());
    *g_dashClientTxFiles[ueId] << "# DASH Client " << ueId << " - Segment Requests Transmitted" << std::endl;
    *g_dashClientTxFiles[ueId] << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes" << std::endl;
    g_dashClientTxPackets[ueId] = 0;
    g_dashClientTxBytes[ueId] = 0;
  }
  
  g_dashClientTxPackets[ueId]++;
  g_dashClientTxBytes[ueId] += packet->GetSize();
  
  *g_dashClientTxFiles[ueId] << Simulator::Now().GetSeconds() << "\t"
                             << packet->GetSize() << "\t"
                             << g_dashClientTxPackets[ueId] << "\t"
                             << g_dashClientTxBytes[ueId] << std::endl;
}

// DASH Client Rx Trace (when client receives video segments - MPEG frames)
void DashClientRxTrace(uint32_t ueId, Ptr<const Packet> packet)
{
  if (g_dashClientRxFiles.find(ueId) == g_dashClientRxFiles.end())
  {
    std::string filename = "DashClientRx_TCP_UE_" + std::to_string(ueId) + ".txt";
    g_dashClientRxFiles[ueId] = new std::ofstream(filename.c_str());
    *g_dashClientRxFiles[ueId] << "# DASH Client " << ueId << " - Video Segments (MPEG frames) Received" << std::endl;
    *g_dashClientRxFiles[ueId] << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes" << std::endl;
    g_dashClientRxPackets[ueId] = 0;
    g_dashClientRxBytes[ueId] = 0;
  }
  
  g_dashClientRxPackets[ueId]++;
  g_dashClientRxBytes[ueId] += packet->GetSize();
  
  *g_dashClientRxFiles[ueId] << Simulator::Now().GetSeconds() << "\t"
                             << packet->GetSize() << "\t"
                             << g_dashClientRxPackets[ueId] << "\t"
                             << g_dashClientRxBytes[ueId] << std::endl;
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

int
main (int argc, char *argv[])
{
  // Enable DASH logging for debugging
  // LogComponentEnable("DashClient", LOG_LEVEL_ALL);  // LOG_LEVEL_LOGIC to see ConnectionSucceeded/Failed
  // LogComponentEnable("DashServer", LOG_LEVEL_ALL);
  // LogComponentEnable("HttpParser", LOG_LEVEL_INFO);
  LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);
  
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
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);  
  cmd.AddValue("ccAlgorithm", "TCP Congestion Control Algorithm", ccAlgorithm);
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

	Config::SetDefault ("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue (4));
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
  // TCP Socket buffer configuration - must be large enough for high bitrate segments
  // QUIC: SocketSndBufSize = SocketRcvBufSize = 256 MB
  // For 66 Mbps: average segment ~15.74 MB, max segment ~31.47 MB
  // Increased to 256 MB to hold 8+ full segments and prevent blocking during network fluctuations (matches QUIC)
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(512*1024*1024));  // 256 MB (8x max segment) - increased for better QoE (matches QUIC)
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(512*1024*1024));  // 256 MB (8x max segment) - increased for better QoE (matches QUIC)
  
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
 
  enbNodes.Create(1);
  iabNodes.Create(numRelays);
  ueNodes.Create(numUes);
  
  NS_LOG_UNCOND("Actually created " << ueNodes.GetN() << " UE nodes");
  NS_LOG_UNCOND("Actually created " << iabNodes.GetN() << " IAB nodes");
  NS_LOG_UNCOND("Actually created " << enbNodes.GetN() << " eNB nodes");
  NS_LOG_UNCOND("================================\n");
  
  // Get current stopTime (line 1024)
  double desiredVideoDuration = 90.0;
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
  double target_dt = 30.0;  // Target buffering time (realistic for NTN while preventing interruptions, matches QUIC)
  // DASH bufferSpace: should hold multiple segments for smooth playback
  // For 66 Mbps: ~6 segments in 100 MB, increase to 512 MB for 30+ segments
  // Larger buffer provides more headroom to prevent interruptions during network fluctuations (matches QUIC)
  uint32_t bufferSpace = 512*1024*1024;  // 512 MB (30+ segments at 66 Mbps) - increased for maximum QoE to prevent interruptions (matches QUIC)

  double window = 50;  // Throughput measurement window in milliseconds (increased from 5ms to 50ms for more stable measurements and smoother adaptation, matches QUIC)

  std::string algorithm = "ns3::FdashClient";  // DASH adaptation algorithm
  


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
      dashClient->TraceConnectWithoutContext("Tx", MakeBoundCallback(&DashClientTxTrace, u));
      dashClient->TraceConnectWithoutContext("Rx", MakeBoundCallback(&DashClientRxTrace, u));
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
  
  // Server starts early to ensure it's ready before clients connect
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after servers (all start at same time, matching QUIC for fair comparison)
  for (uint32_t i = 0; i < clientApps.GetN(); ++i)
  {
    double clientStartTime = 0.1;  // All clients start at 0.1s
    clientApps.Get(i)->SetStartTime(Seconds(clientStartTime));
    // Stop apps 1 second before simulation stops to allow cleanup
    clientApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
    NS_LOG_UNCOND("DASH Client " << i << " scheduled to start at t=" << clientStartTime << "s");
  }
  
  Simulator::Stop (Seconds (stopTime + 2.0));

  NS_LOG_UNCOND("\n=== Scheduling TCP Trace Connections (DOWNLINK) ===");
  
  // DOWNLINK: Clients are on UE nodes, Server is on remoteHost
  // Connect traces for each UE node (TCP clients) - schedule after apps start and TCP sockets are created
  // Matching QUIC: clientStartTime=0.1, add 0.05s buffer for handshake
  double clientStartTime = 0.1;
  Time clientConnectionTime = Seconds(clientStartTime + 0.05);
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    uint32_t nodeId = ueNodes.Get(u)->GetId();
    Simulator::Schedule(clientConnectionTime, &Traces, nodeId, "./client", ".txt");
    NS_LOG_UNCOND("  Scheduled TCP traces for UE Node " << nodeId << " (UE " << u 
                  << ", DASH client) at t=" << clientConnectionTime.GetSeconds() 
                  << "s (client starts at t=" << clientStartTime << "s)");
  }
  
  // Connect traces for remoteHost (TCP server) - schedule after server starts and TCP sockets are created
  // Matching QUIC: Server starts at 0.1, add 0.05s buffer for handshake
  uint32_t serverNodeId = remoteHost->GetId();
  Time serverTraceTimeSched = Seconds(0.1 + 0.05);
  Simulator::Schedule(serverTraceTimeSched, &Traces, serverNodeId, "./server", ".txt");
  NS_LOG_UNCOND("  Scheduled TCP traces for Server Node " << serverNodeId << " (remoteHost, DASH server) at t=" << serverTraceTimeSched.GetSeconds() << "s");

  // Schedule BBR stats trace connection (sockets created when connections establish)
  // Commented out: BBR stats CSV output (bbr_stats_TCP.csv)
  // Simulator::Schedule(clientConnectionTime, []() {
  //   Config::MatchContainer bbrMatches = Config::LookupMatches("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/CongestionOps/$ns3::TcpBbr");
  //   bbrMatches.ConnectFailSafe("BbrStatsTrace", MakeCallback(&TcpBbrStatsCsvCallback));
  //   if (bbrMatches.GetN() > 0)
  //     NS_LOG_UNCOND("  Connected BBR stats trace to " << bbrMatches.GetN() << " TcpBbr instance(s)");
  // });
  
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
      if (g_dashClientTxPackets.find(u) != g_dashClientTxPackets.end())
      {
        NS_LOG_UNCOND("  DASH Requests sent: " << g_dashClientTxPackets[u] 
                     << " packets (" << g_dashClientTxBytes[u] << " bytes)");
      }
      if (g_dashClientRxPackets.find(u) != g_dashClientRxPackets.end())
      {
        NS_LOG_UNCOND("  DASH Video received: " << g_dashClientRxPackets[u] 
                     << " packets (" << g_dashClientRxBytes[u] << " bytes)");
        double avgThroughput = (g_dashClientRxBytes[u] * 8.0) / (stopTime * 1000000.0);
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