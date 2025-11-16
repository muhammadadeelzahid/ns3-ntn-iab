
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
#include "ns3/dash-module.h"
#include <iomanip>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MmWaveNtnIabQuicDash");

// Performance optimization: Set to false to disable expensive packet-level tracing/logging
// This significantly speeds up simulations, especially for QUIC which generates more packets
static bool g_enableVerbosePacketTracing = false;

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
    std::string filename = "DashClientTx_UE_" + std::to_string(ueId) + ".txt";
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
    std::string filename = "DashClientRx_UE_" + std::to_string(ueId) + ".txt";
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
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
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
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
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

static void
Traces(uint32_t serverId, std::string pathVersion, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/CongestionWindow";
  uint32_t cwMatches = Config::LookupMatches(pathCW.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - CongestionWindow matches: " << cwMatches);

  std::ostringstream fileCW;
  fileCW << pathVersion << "QUIC-cwnd-change"  << serverId << "" << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/RTT";
  uint32_t rttMatches = Config::LookupMatches(pathRTT.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - RTT matches: " << rttMatches);

  std::ostringstream fileRTT;
  fileRTT << pathVersion << "QUIC-rtt"  << serverId << "" << finalPart;

  std::ostringstream pathRCWnd;
  pathRCWnd<< "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/RWND";

  std::ostringstream fileRCWnd;
  fileRCWnd<<pathVersion << "QUIC-rwnd-change"  << serverId << "" << finalPart;

  std::ostringstream fileName;
  fileName << pathVersion << "QUIC-rx-data" << serverId << "" << finalPart;
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  uint32_t rxMatches = Config::LookupMatches(pathRx.str().c_str()).GetN();
  NS_LOG_UNCOND("Node " << serverId << " (" << pathVersion << ") - Rx matches: " << rxMatches);

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  // Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  // Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  // Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
  // Config::ConnectWithoutContextFailSafe (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));
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
    udpL4TxFile.open("udp_l4_tx.txt", std::ios::out);
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
    udpL4RxFile.open("udp_l4_rx.txt", std::ios::out);
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
    ipv4L3TxFile.open("ipv4_l3_tx.txt", std::ios::out);
  }
  DumpPacketHex(ipv4L3TxFile, packet, "IPV4_L3_TX Interface=" + std::to_string(interface));
  ipv4L3TxFile.flush();
}

void Ipv4L3RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!ipv4L3RxFile.is_open())
  {
    ipv4L3RxFile.open("ipv4_l3_rx.txt", std::ios::out);
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
    p2pTxFile.open("p2p_tx.txt", std::ios::out);
  }
  DumpPacketHex(p2pTxFile, packet, "P2P_TX");
  p2pTxFile.flush();
}

