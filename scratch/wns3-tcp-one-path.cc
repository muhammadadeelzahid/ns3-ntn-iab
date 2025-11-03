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
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/random-variable-stream.h"
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <cctype>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("wns3-tcp-one-path");

// Trace callbacks for TCP CWND, RTT, and Rx data
static void
TcpCwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
TcpRttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldRtt.GetSeconds () << "\t" << newRtt.GetSeconds () << std::endl;
}

static void
PacketSinkRx (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet, const Address &from)
{
  uint32_t packetSize = packet->GetSize();
  
  // Log packet reception from PacketSink
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << packetSize 
                       << "\tTCP" << std::endl;
}

// Function to set up TCP traces for a given node
void
Traces(uint32_t nodeId, std::string pathPrefix, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  // TCP Congestion Window trace
  std::ostringstream pathCW;
  pathCW << "/NodeList/" << nodeId << "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow";
  NS_LOG_INFO("Matches TCP cw " << Config::LookupMatches(pathCW.str().c_str()).GetN());

  std::ostringstream fileCW;
  fileCW << pathPrefix << "TCP-cwnd-change" << nodeId << finalPart;

  // TCP RTT trace
  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << nodeId << "/$ns3::TcpL4Protocol/SocketList/0/RTT";

  std::ostringstream fileRTT;
  fileRTT << pathPrefix << "TCP-rtt" << nodeId << finalPart;

  // PacketSink Rx data trace (for received data)
  std::ostringstream pathPacketSinkRx;
  pathPacketSinkRx << "/NodeList/" << nodeId << "/ApplicationList/*/$ns3::PacketSink/Rx";
  uint32_t packetSinkMatches = Config::LookupMatches(pathPacketSinkRx.str().c_str()).GetN();
  NS_LOG_INFO("Matches PacketSink rx " << packetSinkMatches);

  std::ostringstream filePacketSinkRx;
  filePacketSinkRx << pathPrefix << "TCP-rx-data" << nodeId << finalPart;

  // Connect traces
  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathCW.str ().c_str (), MakeBoundCallback(&TcpCwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRTT.str ().c_str (), MakeBoundCallback(&TcpRttChange, stream2));

  // Connect PacketSink traces for rx data
  Ptr<OutputStreamWrapper> stream3 = asciiTraceHelper.CreateFileStream (filePacketSinkRx.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathPacketSinkRx.str ().c_str (), MakeBoundCallback (&PacketSinkRx, stream3));
}


void
ModifyLinkRate(NetDeviceContainer *ptp, DataRate lr, Time delay) {
    StaticCast<PointToPointNetDevice>(ptp->Get(0))->SetDataRate(lr);
    StaticCast<PointToPointChannel>(StaticCast<PointToPointNetDevice>(ptp->Get(0))->GetChannel())->SetAttribute("Delay", TimeValue(delay));
}

// Helper function to convert string to lowercase for case-insensitive comparison
std::string toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Helper function to get TCP congestion control algorithm string from name
std::string getTcpCcAlgorithm(const std::string& ccName) {
    std::string name = toLowercase(ccName);
    
    if (name == "newreno" || name == "tcpnewreno") {
        return "ns3::TcpNewReno";
    } else if (name == "cubic" || name == "tcpcubic") {
        return "ns3::TcpCubic";
    } else if (name == "highspeed" || name == "tcphighspeed") {
        return "ns3::TcpHighSpeed";
    } else if (name == "westwood" || name == "tcpwestwood") {
        return "ns3::TcpWestwood";
    } else if (name == "hybla" || name == "tcphybla") {
        return "ns3::TcpHybla";
    } else if (name == "vegas" || name == "tcpvegas") {
        return "ns3::TcpVegas";
    } else if (name == "scalable" || name == "tcpscalable") {
        return "ns3::TcpScalable";
    } else if (name == "veno" || name == "tcpveno") {
        return "ns3::TcpVeno";
    } else if (name == "bic" || name == "tcpbic") {
        return "ns3::TcpBic";
    } else if (name == "yeah" || name == "tcpyeah") {
        return "ns3::TcpYeah";
    } else if (name == "illinois" || name == "tcpillinois") {
        return "ns3::TcpIllinois";
    } else if (name == "htcp" || name == "tcphtcp") {
        return "ns3::TcpHtcp";
    } else if (name == "ledbat" || name == "tcpledbat") {
        return "ns3::TcpLedbat";
    } else if (name == "lpd" || name == "tcplpd" || name == "tcplp") {
        return "ns3::TcpLp";
    } else if (name == "dctcp" || name == "tcpdctcp") {
        return "ns3::TcpDctcp";
    } else {
        NS_LOG_WARN("Unknown TCP congestion control algorithm: " << ccName << ". Using TcpNewReno as default.");
        return "ns3::TcpNewReno";
    }
}

