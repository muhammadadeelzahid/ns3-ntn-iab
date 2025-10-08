
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
 *                 Integrating NTNs & Multilayer support with IAB with MPEG-DASH video streaming with quic derived from signetlabdei/ns3-mmwave-iab, Mattia Sandri/ns3-ntn, signetlabdei/ns3-mmwave-hbf, signetlabdei/quic-ns-3 and djvergadd/dash
 *                  
 */
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

// DASH Client Socket Rx Trace (when client receives video data on socket)
void DashClientSocketRxTrace(uint32_t ueId, Ptr<const Packet> packet, const Address& from)
{
  if (g_dashClientRxFiles.find(ueId) == g_dashClientRxFiles.end())
  {
    std::string filename = "DashClientRx_UE_" + std::to_string(ueId) + ".txt";
    g_dashClientRxFiles[ueId] = new std::ofstream(filename.c_str());
    *g_dashClientRxFiles[ueId] << "# DASH Client " << ueId << " - Video Segments Received (Similar to QuicServerRx)" << std::endl;
    *g_dashClientRxFiles[ueId] << "# Time(s)\tPacketSize(bytes)\tTotalPackets\tTotalBytes\tFromIP\tFromPort" << std::endl;
    g_dashClientRxPackets[ueId] = 0;
    g_dashClientRxBytes[ueId] = 0;
  }
  
  g_dashClientRxPackets[ueId]++;
  g_dashClientRxBytes[ueId] += packet->GetSize();
  
  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  *g_dashClientRxFiles[ueId] << Simulator::Now().GetSeconds() << "\t"
                             << packet->GetSize() << "\t"
                             << g_dashClientRxPackets[ueId] << "\t"
                             << g_dashClientRxBytes[ueId] << "\t"
                             << addr.GetIpv4() << "\t"
                             << addr.GetPort() << std::endl;
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
  std::cout << "QUIC SOCKET TX CALLBACK TRIGGERED! Packet size: " << packet->GetSize() 
            << " bytes, packet_number: " << header.GetPacketNumber() << std::endl;
  if (!quicTxFile.is_open())
  {
    quicTxFile.open("quic_socket_tx.txt", std::ios::out);
    std::cout << "QUIC SOCKET TX file opened" << std::endl;
  }
  DumpPacketHex(quicTxFile, packet, "QUIC_SOCKET_TX PacketNumber=" + std::to_string(header.GetPacketNumber().GetValue()));
  quicTxFile.flush();
}

// QUIC Socket Base Rx callback
void QuicSocketRxCallback(Ptr<const Packet> packet, const QuicHeader& header, Ptr<const QuicSocketBase> socket)
{
  std::cout << "QUIC SOCKET RX CALLBACK TRIGGERED! Packet size: " << packet->GetSize() 
            << " bytes, packet_number: " << header.GetPacketNumber() << std::endl;
  if (!quicRxFile.is_open())
  {
    quicRxFile.open("quic_socket_rx.txt", std::ios::out);
    std::cout << "QUIC SOCKET RX file opened" << std::endl;
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
  pathCW << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
  NS_LOG_INFO("Matches cw " << Config::LookupMatches(pathCW.str().c_str()).GetN());

  std::ostringstream fileCW;
  fileCW << pathVersion << "QUIC-cwnd-change"  << serverId << "" << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";

  std::ostringstream fileRTT;
  fileRTT << pathVersion << "QUIC-rtt"  << serverId << "" << finalPart;

  std::ostringstream pathRCWnd;
  pathRCWnd<< "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RWND";

  std::ostringstream fileRCWnd;
  fileRCWnd<<pathVersion << "QUIC-rwnd-change"  << serverId << "" << finalPart;

  std::ostringstream fileName;
  fileName << pathVersion << "QUIC-rx-data" << serverId << "" << finalPart;
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  NS_LOG_INFO("Matches rx " << Config::LookupMatches(pathRx.str().c_str()).GetN());

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));
}

void UdpL4TxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  std::cout << "UDP L4 TX CALLBACK TRIGGERED! Packet size: " << packet->GetSize() << " bytes" << std::endl;
  if (!udpL4TxFile.is_open())
  {
    udpL4TxFile.open("udp_l4_tx.txt", std::ios::out);
    std::cout << "UDP L4 TX file opened" << std::endl;
  }
  DumpPacketHex(udpL4TxFile, packet, "UDP_L4_TX Interface=" + std::to_string(interface));
  udpL4TxFile.flush();
}

void UdpL4RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  std::cout << "UDP L4 RX CALLBACK TRIGGERED! Packet size: " << packet->GetSize() << " bytes" << std::endl;
  if (!udpL4RxFile.is_open())
  {
    udpL4RxFile.open("udp_l4_rx.txt", std::ios::out);
    std::cout << "UDP L4 RX file opened" << std::endl;
  }
  DumpPacketHex(udpL4RxFile, packet, "UDP_L4_RX Interface=" + std::to_string(interface));
  udpL4RxFile.flush();
}