void P2PRxCallback(Ptr<const Packet> packet)
{
  if (!g_enableVerbosePacketTracing) return;  // Skip expensive operations for performance
  
  if (!p2pRxFile.is_open())
  {
    p2pRxFile.open("p2p_rx.txt", std::ios::out);
  }
  DumpPacketHex(p2pRxFile, packet, "P2P_RX");
  p2pRxFile.flush();
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
  LogComponentEnable("MpegPlayer", LOG_LEVEL_ALL);
  
  // Enable QUIC socket logging to see connection events and data flow
  // LogComponentEnable("QuicSocketBase", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet handling
  // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet flow
  // LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);  // LOG_LEVEL_ALL to see detailed packet flow
  // LogComponentEnable("QuicStream", LOG_LEVEL_ALL);      // LOG_LEVEL_ALL to see stream data handling
  
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
  LogComponentEnable("MmWaveHelper", LOG_LEVEL_ALL);
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
  uint32_t numUes = 1;  // Number of UE nodes/users
  uint32_t rlcBufSize = 10;
  uint32_t interPacketInterval = 10000; 
  uint32_t packetSize = 1500; //bytes // Increased to accommodate DASH frames
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);
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

  // Set center frequency to 6 GHz for RMa scenario compatibility
  Config::SetDefault ("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue (6.0e9)); 
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
  Config::SetDefault("ns3::QuicSocketBase::MaxTrackedGaps", UintegerValue(100));
  
  // 2. Reduce maximum packets before ACK send (from default 20 to 5) - SIGNIFICANT IMPROVEMENT FOR CONGESTION AVOIDANCE
  //    Forces much more frequent ACKs, significantly reducing gaps and detecting congestion faster
  //    Realistic: 5 packets is aggressive but critical for congestion avoidance - detects losses 2x faster
  Config::SetDefault("ns3::QuicSocketState::kMaxPacketsReceivedBeforeAckSend", UintegerValue(5)); // REDUCED FROM 10 TO 5
  
  // 3. Reduce delayed ACK timeout (from default 25ms to 10ms) - SIGNIFICANT IMPROVEMENT FOR CONGESTION AVOIDANCE
  //    Sends ACKs much more frequently, reducing acknowledgment delays by 60% for faster congestion detection
  //    Realistic: 10ms is still safe for NTN and critical for detecting congestion quickly
  Config::SetDefault("ns3::QuicSocketState::kDelayedAckTimeout", TimeValue(MilliSeconds(10))); // REDUCED FROM 15ms TO 10ms
  
  // 4. Reduce ACK delay exponent (from default 3 to 2) - MODERATE IMPROVEMENT
  //    Limits maximum encodable ACK delay, reducing delay variability
  //    Realistic: Standard QUIC allows values 0-20, so 2 is well within range
  Config::SetDefault("ns3::QuicSocketBase::AckDelayExponent", UintegerValue(2));
  
  // 5. DISABLE time-based loss detection to prevent premature timeout triggers
  //    When enabled with aggressive reordering threshold, it can trigger timeouts
  //    before packets are actually lost, causing empty vector crashes
  //    Keeping at default (false) for stability - packet number-based detection is sufficient
  // Config::SetDefault("ns3::QuicSocketState::kUsingTimeLossDetection", BooleanValue(true));
  
  // 6. Keep minimum TLP timeout at default (10ms)
  //    Not changing this - 10ms is appropriate for NTN scenarios
  // Config::SetDefault("ns3::QuicSocketState::kMinTLPTimeout", TimeValue(MilliSeconds(10)));
  
  // ============================================================================
  // LOSS DETECTION PARAMETERS (RFC 9002 compliant)
  // ============================================================================
  
  // Reordering threshold for loss detection (RFC 9002 Section 6.1.1)
  // Using default value (3) to prevent premature loss detection
  // When combined with time-based detection, lower values can cause empty vector issues
  // Default (3) is safer and prevents false positives that trigger empty OnPacketsLost calls
  // Config::SetDefault("ns3::QuicSocketBase::kReorderingThreshold", UintegerValue(2));
  
  // Time-based reordering fraction (RFC 9002 Section 6.1.2)
  // Standard RFC 9002 value - keeps time-based detection conservative
  Config::SetDefault("ns3::QuicSocketBase::kTimeReorderingFraction", DoubleValue(9.0/8.0));
  
  // Default initial RTT (RFC 9002 Section 6.2.2)
  // RFC 9002 recommended value for initial RTT estimation
  Config::SetDefault("ns3::QuicSocketBase::kDefaultInitialRtt", TimeValue(MilliSeconds(333)));
  
  // ============================================================================
  // CONGESTION CONTROL PARAMETERS
  // ============================================================================
  
  // QUIC Congestion Control Configuration
  Config::SetDefault("ns3::QuicSocketBase::CcType", IntegerValue(QuicSocketBase::QuicNewReno)); // Use New Reno
  Config::SetDefault("ns3::QuicSocketBase::LegacyCongestionControl", BooleanValue(true)); // Use QUIC-specific congestion control  
  
  // Reduce initial slow start threshold to enter congestion avoidance sooner
  // This prevents aggressive sending that can cause congestion
  // SIGNIFICANT CHANGE: Reduced from unlimited (65535) to 32KB (21 packets) for much more conservative behavior
  // This forces the connection to exit slow start after ~21 packets, preventing congestion buildup
  // Realistic: 32KB is conservative but prevents the exponential growth that causes congestion
  Config::SetDefault("ns3::QuicSocketBase::InitialSlowStartThreshold", UintegerValue(32*1024)); // 32KB - SIGNIFICANTLY REDUCED

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
  Config::SetDefault("ns3::QuicStreamBase::MaxDataInterval", UintegerValue(5)); // Reduced from default 15000
  
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
  
  // Install QUIC stack on remote host (instead of Internet stack)
  QuicHelper quicHelper;

  quicHelper.InstallQuic (remoteHostContainer);
  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
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

  double xMax = 4000.0;
  double yMax = xMax;

  // Altitudes
  double gnbHeight = 2000000.0;
  double iabHeight = 10.0;

  // Offsets as fractions of total area (adjust as needed)
  double xOffset = xMax*0.36;  // ~30% from center to left/right
  double yOffset = yMax*0.40;  // ~30% from center to top/bottom
  //double gnbX = xMax/2.0;
  //double gnbY = yMax/2.0;
  // Center Donor Node
  Vector posWired = Vector(xMax / 2.0, yMax / 2.0, gnbHeight);

  // Symmetric IAB positions
  Vector posIab1 = Vector(xMax / 2.0 , yMax / 2.0, iabHeight);        // Mid
  Vector posIab3 = Vector(xMax / 2.0 - xOffset, yMax / 2.0 + yOffset, iabHeight);        // Top-left
  Vector posIab4 = Vector(xMax / 2.0 - xOffset, yMax / 2.0 - yOffset, iabHeight);        // Bottom-left
  Vector posIab2 = Vector(xMax / 2.0 + xOffset, yMax / 2.0 - yOffset, iabHeight);        // Bottom-right
  Vector posIab5 = Vector(xMax / 2.0 + xOffset, yMax / 2.0, iabHeight);                  // Mid-right
  Vector posIab6 = Vector(xMax / 2.0 - xOffset, yMax / 2.0, iabHeight);                  // Mid-left

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
  // Install Mobility Model
  
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator> ();
  enbPositionAlloc->Add (posWired);
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  enbmobility.SetPositionAllocator(enbPositionAlloc);
  enbmobility.Install (enbNodes);
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

  // // Random user generation code
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
  
  //     uePosAlloc->Add(Vector(x, y, z));
  // }

  // Create one user at fixed position 100 meters away from eNB
  double ueX = xMax/2.0+1000;  // Same X coordinate as eNB
  double ueY = yMax/2.0 + 100.0;  // 100 meters north of eNB
  double ueZ = 1.7;  // Typical UE height
  
  uePosAlloc->Add(Vector(ueX, ueY, ueZ));
  

// Additional user positioning code (no longer needed)
// uint32_t totalUes = ueNodes.GetN();        // e.g., 20
// uint32_t clusterCount = clusterCenters.size(); // 7 clusters
// uint32_t baseUesPerCluster = totalUes / clusterCount;     // 2 UEs per cluster
// uint32_t extraUes = totalUes % clusterCount;              // Remaining UEs to distribute

  // Ptr<UniformRandomVariable> radiusRand = CreateObject<UniformRandomVariable>();
  // radiusRand->SetAttribute("Min", DoubleValue(100));               // minimum radius from center
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
  
  //     uePosAlloc->Add(Vector(x, y, z));
  // }

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
  
  // QUIC Socket buffer configuration - must be large enough for high bitrate segments
  // For 66 Mbps: average segment ~15.74 MB, max segment ~31.47 MB
  // Set to hold at least 2 full segments to prevent blocking
  Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(64*1024*1024));  // 64 MB (2x max segment)
  Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(64*1024*1024));  // 64 MB (2x max segment)

  // QUIC stream buffer configuration - must hold multiple large frames (up to 330 KB each)
  // At 66 Mbps, frames accumulate faster than they can be transmitted
  // Buffer was 98.5% full (7.88 MB used) with only 127 KB available, but frames are 156 KB
  // Set to 32 MB to handle burst accumulation and prevent blocking
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (32*1024*1024));  // 32 MB (96x max frame)
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (32*1024*1024));  // 32 MB (96x max frame)

  // DASH over QUIC configuration - aligned with QUIC packet size limits
  double target_dt = 10.0;  // Target buffering time
  // DASH bufferSpace: should hold multiple segments for smooth playback
  // For 66 Mbps: ~6 segments in 100 MB, increase to 200 MB for 10+ segments
  uint32_t bufferSpace = 400*1024*1024;  // 200 MB (10+ segments at 66 Mbps)
  double window = 5;  // Throughput measurement window in milliseconds
  std::string algorithm = "ns3::FdashClient";  // DASH adaptation algorithm
  
  // Video duration configuration
  double desiredVideoDuration = 60.0;  // Desired video duration in seconds
  
  // Calculate minimum simulation duration
  // Video duration + buffer time for handshake, initial buffering, cleanup, and app stop buffer
  double initialSetupTime = 1.5;  // Buffer time in seconds (handshake + initial buffering + cleanup + 1s app stop buffer)
  double minSimulationDuration = desiredVideoDuration*1.15;
  
  // Get current stopTime (line 1024)
  double stopTime = 1.0;  // Minimal time for testing
  
  // Check if current stopTime is less than minimum, and adjust if needed
  if (stopTime < minSimulationDuration)
  {
      NS_LOG_UNCOND("Adjusting simulation duration: " << stopTime << "s -> " 
                   << minSimulationDuration << "s (required for video duration " 
                   << desiredVideoDuration << "s)");
      stopTime = minSimulationDuration;
  }
  else
  {
      NS_LOG_UNCOND("Simulation duration: " << stopTime << "s (video duration: " 
                   << desiredVideoDuration << "s, minimum required: " 
                   << minSimulationDuration << "s)");
  }

  // Create a DASH server on each UE (listening on port 80)
  DashServerHelper dashServer ("ns3::QuicSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), 80));
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    serverApps.Add (dashServer.Install (ueNodes.Get(u)));
    NS_LOG_UNCOND("DASH Server installed on UE " << u << " (IP=" << ueIpIface.GetAddress(u) << ") port 80");
  }
  
  // Create DASH clients on the remote host, one per UE (connecting to each UE server)
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    DashClientHelper dashClient ("ns3::QuicSocketFactory",
                                  InetSocketAddress(ueIpIface.GetAddress(u), 80),
                                  algorithm);
    dashClient.SetAttribute ("VideoId", UintegerValue(u + 1));
    dashClient.SetAttribute ("TargetDt", TimeValue(Seconds(target_dt)));
    dashClient.SetAttribute ("window", TimeValue(MilliSeconds(window)));
    dashClient.SetAttribute ("bufferSpace", UintegerValue(bufferSpace));
    dashClient.SetAttribute ("MaxVideoDuration", TimeValue(Seconds(desiredVideoDuration)));  // Add this line
    
    clientApps.Add (dashClient.Install (remoteHost));
    NS_LOG_UNCOND("DASH Client " << u << " installed on remoteHost (IP=" << remoteHostAddr 
                  << ") -> server IP=" << ueIpIface.GetAddress(u) << ":80");
  }
  
  // Connect DASH trace sources for clients (now on remote host)
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
    if (dashClient)
    {
      dashClient->TraceConnectWithoutContext("Tx", MakeBoundCallback(&DashClientTxTrace, u));
      dashClient->TraceConnectWithoutContext("Rx", MakeBoundCallback(&DashClientRxTrace, u));
      NS_LOG_UNCOND("Connected DASH Client Tx and Rx traces for Client " << u);
    }
  }
  
  // Connect server Rx traces for all UE servers
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    Ptr<Application> app = serverApps.Get(i);
    Ptr<DashServer> srv = DynamicCast<DashServer>(app);
    if (srv)
    {
      srv->TraceConnectWithoutContext("Rx", MakeCallback(&DashServerRxTrace));
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
  
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.2 + i * 0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    serverApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  // Clients start after servers with additional delay for QUIC handshake
  for (uint32_t i = 0; i < clientApps.GetN(); ++i)
  {
    clientApps.Get(i)->SetStartTime(Seconds(0.3 + i * 0.1));
    // Stop apps 1 second before simulation stops to allow cleanup
    clientApps.Get(i)->SetStopTime(Seconds(stopTime + 2.0 - 1.0));
  }
  
  Simulator::Stop (Seconds (stopTime + 2.0));

  NS_LOG_UNCOND("\n=== Scheduling QUIC Trace Connections ===");
  
  // Connect traces for remote host (now QUIC client) - schedule after app start
  uint32_t clientNodeId = remoteHost->GetId();
  Time clientTraceTimeSched = Seconds(0.417614);  // After clients start at 0.3s
  Simulator::Schedule(clientTraceTimeSched, &Traces, clientNodeId, "./client", ".txt");
  NS_LOG_UNCOND("  Scheduled QUIC traces for Client Node " << clientNodeId << " at t=" << clientTraceTimeSched.GetSeconds() << "s");
  
  // Connect traces for each UE node (now QUIC servers)
  Time serverTraceTimeSched = Seconds(0.45);  // After server apps start at 0.2s
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    uint32_t nodeId = ueNodes.Get(u)->GetId();
    Simulator::Schedule(serverTraceTimeSched + Seconds(u * 0.1), &Traces, nodeId, "./server", ".txt");
    NS_LOG_UNCOND("  Scheduled QUIC traces for UE Node " << nodeId << " (server) at t=" << (serverTraceTimeSched + Seconds(u * 0.1)).GetSeconds() << "s");
  }
  
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
    
  std::string tracePrefix = "ntn_iab_quic_dash";  // Keep variable for log statements
  NS_LOG_UNCOND("\n=== Trace Configuration ===");
  NS_LOG_UNCOND("QUIC traces: Using quic-variants-comparison example approach");
  NS_LOG_UNCOND("DASH application traces: ENABLED");
  NS_LOG_UNCOND("RLC/MAC/PHY layer traces: ENABLED");
  NS_LOG_UNCOND("============================\n");
  
  NS_LOG_UNCOND("\n=== DASH over QUIC Simulation Parameters ===");
  NS_LOG_UNCOND("Number of UEs: " << ueNodes.GetN());
  NS_LOG_UNCOND("Simulation time: " << stopTime << " seconds");
  NS_LOG_UNCOND("DASH algorithm: " << algorithm);
  NS_LOG_UNCOND("Target buffering time: " << target_dt << " milliseconds");
  NS_LOG_UNCOND("Expected video segments: ~" << (int)(stopTime/2));
  NS_LOG_UNCOND("Expected video frames: ~" << (int)(stopTime * 50));
  
  Simulator::Run();
  
  // Print DASH statistics for each UE
  NS_LOG_UNCOND("\n========== DASH over QUIC Results ==========");
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
    if (dashClient)
    {
      NS_LOG_UNCOND("\nUE " << u << " (VideoId=" << (u+1) << "):");
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
    
  return 0;
}