int
main (int argc, char *argv[])
{
    std::string myRandomNo = "52428800";  // Increased from 5MB to 50MB to allow more data transfer
    std::string lossrate = "0.0000";

    double rate0a = 120.0;  // Minimum 120 Mbps to guarantee 100+ Mbps throughput
    double rate1a = 150.0;  // Maximum 150 Mbps (not currently used in bottleneck)
    double delay0a = 2.0;   // Minimal delay for better performance
    double delay1a = 1.0;   // Minimal delay
    double rate0b = 150.0;  // Maximum 150 Mbps (bottleneck link maximum, varies 120-150)
    double rate1b = 150.0;  // Not used
    double delay0b = 5.0;   // Maximum delay (bottleneck varies 2-5 ms)
    double delay1b = 2.0;   // Not used    

    int seed = 1;
    std::string ccAlgorithm = "newreno";  // Default to NewReno
    double throughputMax = 0.0;  // Maximum achievable throughput (Mbps)
    CommandLine cmd;

    cmd.AddValue ("Rate0a", "e.g. 5Mbps", rate0a);
    cmd.AddValue ("Rate1a", "e.g. 50Mbps", rate1a);
    cmd.AddValue ("Delay0a", "e.g. 80ms", delay0a);
    cmd.AddValue ("Delay1a", "e.g. 20ms", delay1a);
    cmd.AddValue ("Rate0b", "e.g. 5Mbps", rate0b);
    cmd.AddValue ("Rate1b", "e.g. 50Mbps", rate1b);
    cmd.AddValue ("Delay0b", "e.g. 80ms", delay0b);
    cmd.AddValue ("Delay1b", "e.g. 20ms", delay1b);
    cmd.AddValue ("Size", "e.g. 5242880", myRandomNo);
    cmd.AddValue ("Seed", "e.g. 1", seed);
    cmd.AddValue ("LossRate", "e.g. 0.0001", lossrate);
    cmd.AddValue ("CcAlgorithm", "Congestion control algorithm (newreno, cubic, highspeed, westwood, etc.) - case insensitive", ccAlgorithm);
    cmd.AddValue ("Throughput", "Maximum achievable throughput in Mbps (e.g. 100)", throughputMax);
    cmd.Parse (argc, argv);
    
    // If throughputMax is specified, use it to set rate0a and rate0b
    if (throughputMax > 0.0) {
        rate0a = throughputMax;  // Set minimum to the specified throughput
        rate0b = throughputMax;  // Set maximum to the specified throughput
        NS_LOG_INFO("Throughput set to " << throughputMax << " Mbps. Rate range: " << rate0a << "-" << rate0b << " Mbps");
    }

    NS_LOG_INFO("\n\n#################### SIMULATION SET-UP ####################\n\n\n");
    
    LogLevel log_precision = LOG_LEVEL_LOGIC;
    Time::SetResolution (Time::NS);
    LogComponentEnableAll (LOG_PREFIX_TIME);
    LogComponentEnableAll (LOG_PREFIX_FUNC);
    LogComponentEnableAll (LOG_PREFIX_NODE);
    LogComponentEnable ("wns3-tcp-one-path", log_precision);

    RngSeedManager::SetSeed (seed);  

    // TCP socket buffer configurations - increased to support higher throughput (100Mbps links)
    Config::SetDefault ("ns3::TcpSocket::SndBufSize",UintegerValue (100000000));  // 100MB
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize",UintegerValue (100000000));  // 100MB
    
    // TCP optimizations for high-speed networks (similar to QUIC performance)
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));  // Larger segment size (Ethernet MTU - headers = 1500 - 40 - 12 = 1448)
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (20));  // Larger initial window for high BDP networks    

    // Set congestion control algorithm from command-line argument
    std::string ccTypeString = getTcpCcAlgorithm(ccAlgorithm);
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (ccTypeString));
    NS_LOG_INFO("Using TCP congestion control algorithm: " << ccTypeString); 

    
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

    uint32_t maxBytes = UINT32_MAX;// stoi(myRandomNo);
    
    NS_LOG_INFO ("Create nodes.");
    NodeContainer c;
    c.Create (9);  // Only create nodes 0-8 (we use 0, 1, 2, 4, 5, 8)
    NodeContainer n0n1 = NodeContainer (c.Get (0), c.Get (1));
    NodeContainer n1n8 = NodeContainer (c.Get (1), c.Get (8));
    NodeContainer n8n2 = NodeContainer (c.Get (8), c.Get (2));

    NodeContainer n4n1 = NodeContainer (c.Get (4), c.Get (1));
    NodeContainer n8n5 = NodeContainer (c.Get (8), c.Get (5));
 
    InternetStackHelper internet;
    // Only install internet stack on nodes that are actually used
    internet.Install (c.Get (0));
    internet.Install (c.Get (1));
    internet.Install (c.Get (2));
    internet.Install (c.Get (4));
    internet.Install (c.Get (5));
    internet.Install (c.Get (8));


    // We create the channels first without any IP addressing information
    NS_LOG_INFO ("Create channels.");
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue (std::to_string(rateVal0->GetValue())+"Mbps"));
    p2p.SetChannelAttribute ("Delay", StringValue (std::to_string(delayVal0->GetValue())+"ms"));
    NetDeviceContainer d1d8 = p2p.Install (n1n8);
    d1d8.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

    p2p.SetDeviceAttribute ("DataRate", StringValue ("1000Mbps"));  // Increased to 1Gbps for access links
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
    
    BulkSendHelper source ("ns3::TcpSocketFactory",
                            InetSocketAddress (i8i5.GetAddress (1), port2));
    // Set the amount of data to send in bytes.  Zero is unlimited.
    source.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
    ApplicationContainer sourceApps = source.Install (c.Get (4));
    sourceApps.Start (Seconds (start_time));
    sourceApps.Stop (Seconds(simulationEndTime));

    PacketSinkHelper sink2 ("ns3::TcpSocketFactory",
                            InetSocketAddress (Ipv4Address::GetAny (), port2));
    ApplicationContainer sinkApps2 = sink2.Install (c.Get (5));
    sinkApps2.Start (Seconds (0.0));
    sinkApps2.Stop (Seconds(simulationEndTime));

    // Schedule trace connections for TCP sender (node 4) and receiver (node 5)
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

    NS_LOG_INFO("\nfile size: "<<maxBytes<< "Bytes, TCP"
                "\npath 0: rate "<< rate0a <<", delay "<< delay0a << 
                "\npath 1: rate " << rate1a << ", delay " << delay1a );

    Simulator::Destroy ();

    return 0;
}