// IPv4 L3 layer callbacks
void Ipv4L3TxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  if (!ipv4L3TxFile.is_open())
  {
    ipv4L3TxFile.open("ipv4_l3_tx.txt", std::ios::out);
  }
  DumpPacketHex(ipv4L3TxFile, packet, "IPV4_L3_TX Interface=" + std::to_string(interface));
  ipv4L3TxFile.flush();
}

void Ipv4L3RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
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
  if (!p2pTxFile.is_open())
  {
    p2pTxFile.open("p2p_tx.txt", std::ios::out);
  }
  DumpPacketHex(p2pTxFile, packet, "P2P_TX");
  p2pTxFile.flush();
}

void P2PRxCallback(Ptr<const Packet> packet)
{
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
  std::cout << "Packet dropped at " << Simulator::Now().GetSeconds() << "s" << std::endl;
}

int
main (int argc, char *argv[])
{
  // Enable DASH logging for debugging
  LogComponentEnable("DashClient", LOG_LEVEL_INFO);
  LogComponentEnable("DashServer", LOG_LEVEL_INFO);
  LogComponentEnable("HttpParser", LOG_LEVEL_INFO);
  LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);
  
  // Enable QUIC logging (safe - no wildcard traces that cause crash)
  // LogComponentEnable("QuicSocketBase", LOG_LEVEL_INFO);
  // LogComponentEnable("QuicL4Protocol", LOG_LEVEL_INFO);
  // LogComponentEnable("QuicStreamBase", LOG_LEVEL_INFO);
  
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
  // LogComponentEnable("QuicStream", (LogLevel)(LOG_PREFIX_TIME | LOG_PREFIX_FUNC | LOG_LEVEL_ALL));
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
  uint32_t numRelays = 0;
  uint32_t numUes = 1;  // Number of UE nodes/users
  uint32_t rlcBufSize = 10;
  uint32_t interPacketInterval = 5;
  uint32_t throughput = 200;
  uint32_t packetSize = 1400; //bytes
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("numUes", "Number of UE nodes/users", numUes);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("throughput", "throughput [mbps]", throughput);
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
  // Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
  // Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (rlcBufSize * 1024 * 1024));
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
//   Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue (10 * 1024 * 1024));
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
  Config::SetDefault("ns3::QuicClient::PacketSize", UintegerValue(packetSize));
  Config::SetDefault("ns3::QuicClient::MaxPackets", UintegerValue(500));
  Config::SetDefault("ns3::QuicClient::Interval", TimeValue(MicroSeconds(interPacketInterval)));
  
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
  NS_LOG_UNCOND("Throughput: "<<throughput<<" Inter-packet interval: "<<interPacketInterval);
 
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  // Create a single RemoteHost
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  
  // Install QUIC stack on remote host (instead of Internet stack)
  QuicHelper quicHelper;

  Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (1 << 25));  // 32 MB
  Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (1 << 25));  // 32 MB
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (1 << 25));  // 32 MB
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (1 << 25));  // 32 MB
 
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
  // eNB is at center (xMax/2, yMax/2, gnbHeight)
  // Place UE 100 meters north of eNB
  // double ueX = xMax/2.0+10000;  // Same X coordinate as eNB
  // double ueY = yMax/2.0 + 100.0;  // 100 meters north of eNB
  // double ueZ = 1.7;  // Typical UE height
  
  // uePosAlloc->Add(Vector(ueX, ueY, ueZ));
  

