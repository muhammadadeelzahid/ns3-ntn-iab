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
 * n0                     n2
 *   \        TCP        /
 *    n1 -------------- n8
 *   /         P0        \
 * n4                     n5
 *
 * 
 * Authors: Alvise De Biasio <alvise.debiasio@gmail.com>
 *          Federico Chiariotti <whatever@blbl.it>
 *          Michele Polese <michele.polese@gmail.com>
 *          Davide Marcato <davidemarcato@outlook.com>
 *          Shengjie Shu <shengjies@uvic.ca>
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/random-variable-stream.h"
#include <iostream>
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/quic-header.h"
#include "ns3/quic-socket-base.h"
#include <algorithm>
#include <cctype>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("wns3-mpquic-one-path");

// Trace callbacks for CWND, RTT, and Rx data
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
  uint32_t nodeId = qsb->GetNode()->GetId();
  uint32_t packetSize = p->GetSize();
  
  // Log with node and packet information
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << packetSize 
                       << "\tNode:" << nodeId << std::endl;
}

// Helper function to convert string to lowercase for case-insensitive comparison
std::string toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Helper function to get QUIC congestion control type from algorithm name
int getQuicCcType(const std::string& ccName) {
    std::string name = toLowercase(ccName);
    
    if (name == "newreno" || name == "quicnewreno") {
        return QuicSocketBase::QuicNewReno;
    } else if (name == "olia") {
        return QuicSocketBase::OLIA;
    } else {
        NS_LOG_WARN("Unknown QUIC congestion control algorithm: " << ccName << ". Using QuicNewReno as default.");
        return QuicSocketBase::QuicNewReno;
    }
}

// Function to set up traces for a given node
void
Traces(uint32_t nodeId, std::string pathPrefix, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
  NS_LOG_INFO("Matches cw " << Config::LookupMatches(pathCW.str().c_str()).GetN());

  std::ostringstream fileCW;
  fileCW << pathPrefix << "QUIC-cwnd-change" << nodeId << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";

  std::ostringstream fileRTT;
  fileRTT << pathPrefix << "QUIC-rtt" << nodeId << finalPart;

  std::ostringstream fileName;
  fileName << pathPrefix << "QUIC-rx-data" << nodeId << finalPart;
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  NS_LOG_INFO("Matches rx " << Config::LookupMatches(pathRx.str().c_str()).GetN());

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContext (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));
}

void ThroughputMonitor (FlowMonitorHelper *fmhelper, Ptr<FlowMonitor> flowMon, Ptr<OutputStreamWrapper> stream)
{
    std::map<FlowId, FlowMonitor::FlowStats> flowStats = flowMon->GetFlowStats();
    Ptr<Ipv4FlowClassifier> classing = DynamicCast<Ipv4FlowClassifier> (fmhelper->GetClassifier());
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator stats = flowStats.begin (); stats != flowStats.end (); ++stats)
    {
        if (stats->first == 1 || stats->first == 2){
            *stream->GetStream () << stats->first  << "\t" << Simulator::Now().GetSeconds()/*->second.timeLastRxPacket.GetSeconds()*/ << "\t" << stats->second.rxBytes << "\t" << stats->second.rxPackets << "\t" << stats->second.lastDelay.GetMilliSeconds() << "\t" << stats->second.rxBytes*8/1024/1024/(stats->second.timeLastRxPacket.GetSeconds()-stats->second.timeFirstRxPacket.GetSeconds())  << std::endl;
        }
    }
    Simulator::Schedule(Seconds(0.05),&ThroughputMonitor, fmhelper, flowMon, stream);
}

void
ModifyLinkRate(NetDeviceContainer *ptp, DataRate lr, Time delay) {
    StaticCast<PointToPointNetDevice>(ptp->Get(0))->SetDataRate(lr);
    StaticCast<PointToPointChannel>(StaticCast<PointToPointNetDevice>(ptp->Get(0))->GetChannel())->SetAttribute("Delay", TimeValue(delay));
}

