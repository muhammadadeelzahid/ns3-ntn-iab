
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
#include <ns3/buildings-module.h>
#include "ns3/log.h"
#include "ns3/mmwave-helper.h"
#include "ns3/lte-module.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
//#include "ns3/gtk-config-store.h"
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MmWaveNtnIab");

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
  // // LogComponentEnable("EpcSgwPgwApplication", LOG_LEVEL_LOGIC);
  // // LogComponentEnable("EpcMmeApplication", LOG_LEVEL_LOGIC);
  // // LogComponentEnable("EpcUeNas", LOG_LEVEL_LOGIC);
  // LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
  // LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
  LogComponentEnable("MmWaveHelper", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWavePaddedHbfMacScheduler", LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveSpectrumPhy", ns3::LOG_LEVEL_ALL);
  // LogComponentEnable("MmWaveEnbPhy", ns3::LOG_LEVEL_INFO);
  // LogComponentEnable("MmWaveUePhy", ns3::LOG_LEVEL_INFO);
  // LogComponentEnable("MmWavePointToPointEpcHelper", LOG_LEVEL_LOGIC);
  // //LogComponentEnable("EpcS1ap", LOG_LEVEL_LOGIC);
  // // LogComponentEnable("EpcTftClassifier", LOG_LEVEL_LOGIC);
  // // LogComponentEnable("EpcGtpuHeader", LOG_LEVEL_INFO);
  // // LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
  // // LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("UdpClient", LOG_ALL);
  // LogComponentEnable("UdpServer", LOG_ALL);
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

  CommandLine cmd; 
  unsigned run = 0;
  bool rlcAm = false;
  uint32_t numRelays = 0;
  uint32_t rlcBufSize = 10;
  uint32_t interPacketInterval = 5;
  uint32_t throughput = 200;
  uint32_t packetSize = 1400; //bytes
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
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

// 	Config::SetDefault ("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue (2));
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
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);
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
  // Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);
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
  NodeContainer ueNodes;
  NodeContainer enbNodes;
  NodeContainer iabNodes;
 
  enbNodes.Create(1);
  iabNodes.Create(numRelays);
  ueNodes.Create(20);
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
  // Install the IP stack on the UEs
  internet.Install (ueNodes);
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
  uint16_t dlPort = 1234;
  // LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
  // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
  // uint16_t ulPort = 2000;
  // uint16_t otherPort = 3000;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
  {
    // DL UDP
    UdpServerHelper dlPacketSinkHelper (dlPort);
    serverApps.Add (dlPacketSinkHelper.Install (ueNodes.Get(u)));
    UdpClientHelper dlClient (ueIpIface.GetAddress (u), dlPort);
    dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds(interPacketInterval)));
    dlClient.SetAttribute ("PacketSize", UintegerValue(packetSize));
    dlClient.SetAttribute ("MaxPackets", UintegerValue(500));
    clientApps.Add (dlClient.Install (remoteHost));
    dlPort++;
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
  mmwaveHelper->EnableTraces ();
  serverApps.Start (Seconds (0.3));
  clientApps.Start (Seconds (0.3));
  clientApps.Stop (Seconds (2.0));
  serverApps.Stop (Seconds (2.0));
  Simulator::Stop (Seconds (2.5));
  
  Simulator::Run();
  /*GtkConfigStore config;
  config.ConfigureAttributes();*/
  Simulator::Destroy();
  return 0;
}