// Additional user positioning code (no longer needed)
// uint32_t totalUes = ueNodes.GetN();        // e.g., 20
// uint32_t clusterCount = clusterCenters.size(); // 7 clusters
// uint32_t baseUesPerCluster = totalUes / clusterCount;     // 2 UEs per cluster
// uint32_t extraUes = totalUes % clusterCount;              // Remaining UEs to distribute

  Ptr<UniformRandomVariable> radiusRand = CreateObject<UniformRandomVariable>();
  radiusRand->SetAttribute("Min", DoubleValue(100));               // minimum radius from center
  radiusRand->SetAttribute("Max", DoubleValue(std::min(xMax, yMax) / 2.0)); // max radius: half of area
  
  Ptr<UniformRandomVariable> angleRand = CreateObject<UniformRandomVariable>();
  angleRand->SetAttribute("Min", DoubleValue(0));
  angleRand->SetAttribute("Max", DoubleValue(2 * M_PI));
  
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
      double radius = radiusRand->GetValue();
      double angle = angleRand->GetValue();
  
      double x = xMax/2 + radius * std::cos(angle);
      double y = yMax/2 + radius * std::sin(angle);
      double z = 1.7; // typical UE height
  
      // Ensure within boundaries
      x = std::min(std::max(x, 0.0), xMax);
      y = std::min(std::max(y, 0.0), yMax);
  
      uePosAlloc->Add(Vector(x, y, z));
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
  
  // DASH over QUIC configuration
  double target_dt = 35.0;  // Target buffering time
  uint32_t bufferSpace = 30000000;  // 30 MB buffer
  std::string window = "10s";  // Throughput measurement window
  std::string algorithm = "ns3::FdashClient";  // DASH adaptation algorithm
  
  // Create ONE DASH server on remote host to serve all UEs (on port 80)
  DashServerHelper dashServer ("ns3::QuicSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), 80));
  serverApps.Add (dashServer.Install (remoteHost));
  NS_LOG_UNCOND("DASH Server installed on remoteHost (IP=" << remoteHostAddr << ") port 80");
  
  // Create DASH clients on each UE
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    // Client (on UE) - requests and receives video segments
    DashClientHelper dashClient ("ns3::QuicSocketFactory",
                                  InetSocketAddress(remoteHostAddr, 80),
                                  algorithm);
    dashClient.SetAttribute ("VideoId", UintegerValue(u + 1));  // Each UE gets different video ID
    dashClient.SetAttribute ("TargetDt", TimeValue(Seconds(target_dt)));
    dashClient.SetAttribute ("window", TimeValue(Time(window)));
    dashClient.SetAttribute ("bufferSpace", UintegerValue(bufferSpace));
    
    clientApps.Add (dashClient.Install (ueNodes.Get(u)));
    
    NS_LOG_UNCOND("DASH Client " << u << " installed on UE (IP=" << ueIpIface.GetAddress(u) 
                  << ") -> server IP=" << remoteHostAddr << ":80");
  }
  
   // Connect DASH trace sources
   for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
   {
     Ptr<DashClient> dashClient = DynamicCast<DashClient>(clientApps.Get(u));
     if (dashClient)
     {
       // Connect Tx trace (segment requests sent by client)
       dashClient->TraceConnectWithoutContext("Tx", MakeBoundCallback(&DashClientTxTrace, u));
       
       // Connect Rx trace (video segments received by client)
       dashClient->TraceConnectWithoutContext("Rx", MakeBoundCallback(&DashClientSocketRxTrace, u));
       
       NS_LOG_UNCOND("Connected DASH Client Tx and Rx traces for Client " << u);
     }
   }
  
  // Connect server Rx trace - get the actual DashServer application
  if (serverApps.GetN() > 0)
  {
    Ptr<Application> app = serverApps.Get(0);
    Ptr<DashServer> dashServer = DynamicCast<DashServer>(app);
    if (dashServer)
    {
      dashServer->TraceConnectWithoutContext("Rx", MakeCallback(&DashServerRxTrace));
      NS_LOG_UNCOND("Connected DASH Server Rx trace");
    }
    else
    {
      NS_LOG_WARN("Could not cast application to DashServer");
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
  
  // DASH timing: servers start first with staggered delays, clients need delay for QUIC handshake
  // Stagger server starts to avoid QUIC socket conflicts
  for (uint32_t i = 0; i < serverApps.GetN(); ++i)
  {
    serverApps.Get(i)->SetStartTime(Seconds(0.0 + i * 0.1));
  }
  
  // Clients start after servers with additional delay for QUIC handshake
  for (uint32_t i = 0; i < clientApps.GetN(); ++i)
  {
    clientApps.Get(i)->SetStartTime(Seconds(1.0 + i * 0.1));
  }
  
  double stopTime = 3.0;  // Minimal time for testing
  clientApps.Stop (Seconds (stopTime));
  serverApps.Stop (Seconds (stopTime + 1.0));
  Simulator::Stop (Seconds (stopTime + 2.0));

  NS_LOG_UNCOND("\n=== Scheduling QUIC Trace Connections ===");
  Time traceTime = Seconds(0.1);
  
  // Connect traces for each UE node (QUIC client)
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    uint32_t nodeId = ueNodes.Get(u)->GetId();
    Simulator::Schedule(traceTime, &Traces, nodeId, "./client", ".txt");
    NS_LOG_UNCOND("  Scheduled QUIC traces for UE Node " << nodeId << " (client)");
  }
  
  // Connect traces for remote host (QUIC server)
  uint32_t serverNodeId = remoteHost->GetId();
  Simulator::Schedule(traceTime, &Traces, serverNodeId, "./server", ".txt");
  NS_LOG_UNCOND("  Scheduled QUIC traces for Server Node " << serverNodeId);
  NS_LOG_UNCOND("  Traces will connect at t=" << traceTime.GetSeconds() << "s");
  NS_LOG_UNCOND("========================================\n");
  
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
  NS_LOG_UNCOND("Target buffering time: " << target_dt << " seconds");
  NS_LOG_UNCOND("Expected video segments: ~" << (int)(stopTime/2) << " (2s per segment)");
  NS_LOG_UNCOND("Expected video frames: ~" << (int)(stopTime * 50) << " (50 fps)");
  
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