int
main (int argc, char *argv[])
{
    int schedulerType = MpQuicScheduler::PEEKABOO;
    
    string myRandomNo = "52428800";
    string lossrate = "0.0000";

    double rate0a = 120.0;
    double rate1a = 150.0;
    double delay0a = 2.0;
    double delay1a = 1.0;
    double rate0b = 150.0;
    double rate1b = 150.0;
    double delay0b = 5.0;
    double delay1b = 2.0;    

    int bVar = 2;
    int bLambda = 100;
    int mrate = 52428800;
    std::string ccAlgorithm = "newreno";
    int mselect = 3;
    int seed = 1;
    int ccType = QuicSocketBase::QuicNewReno;  // Will be set from ccAlgorithm
    TypeId ccTypeId = QuicCongestionOps::GetTypeId ();  // Will be set based on ccType
    double throughputMax = 0.0;  // Maximum achievable throughput (Mbps)
    CommandLine cmd;


    cmd.AddValue ("SchedulerType", "in use scheduler type (0 - ROUND_ROBIN, 1 - MIN_RTT, 2 - BLEST, 3 - ECF, 4 - Peekaboo)", schedulerType);
    cmd.AddValue ("BVar", "e.g. 100", bVar);
    cmd.AddValue ("BLambda", "e.g. 100", bLambda);
    cmd.AddValue ("MabRate", "e.g. 100", mrate);
    cmd.AddValue ("Rate0a", "e.g. 5Mbps", rate0a);
    cmd.AddValue ("Rate1a", "e.g. 50Mbps", rate1a);
    cmd.AddValue ("Delay0a", "e.g. 80ms", delay0a);
    cmd.AddValue ("Delay1a", "e.g. 20ms", delay1a);
    cmd.AddValue ("Rate0b", "e.g. 5Mbps", rate0b);
    cmd.AddValue ("Rate1b", "e.g. 50Mbps", rate1b);
    cmd.AddValue ("Delay0b", "e.g. 80ms", delay0b);
    cmd.AddValue ("Delay1b", "e.g. 20ms", delay1b);
    cmd.AddValue ("Size", "e.g. 80", myRandomNo);
    cmd.AddValue ("Seed", "e.g. 80", seed);
    cmd.AddValue ("LossRate", "e.g. 0.0001", lossrate);
    cmd.AddValue ("Select", "e.g. 0.0001", mselect);
    cmd.AddValue ("CcAlgorithm", "Congestion control algorithm (newreno, olia) - case insensitive", ccAlgorithm);
    cmd.AddValue ("Throughput", "Maximum achievable throughput in Mbps (e.g. 100)", throughputMax);
    cmd.Parse (argc, argv);
    
    // If throughputMax is specified, use it to set rate0a and rate0b
    if (throughputMax > 0.0) {
        rate0a = throughputMax;  // Set minimum to the specified throughput
        rate0b = throughputMax;  // Set maximum to the specified throughput
        NS_LOG_INFO("Throughput set to " << throughputMax << " Mbps. Rate range: " << rate0a << "-" << rate0b << " Mbps");
    }

    // Convert algorithm name to ccType after parsing command line
    ccType = getQuicCcType(ccAlgorithm);

    NS_LOG_INFO("\n\n#################### SIMULATION SET-UP ####################\n\n\n");
    
    LogLevel log_precision = LOG_LEVEL_LOGIC;
    Time::SetResolution (Time::NS);
    LogComponentEnableAll (LOG_PREFIX_TIME);
    LogComponentEnableAll (LOG_PREFIX_FUNC);
    LogComponentEnableAll (LOG_PREFIX_NODE);
    LogComponentEnable ("wns3-mpquic-one-path", log_precision);

    RngSeedManager::SetSeed (seed);  

    // Set ccTypeId based on ccType
    if (ccType == QuicSocketBase::OLIA){
        ccTypeId = MpQuicCongestionOps::GetTypeId ();
        NS_LOG_INFO("Using QUIC congestion control algorithm: OLIA");
    }
    else if(ccType == QuicSocketBase::QuicNewReno){
        ccTypeId = QuicCongestionOps::GetTypeId ();
        NS_LOG_INFO("Using QUIC congestion control algorithm: QuicNewReno");
    }
    else {
        // Default fallback
        ccTypeId = QuicCongestionOps::GetTypeId ();
        NS_LOG_WARN("Unknown ccType, defaulting to QuicNewReno");
    }

    // Increased buffer sizes to support higher throughput (100Mbps links)
    Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize",UintegerValue (100000000));  // 100MB
    Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize",UintegerValue (100000000));  // 100MB
    Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize",UintegerValue (100000000));  // 100MB
    Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize",UintegerValue (100000000));  // 100MB

    // QUIC optimizations for high-speed networks (matching TCP optimizations for fair comparison)
    Config::SetDefault ("ns3::QuicSocketBase::MaxData", UintegerValue (UINT32_MAX));
    Config::SetDefault ("ns3::QuicSocketBase::MaxStreamData", UintegerValue (UINT32_MAX));
    Config::SetDefault ("ns3::QuicSocketBase::MaxPacketSize", UintegerValue (1448));  // Match TCP segment size (Ethernet MTU - headers)
    Config::SetDefault ("ns3::QuicSocketBase::InitialPacketSize", UintegerValue (1448));  // Larger initial packet size
    Config::SetDefault ("ns3::QuicSocketBase::InitialSlowStartThreshold", UintegerValue (UINT32_MAX));  // High initial threshold for high BDP

    Config::SetDefault ("ns3::QuicSocketBase::EnableMultipath",BooleanValue(false));
    Config::SetDefault ("ns3::QuicSocketBase::CcType",IntegerValue(ccType));
    // Config::SetDefault ("ns3::QuicL4Protocol::SocketType",TypeIdValue (ccTypeId));
    // Config::SetDefault ("ns3::MpQuicScheduler::SchedulerType", IntegerValue(schedulerType));   
    // Config::SetDefault ("ns3::MpQuicScheduler::BlestVar", UintegerValue(bVar));   
    // Config::SetDefault ("ns3::MpQuicScheduler::BlestLambda", UintegerValue(bLambda));     
    // Config::SetDefault ("ns3::MpQuicScheduler::MabRate", UintegerValue(mrate)); 
    // Config::SetDefault ("ns3::MpQuicScheduler::Select", UintegerValue(mselect)); 

    
    Ptr<RateErrorModel> em = CreateObjectWithAttributes<RateErrorModel> (
    "RanVar", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1.0]"),
    "ErrorRate", DoubleValue (stod(lossrate)));

    Ptr<UniformRandomVariable> rateVal0 = CreateObject<UniformRandomVariable> ();
    rateVal0->SetAttribute ("Min", DoubleValue (rate0a));
    rateVal0->SetAttribute ("Max", DoubleValue (rate0b));


    Ptr<UniformRandomVariable> delayVal0 = CreateObject<UniformRandomVariable> ();
    delayVal0->SetAttribute ("Min", DoubleValue (delay0a));
    delayVal0->SetAttribute ("Max", DoubleValue (delay0b));



    int simulationEndTime = 30;
    int start_time = 1;

    uint32_t maxBytes = UINT32_MAX;
    
    NS_LOG_INFO ("Create nodes.");
    NodeContainer c;
    c.Create (10);
    NodeContainer n0n1 = NodeContainer (c.Get (0), c.Get (1));
    NodeContainer n1n8 = NodeContainer (c.Get (1), c.Get (8));
    NodeContainer n8n2 = NodeContainer (c.Get (8), c.Get (2));

    NodeContainer n4n1 = NodeContainer (c.Get (4), c.Get (1));
    NodeContainer n8n5 = NodeContainer (c.Get (8), c.Get (5));
 
    InternetStackHelper internet;
    internet.Install (c.Get (0));
    internet.Install (c.Get (1));
    internet.Install (c.Get (2));
    internet.Install (c.Get (8));

    QuicHelper stack;
    stack.InstallQuic (c.Get (4));
    stack.InstallQuic (c.Get (5));


    // We create the channels first without any IP addressing information
    NS_LOG_INFO ("Create channels.");
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue (std::to_string(rateVal0->GetValue())+"Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue (std::to_string(delayVal0->GetValue())+"ms"));
    NetDeviceContainer d1d8 = p2p.Install (n1n8);
    d1d8.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

    p2p.SetDeviceAttribute ("DataRate", StringValue ("1000Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue ("0ms"));
    NetDeviceContainer d4d1 = p2p.Install (n4n1);
    NetDeviceContainer d0d1 = p2p.Install (n0n1);
    NetDeviceContainer d8d5 = p2p.Install (n8n5);
    NetDeviceContainer d8d2 = p2p.Install (n8n2);
    
    // Later, we add IP addresses.
    NS_LOG_INFO ("Assign IP Addresses.");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i4i1 = ipv4.Assign (d4d1);

    ipv4.SetBase ("10.1.9.0", "255.255.255.0");
    Ipv4InterfaceContainer i1i8 = ipv4.Assign (d1d8);

    ipv4.SetBase ("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer i8i5 = ipv4.Assign (d8d5);
    
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i0i1 = ipv4.Assign (d0d1);

    ipv4.SetBase ("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i1i2 = ipv4.Assign (d8d2);



    Ptr<Ipv4> ipv4_n4 = c.Get(4)->GetObject<Ipv4> ();
    Ipv4StaticRoutingHelper ipv4RoutingHelper; 
    Ptr<Ipv4StaticRouting> staticRouting_n4 = ipv4RoutingHelper.GetStaticRouting (ipv4_n4); 
    staticRouting_n4->AddHostRouteTo (Ipv4Address ("10.1.5.2"), Ipv4Address ("10.1.9.2") ,1); 

    Ptr<Ipv4> ipv4_n5 = c.Get(5)->GetObject<Ipv4> ();
    Ptr<Ipv4StaticRouting> staticRouting_n5 = ipv4RoutingHelper.GetStaticRouting (ipv4_n5); 
    staticRouting_n5->AddHostRouteTo (Ipv4Address ("10.1.4.1"), Ipv4Address ("10.1.9.1") ,1); 
   
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    
    uint16_t port2 = 9;  // well-known echo port number
    
    MpquicBulkSendHelper source ("ns3::QuicSocketFactory",
                            InetSocketAddress (i8i5.GetAddress (1), port2));
    // Set the amount of data to send in bytes.  Zero is unlimited.
    source.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
    ApplicationContainer sourceApps = source.Install (c.Get (4));
    sourceApps.Start (Seconds (start_time));
    sourceApps.Stop (Seconds(simulationEndTime));

    PacketSinkHelper sink2 ("ns3::QuicSocketFactory",
                            InetSocketAddress (Ipv4Address::GetAny (), port2));
    ApplicationContainer sinkApps2 = sink2.Install (c.Get (5));
    sinkApps2.Start (Seconds (0.0));
    sinkApps2.Stop (Seconds(simulationEndTime));


    std::ostringstream file;
    file<<"./scheduler" << schedulerType;

    AsciiTraceHelper asciiTraceHelper;
    std::ostringstream fileName;
    fileName <<  "./scheduler" << schedulerType << "-rx" << ".txt";
    Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ());
  

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
    ThroughputMonitor(&flowmon, monitor, stream); 
    
    // Schedule trace connections for QUIC sender (node 4) and receiver (node 5)
    // Trace connections should be scheduled after applications are installed
    Time traceStartTime = Seconds(start_time + 0.1);
    Simulator::Schedule (traceStartTime, &Traces, c.Get(4)->GetId(), "./sender", ".txt");
    Simulator::Schedule (traceStartTime, &Traces, c.Get(5)->GetId(), "./receiver", ".txt");

    for (double i = 1; i < simulationEndTime; i = i+0.1){
        Simulator::Schedule (Seconds (i), &ModifyLinkRate, &d1d8  , DataRate(std::to_string(rateVal0->GetValue())+"Mbps"),  Time::FromInteger(delayVal0->GetValue(), Time::MS));
    }


    Simulator::Stop (Seconds(simulationEndTime));
    NS_LOG_INFO("\n\n#################### STARTING RUN ####################\n\n");
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
        if (i->first == 1 || i->first == 2){

        NS_LOG_INFO("Flow " << i->first  << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")"
        << "\n Last rx Seconds: " << i->second.timeLastRxPacket.GetSeconds()
        << "\n Rx Bytes: " << i->second.rxBytes
        << "\n DelaySum(s): " << i->second.delaySum.GetSeconds()
        << "\n rxPackets: " << i->second.rxPackets);
        }
        
    }

    NS_LOG_INFO("\nfile size: "<<maxBytes<< "Bytes, scheduler type " <<schedulerType<<
                "\npath 0: rate "<< rate0a <<", delay "<< delay0a << 
                "\npath 1: rate " << rate1a << ", delay " << delay1a );

    Simulator::Destroy ();

    return 0;
}
