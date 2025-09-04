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
 *   You should have received a copy of this program along with this program;
 *   if not, write to the Free Software Foundation, Inc., 59 Temple Place,
 *   Suite 330, Boston, MA  02111-1307  USA
 *
 *   Author: Marco Miozzo <marco.miozzo@cttc.es>
 *           Nicola Baldo  <nbaldo@cttc.es>
 *
 *   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu>
 *              Sourjya Dutta <sdutta@nyu.edu>
 *              Russell Ford <russell.ford@nyu.edu>
 *              Menglei Zhang <menglei@nyu.edu>
 *              Muhammad Adeel Zahid <zahidma@myumanitoba.ca> 
 *              Traffic Generators Integration for NTN-IAB
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
#include "ns3/traffic-generator-helper.h"
#include "ns3/traffic-generator-ngmn-ftp-multi.h"
#include "ns3/traffic-generator-ngmn-video.h"
#include "ns3/traffic-generator-ngmn-gaming.h"
#include "ns3/traffic-generator-ngmn-voip.h"
#include "ns3/traffic-generator-3gpp-generic-video.h"
#include "ns3/traffic-generator-3gpp-audio-data.h"
#include "ns3/traffic-generator-3gpp-pose-control.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/v4ping-helper.h"
#include "ns3/trace-helper.h"
#include "ns3/output-stream-wrapper.h"
// C++ standard library
#include <cmath>
//#include "ns3/gtk-config-store.h"
using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("MmWaveIabGrid");

// TCP Trace Callback Function with Node Information
void TcpRxTraceCallbackWithNode(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet, const Address& from, uint32_t nodeId, uint32_t ueIndex)
{
    InetSocketAddress address = InetSocketAddress::ConvertFrom(from);
    
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t"
                        << address.GetIpv4() << "\t"
                        << address.GetPort() << "\t"
                        << packet->GetSize() << "\t"
                        << "TCP" << "\t"
                        << nodeId << "\t"
                        << ueIndex << std::endl;
}

// Wrapper function for binding node information
void TcpRxTraceWrapper(Ptr<OutputStreamWrapper> stream, uint32_t nodeId, uint32_t ueIndex, Ptr<const Packet> packet, const Address& from)
{
    TcpRxTraceCallbackWithNode(stream, packet, from, nodeId, ueIndex);
}

// Adaptive Feedback Loop Functions for Traffic Generators
struct NetworkMetrics
{
    double packetLoss;
    double throughput;
    double delay;
    double jitter;
    uint64_t bytesReceived;
    uint32_t packetsReceived;
    Time timestamp;
};

// Global storage for tracking metrics per UE
std::map<uint32_t, NetworkMetrics> g_ueMetrics;
std::map<uint32_t, Ptr<TrafficGenerator3gppGenericVideo>> g_videoTgs;
std::map<uint32_t, Ptr<PacketSink>> g_packetSinks;

// Global configuration for adaptive feedback
bool g_enableAdaptiveFeedback = true;
double g_feedbackInterval = 1; // dummy value

// Global file stream for adaptive feedback logging
Ptr<OutputStreamWrapper> g_adaptiveFeedbackLog;

// Function to calculate network performance metrics
NetworkMetrics CalculateNetworkMetrics(uint32_t ueIndex, Ptr<PacketSink> sink)
{
    NetworkMetrics metrics;
    metrics.timestamp = Simulator::Now();
    
    if (g_ueMetrics.find(ueIndex) == g_ueMetrics.end())
    {
        // First time, initialize
        metrics.packetLoss = 0.0;
        metrics.throughput = 0.0;
        metrics.delay = 0.0;
        metrics.jitter = 0.0;
        metrics.bytesReceived = sink->GetTotalRx();
        metrics.packetsReceived = 0; // PacketSink doesn't provide packet count directly
    }
    else
    {
        NetworkMetrics& prev = g_ueMetrics[ueIndex];
        
        // Calculate throughput (bytes per second)
        uint64_t bytesDiff = sink->GetTotalRx() - prev.bytesReceived;
        NS_LOG_UNCOND("bytesDiff = sink->GetTotalRx() - prev.bytesReceived = " << sink->GetTotalRx() << " - " << prev.bytesReceived << " = " << bytesDiff);
        double timeDiff = (metrics.timestamp - prev.timestamp).GetSeconds();
        NS_LOG_UNCOND("timeDiff = (metrics.timestamp - prev.timestamp).GetSeconds() = " << metrics.timestamp.GetSeconds() << " - " << prev.timestamp.GetSeconds() << " = " << timeDiff);
        metrics.throughput = (timeDiff > 0) ? (bytesDiff / timeDiff) : 0.0;
        NS_LOG_UNCOND("metrics.throughput = (timeDiff > 0) ? (bytesDiff / timeDiff) : 0.0 = " << metrics.throughput);
        
        // Estimate packet loss based on expected vs actual throughput
        // Get current parameters from the traffic generator for accurate calculation
        double expectedBytesPerSecond = 0.0;
        if (g_videoTgs.find(ueIndex) != g_videoTgs.end())
        {
            Ptr<TrafficGenerator3gppGenericVideo> videoTg = g_videoTgs[ueIndex];
            
            // Get current FPS and data rate from the traffic generator
            UintegerValue fpsValue;
            videoTg->GetAttribute("Fps", fpsValue);
            uint32_t currentFps = fpsValue.Get();
            
            DoubleValue dataRateValue;
            videoTg->GetAttribute("DataRate", dataRateValue);
            double currentDataRate = dataRateValue.Get();
            
            // Calculate expected packet size based on current parameters
            // Formula: (dataRate * 1e6) / (fps * 8) - same as in TrafficGenerator3gppGenericVideo
            double expectedPacketSize = (currentDataRate * 1e6) / (currentFps)/8;
            
            // Calculate expected throughput: fps * packet_size
            expectedBytesPerSecond = currentFps * expectedPacketSize;
            NS_LOG_UNCOND("FPS: " << currentFps << ", DataRate: " << currentDataRate << ", ExpectedPacketSize: " << expectedPacketSize << ", ExpectedThroughput: " << expectedBytesPerSecond);
            NS_LOG_UNCOND("UE " << ueIndex << " - Current params: FPS=" << currentFps 
                              << ", DataRate=" << currentDataRate << " Mbps"
                              << ", ExpectedPacketSize=" << expectedPacketSize << " bytes"
                              << ", ExpectedThroughput=" << expectedBytesPerSecond << " bytes/sec");
        }
        else
        {
            // Fallback to default values if traffic generator not found
            expectedBytesPerSecond = 60.0 * 10475.0; // 628,500 bytes/sec
            NS_LOG_WARN("Traffic generator not found for UE " << ueIndex << ", using default expected throughput");
        }
        
        double actualBytesPerSecond = metrics.throughput;
        metrics.packetLoss = std::max(0.0, 1.0 - (actualBytesPerSecond / expectedBytesPerSecond));
        
        // Estimate delay (simplified - could be enhanced with actual RTT measurements)
        metrics.delay = 0.05; // 50ms base delay for NTN
        
        // Estimate jitter (simplified)
        metrics.jitter = 0.01; // 10ms base jitter
        
        metrics.bytesReceived = sink->GetTotalRx();
        metrics.packetsReceived = prev.packetsReceived + 1;
    }
    
    return metrics;
}

// Main feedback loop function that calls ReceiveLoopbackInformation
void NetworkFeedbackLoop(uint32_t ueIndex)
{
    if (g_videoTgs.find(ueIndex) == g_videoTgs.end() || 
        g_packetSinks.find(ueIndex) == g_packetSinks.end())
    {
        NS_LOG_WARN("NetworkFeedbackLoop: Missing traffic generator or sink for UE " << ueIndex);
        return;
    }
    
    Ptr<TrafficGenerator3gppGenericVideo> videoTg = g_videoTgs[ueIndex];
    Ptr<PacketSink> sink = g_packetSinks[ueIndex];
    
    // Snapshot previous metrics before computing new ones (avoid reading after overwrite)
    NetworkMetrics prev;
    bool hasPrev = false;
    {
        auto itPrev = g_ueMetrics.find(ueIndex);
        if (itPrev != g_ueMetrics.end())
        {
            prev = itPrev->second;
            hasPrev = true;
        }
    }
    
    // Calculate current network metrics
    NetworkMetrics metrics = CalculateNetworkMetrics(ueIndex, sink);
    
    // Store metrics for next iteration
    g_ueMetrics[ueIndex] = metrics;
    
    // Get OLD parameters before adaptation
    UintegerValue oldFpsValue;
    DoubleValue oldDataRateValue;
    videoTg->GetAttribute("Fps", oldFpsValue);
    videoTg->GetAttribute("DataRate", oldDataRateValue);
    uint32_t oldFps = oldFpsValue.Get();
    double oldDataRate = oldDataRateValue.Get();
    
    // Log the current metrics before adaptation
    NS_LOG_UNCOND("=== UE " << ueIndex << " Network Metrics (Before Adaptation) ===");
    NS_LOG_UNCOND("Time: " << metrics.timestamp.GetSeconds() << "s");
    NS_LOG_UNCOND("Packet Loss: " << (metrics.packetLoss * 100) << "%");
    NS_LOG_UNCOND("Throughput: " << (metrics.throughput / 1024) << " KB/s");
    NS_LOG_UNCOND("Delay: " << (metrics.delay * 1000) << " ms");
    NS_LOG_UNCOND("Jitter: " << (metrics.jitter * 1000) << " ms");
    NS_LOG_UNCOND("Bytes Received: " << metrics.bytesReceived);
    NS_LOG_UNCOND("OLD Parameters - FPS: " << oldFps << ", DataRate: " << oldDataRate << " Mbps");
    NS_LOG_UNCOND("================================================");
    
    // Note: We'll log the parameter changes after adaptation
    
    // Call ReceiveLoopbackInformation to adapt the traffic generator
    NS_LOG_UNCOND("Calling ReceiveLoopbackInformation for UE " << ueIndex << "...");
    uint32_t packetReceived = 0;
    double windowInSeconds = 0.0;
    try
    {
        // Compute window length and estimate packets received in the window
        windowInSeconds = 0.0;
        packetReceived = 0;
        {
            // Use the snapped previous metrics to compute the measurement window
            if (hasPrev)
            {
                windowInSeconds = (metrics.timestamp - prev.timestamp).GetSeconds();
                if (windowInSeconds < 1e-9)
                {
                    windowInSeconds = g_feedbackInterval; // fallback to configured interval
                }

                // Estimate packet count from bytes diff and current expected packet size
                double expectedPacketSizeBytes = 12000.0; // default fallback
                UintegerValue fpsVal;
                DoubleValue dataRateVal;
                videoTg->GetAttribute("Fps", fpsVal);
                videoTg->GetAttribute("DataRate", dataRateVal);
                const uint32_t fpsNow = fpsVal.Get();
                const double dataRateMbpsNow = dataRateVal.Get();
                if (fpsNow > 0 && dataRateMbpsNow > 0.0)
                {
                    expectedPacketSizeBytes = (dataRateMbpsNow * 1e6) / (static_cast<double>(fpsNow) * 8.0);
                }
                const uint64_t bytesDiff = metrics.bytesReceived - prev.bytesReceived;
                packetReceived = static_cast<uint32_t>(std::round(bytesDiff / expectedPacketSizeBytes));
                NS_LOG_UNCOND("expectedPacketSizeBytes: " << expectedPacketSizeBytes << ", bytesDiff: " << bytesDiff << ", packetReceived: " << packetReceived);
            }
            else
            {
                windowInSeconds = g_feedbackInterval; // first iteration fallback
                packetReceived = 0;
            }
        }

        // Call with correct semantics and argument order
        videoTg->ReceiveLoopbackInformation(
            metrics.packetLoss,               // packetLoss (0..1)
            packetReceived,                   // packetReceived (count)
            windowInSeconds,                  // windowInSeconds
            MilliSeconds(metrics.delay * 1000),   // packetDelay
            MilliSeconds(metrics.jitter * 1000)   // packetDelayJitter
        );
        
        NS_LOG_UNCOND(" ReceiveLoopbackInformation called successfully for UE " << ueIndex);
        
        // Get NEW parameters after adaptation
        UintegerValue newFpsValue;
        DoubleValue newDataRateValue;
        videoTg->GetAttribute("Fps", newFpsValue);
        videoTg->GetAttribute("DataRate", newDataRateValue);
        uint32_t newFps = newFpsValue.Get();
        double newDataRate = newDataRateValue.Get();
        
        // Log the adaptation results
        NS_LOG_UNCOND("=== UE " << ueIndex << " Adaptation Applied ===");
        NS_LOG_UNCOND("Traffic generator parameters updated based on:");
        NS_LOG_UNCOND("- Packet Loss: " << (metrics.packetLoss * 100) << "%");
        NS_LOG_UNCOND("- Throughput: " << (metrics.throughput / 1024) << " KB/s");
        NS_LOG_UNCOND("- Delay: " << (metrics.delay * 1000) << " ms");
        NS_LOG_UNCOND("- Jitter: " << (metrics.jitter * 1000) << " ms");
        NS_LOG_UNCOND("NEW Parameters - FPS: " << newFps << ", DataRate: " << newDataRate << " Mbps");
        NS_LOG_UNCOND("Parameter Changes - FPS: " << oldFps << " -> " << newFps 
                      << ", DataRate: " << oldDataRate << " -> " << newDataRate << " Mbps");
        NS_LOG_UNCOND("================================================");
        
        // Log to file: Parameter changes in the requested format
        if (g_adaptiveFeedbackLog)
        {
            // Get previous bytes received for comparison
            uint64_t prevBytesReceived = hasPrev ? prev.bytesReceived : 0;
            double throughputMBps = packetReceived / windowInSeconds / 1024*1024;
            
            *g_adaptiveFeedbackLog->GetStream() 
                << "Time: " << metrics.timestamp.GetSeconds() 
                << ", UE index: " << ueIndex 
                << ", Packet Loss: " << (metrics.packetLoss * 100) << "%"
                << ", Data received: " << prevBytesReceived << " -> " << metrics.bytesReceived
                << ", Throughput: " << throughputMBps << " MB/s"
                << ", FPS: " << oldFps << " -> " << newFps
                << ", DataRate: " << oldDataRate << " -> " << newDataRate << " Mbps" << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("✗ Error calling ReceiveLoopbackInformation for UE " << ueIndex << ": " << e.what());
        
        // Log error to file
        if (g_adaptiveFeedbackLog)
        {
            // Get previous bytes received for comparison
            uint64_t prevBytesReceived = hasPrev ? prev.bytesReceived : 0;
            
            *g_adaptiveFeedbackLog->GetStream() 
                << "Time: " << metrics.timestamp.GetSeconds() 
                << ", UE index: " << ueIndex 
                << ", Packet Loss: " << (metrics.packetLoss * 100) << "%"
                << ", Data received: " << prevBytesReceived << " -> " << metrics.bytesReceived
                << ", ERROR: " << e.what() << std::endl;
        }
    }
    catch (...)
    {
        NS_LOG_ERROR("✗ Unknown error calling ReceiveLoopbackInformation for UE " << ueIndex);
        
        // Log error to file
        if (g_adaptiveFeedbackLog)
        {
            // Get previous bytes received for comparison
            uint64_t prevBytesReceived = hasPrev ? prev.bytesReceived : 0;
            
            *g_adaptiveFeedbackLog->GetStream() 
                << "Time: " << metrics.timestamp.GetSeconds() 
                << ", UE index: " << ueIndex 
                << ", Packet Loss: " << (metrics.packetLoss * 100) << "%"
                << ", Data received: " << prevBytesReceived << " -> " << metrics.bytesReceived
                << ", ERROR: Unknown error" << std::endl;
        }
    }
    
    // Schedule next feedback iteration using configurable interval
    Simulator::Schedule(Seconds(g_feedbackInterval), &NetworkFeedbackLoop, ueIndex);
}

// Function to initialize the adaptive feedback system
void InitializeAdaptiveFeedback(uint32_t ueIndex, Ptr<TrafficGenerator3gppGenericVideo> videoTg, Ptr<PacketSink> sink)
{
    NS_LOG_UNCOND("Initializing adaptive feedback for UE " << ueIndex);
    
    // Store references for the feedback loop
    g_videoTgs[ueIndex] = videoTg;
    g_packetSinks[ueIndex] = sink;
    
    // Initialize metrics for this UE
    NetworkMetrics initialMetrics;
    initialMetrics.timestamp = Simulator::Now();
    initialMetrics.packetLoss = 0.0;
    initialMetrics.throughput = 0.0;
    initialMetrics.delay = 0.05; // 50ms base delay for NTN
    initialMetrics.jitter = 0.01; // 10ms base jitter
    initialMetrics.bytesReceived = sink->GetTotalRx();
    initialMetrics.packetsReceived = 0;
    
    g_ueMetrics[ueIndex] = initialMetrics;
    
    NS_LOG_UNCOND("Adaptive feedback initialized for UE " << ueIndex);
    NS_LOG_UNCOND("  - Traffic Generator: " << videoTg->GetInstanceTypeId().GetName());
    NS_LOG_UNCOND("  - Packet Sink: " << sink->GetInstanceTypeId().GetName());
    NS_LOG_UNCOND("  - Initial Metrics: " << initialMetrics.bytesReceived << " bytes, " 
                   << (initialMetrics.delay * 1000) << "ms delay, " 
                   << (initialMetrics.jitter * 1000) << "ms jitter");
    
    // Start the feedback loop after a delay to allow initial traffic
    Simulator::Schedule(Seconds(1.0), &NetworkFeedbackLoop, ueIndex);
    
    NS_LOG_UNCOND("  - Feedback loop scheduled to start at t="<<g_feedbackInterval<<"s");
}

// Original TCP Trace Callback Function (keep for backward compatibility)
void TcpRxTraceCallback(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet, const Address& from)
{
    InetSocketAddress address = InetSocketAddress::ConvertFrom(from);
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t"
                        << address.GetIpv4() << "\t"
                        << address.GetPort() << "\t"
                        << packet->GetSize() << "\t"
                        << "TCP" << std::endl;
}

// Traffic type enumeration
enum TrafficType
{
    NGMN_FTP = 0,
    NGMN_VIDEO = 1,
    NGMN_GAMING = 2,
    NGMN_VOIP = 3,
    THREE_GPP_VIDEO = 4,
    THREE_GPP_AUDIO = 5,
    THREE_GPP_POSE = 6
};

// Helper function to get traffic generator TypeId
TypeId
GetTrafficGeneratorTypeId(const std::string& trafficType)
{
    if (trafficType == "ftp" || trafficType == "0")
        return TrafficGeneratorNgmnFtpMulti::GetTypeId();
    else if (trafficType == "video" || trafficType == "1")
        return TrafficGeneratorNgmnVideo::GetTypeId();
    else if (trafficType == "gaming" || trafficType == "2")
        return TrafficGeneratorNgmnGaming::GetTypeId();
    else if (trafficType == "voip" || trafficType == "3")
        return TrafficGeneratorNgmnVoip::GetTypeId();
    else if (trafficType == "3gpp-video" || trafficType == "4")
        return TrafficGenerator3gppGenericVideo::GetTypeId();
    else if (trafficType == "3gpp-audio" || trafficType == "5")
        return TrafficGenerator3gppAudioData::GetTypeId();
    else if (trafficType == "3gpp-pose" || trafficType == "6")
        return TrafficGenerator3gppPoseControl::GetTypeId();
    else
    {
        NS_LOG_WARN("Unknown traffic type: " << trafficType << ". Using NGMN FTP as default.");
        return TrafficGeneratorNgmnFtpMulti::GetTypeId();
    }
}

// Helper function to get traffic type name
std::string
GetTrafficTypeName(const std::string& trafficType)
{
    if (trafficType == "ftp" || trafficType == "0")
        return "ftp";
    else if (trafficType == "video" || trafficType == "1")
        return "video";
    else if (trafficType == "gaming" || trafficType == "2")
        return "gaming";
    else if (trafficType == "voip" || trafficType == "3")
        return "voip";
    else if (trafficType == "3gpp-video" || trafficType == "4")
        return "3gpp-video";
    else if (trafficType == "3gpp-audio" || trafficType == "5")
        return "3gpp-audio";
    else if (trafficType == "3gpp-pose" || trafficType == "6")
        return "3gpp-pose";
    else
        return "ftp";
}

// Helper function to convert simple protocol names to NS-3 format
std::string
GetNs3TransportProtocol(const std::string& protocol)
{
    if (protocol == "udp" || protocol == "UDP")
        return "ns3::UdpSocketFactory";
    else if (protocol == "tcp" || protocol == "TCP")
        return "ns3::TcpSocketFactory";
    else
    {
        NS_LOG_WARN("Unknown transport protocol: " << protocol << ". Using UDP as default.");
        return "ns3::UdpSocketFactory";
    }
}

void 
PrintGnuplottableBuildingListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  uint32_t index = 0;
  for (BuildingList::Iterator it = BuildingList::Begin (); it != BuildingList::End (); ++it)
    {
      ++index;
      Box box = (*it)->GetBoundaries ();
      outFile << "set object " << index
              << " rect from " << box.xMin  << "," << box.yMin
              << " to "   << box.xMax  << "," << box.yMax
              //<< " height " << box.zMin << "," << box.zMax
              << " front fs empty "
              << std::endl;
    }
}
void 
PrintGnuplottableUeListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  
  outFile << "# Node ID, IMSI, Position (x, y, z)" << std::endl; // Add a header to the output file
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      uint32_t nodeId = node->GetId(); // Get the Node ID
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteUeNetDevice> uedev = node->GetDevice (j)->GetObject <LteUeNetDevice> ();
          Ptr<MmWaveUeNetDevice> mmuedev = node->GetDevice (j)->GetObject <MmWaveUeNetDevice> ();
          Ptr<McUeNetDevice> mcuedev = node->GetDevice (j)->GetObject <McUeNetDevice> ();
          if (uedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "Node ID: " << nodeId << ", IMSI: " << uedev->GetImsi()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            }
          else if (mmuedev)
           {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "Node ID: " << nodeId << ", IMSI: " << mmuedev->GetImsi()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            }
          else if (mcuedev)
           {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "Node ID: " << nodeId << ", IMSI: " << mcuedev->GetImsi()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            } 
        }
    }
}
void 
PrintGnuplottableEnbListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  outFile << "# Node ID, Cell ID, Position (x, y, z)" << std::endl; // Add a header to the output file
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      uint32_t nodeId = node->GetId(); // Get the Node ID
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteEnbNetDevice> enbdev = node->GetDevice (j)->GetObject <LteEnbNetDevice> ();
          Ptr<MmWaveEnbNetDevice> mmdev = node->GetDevice (j)->GetObject <MmWaveEnbNetDevice> ();
          Ptr<MmWaveIabNetDevice> mmIabdev = node->GetDevice (j)->GetObject <MmWaveIabNetDevice> ();
          if (enbdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "ENB Node ID: " << nodeId << ", Cell ID: " << enbdev->GetCellId()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            }
          else if (mmdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "mmdev Node ID: " << nodeId << ", Cell ID: " << mmdev->GetCellId()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            } 
          else if (mmIabdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "IAB Node ID: " << nodeId << ", Cell ID: " << mmIabdev->GetCellId()
                      << ", Position=( " << pos.x << ", " << pos.y << ", " << pos.z << ")"
                      << std::endl;
            } 
        }
    }
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
  LogComponentEnable("TrafficGenerator3gppGenericVideo", LOG_LEVEL_ALL);
  LogComponentEnable("TrafficGenerator", LOG_LEVEL_ALL);
  LogComponentEnable("PacketSink", LOG_LEVEL_ALL);
  CommandLine cmd; 
  unsigned run = 0;
  bool rlcAm = true;
  uint32_t numRelays = 0;
  uint32_t rlcBufSize = 10;
  uint32_t interPacketInterval = 200;
  uint32_t throughput = 200;
  uint32_t packetSize = 1400; //bytes
  
  // Traffic generator options
  std::string trafficType = "NA";  // Default traffic type
  bool useTrafficGenerators = false;  // Flag to enable/disable traffic generators
  uint32_t appDuration = 30;  // Application duration in seconds
  std::string transportProtocol = "udp";  // Default transport protocol (can be "udp" or "tcp")
  bool enablePing = false;  // Enable ping for ARP cache seeding

  
  cmd.AddValue("run", "run for RNG (for generating different deterministic sequences for different drops)", run);
  cmd.AddValue("am", "RLC AM if true", rlcAm);
  cmd.AddValue("numRelay", "Number of relays", numRelays);
  cmd.AddValue("rlcBufSize", "RLC buffer size [MB]", rlcBufSize);
  cmd.AddValue("throughput", "throughput [mbps]", throughput);
  cmd.AddValue("intPck", "interPacketInterval [us]", interPacketInterval);
  
  // New traffic generator options
  cmd.AddValue("trafficType", "Traffic type: ftp(0), video(1), gaming(2), voip(3), 3gpp-video(4), 3gpp-audio(5), 3gpp-pose(6)", trafficType);
  cmd.AddValue("useTrafficGenerators", "Enable traffic generators instead of basic UDP", useTrafficGenerators);
  cmd.AddValue("appDuration", "Application duration in seconds", appDuration);
  cmd.AddValue("transportProtocol", "Transport protocol: udp or tcp", transportProtocol);
  cmd.AddValue("enablePing", "Enable ping for ARP cache seeding", enablePing);
  
  // Adaptive feedback options
  bool enableAdaptiveFeedback = true;  // Enable adaptive feedback for 3GPP video traffic
  double feedbackInterval = 0.2;  // Feedback interval in seconds
  cmd.AddValue("enableAdaptiveFeedback", "Enable adaptive feedback for 3GPP video traffic generators", enableAdaptiveFeedback);
  cmd.AddValue("feedbackInterval", "Feedback interval in seconds for adaptive traffic generation", feedbackInterval);
  
  cmd.Parse(argc, argv);
  
  // Global variables for adaptive feedback (accessible by callback functions)
  // Must be set AFTER command line parsing to get the correct values
  extern bool g_enableAdaptiveFeedback;
  extern double g_feedbackInterval;
  extern Ptr<OutputStreamWrapper> g_adaptiveFeedbackLog;
  g_enableAdaptiveFeedback = enableAdaptiveFeedback;
  g_feedbackInterval = feedbackInterval;

  if (trafficType != "NA")
  {
    useTrafficGenerators = true;
  }

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
  
  // TrafficGenerator3gppGenericVideo Configuration
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::DataRate", DoubleValue(60.0));  // Mbps
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MinDataRate", DoubleValue(1.0));  // Mbps 240p @ 30 fps
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MaxDataRate", DoubleValue(590.0));  // Mbps 8K @ 60 fps

  // Adaptive feedback thresholds
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::LowerThresholdForDecreasingSlowly", DoubleValue(0.10));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::LowerThresholdForDecreasingQuickly", DoubleValue(1.0));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::UpperThresholdForIncreasing", DoubleValue(0.02));

  // Adaptive feedback multipliers
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::IncreaseDataRateMultiplier", DoubleValue(2.1));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::DecreaseDataRateSlowlyMultiplier", DoubleValue(0.5));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::DecreaseDataRateQuicklyMultiplier", DoubleValue(0.7));

  // Frame rate settings
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::Fps", UintegerValue(60));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MinFps", UintegerValue(10));
  Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MaxFps", UintegerValue(240));

  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1400));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (20 *1024 * 1024));
	Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (1024 * 1024));
	Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue (1024 * 1024));
	Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (131072*50));
	Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (131072*50));

  
  // Packet size variation (3GPP TR 38.838 parameters)
  // Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::StdRatioPacketSize", DoubleValue(0.105));  // 10.5%
  // Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MinRatioPacketSize", DoubleValue(0.5));    // 50%
  // Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::MaxRatioPacketSize", DoubleValue(1.5));    // 150%

  // Algorithm type for adaptation
  // Config::SetDefault("ns3::TrafficGenerator3gppGenericVideo::AlgType", EnumValue(TrafficGenerator3gppGenericVideo::ADJUST_IPA_TIME));




  // Add buffer safety configurations for traffic generators
  if (useTrafficGenerators)
  {
    // Increase buffer sizes to handle larger packets from traffic generators
    Config::SetDefault("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue(100 * 1024 * 1024));  // 100MB
    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(100 * 1024 * 1024));  // 100MB
    Config::SetDefault("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue(100 * 1024 * 1024));  // 100MB
    
    // Increase socket buffer sizes
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1024 * 1024));  // 1MB
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1024 * 1024));  // 1MB
    Config::SetDefault("ns3::UdpSocket::RcvBufSize", UintegerValue(1024 * 1024));  // 1MB
    
    NS_LOG_UNCOND("Buffer sizes increased for traffic generator compatibility");
  }
  
  // Enable multi-beam functionality
//  Config::SetDefault("ns3::MmWavePhyMacCommon::NumEnbLayers", UintegerValue(2));
  
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
  // Ptr<MmWavePhyMacCommon> phyMacConfig = mmwaveHelper->GetPhyMacConfigurable();
  // NS_LOG_UNCOND("=== BANDWIDTH VERIFICATION ===");
  // NS_LOG_UNCOND("ChunkWidth: " << phyMacConfig->GetChunkWidth() / 1e6 << " MHz");
  // NS_LOG_UNCOND("ChunkPerRB: " << phyMacConfig->GetNumChunkPerRb());
  // NS_LOG_UNCOND("ResourceBlockNum: " << phyMacConfig->GetNumRb());
  // NS_LOG_UNCOND("Total Bandwidth: " << (phyMacConfig->GetChunkWidth() * phyMacConfig->GetNumChunkPerRb() * phyMacConfig->GetNumRb()) / 1e6 << " MHz");
  // NS_LOG_UNCOND("Center Frequency: " << phyMacConfig->GetCenterFrequency() / 1e9 << " GHz");
  // NS_LOG_UNCOND("================================");
  
  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults();
  // parse again so you can override default values from the command line
  cmd.Parse(argc, argv);
  NS_LOG_UNCOND("Throughput: "<<throughput<<" Inter-packet interval: "<<interPacketInterval);
  
  // Log traffic generator configuration
  if (useTrafficGenerators)
  {
    NS_LOG_UNCOND("=== TRAFFIC GENERATOR CONFIGURATION ===");
    NS_LOG_UNCOND("Traffic Type: " << trafficType << " (" << GetTrafficTypeName(trafficType) << ")");
    NS_LOG_UNCOND("Transport Protocol: " << transportProtocol);
    NS_LOG_UNCOND("Application Duration: " << appDuration << " seconds");
    NS_LOG_UNCOND("Enable Ping: " << (enablePing ? "Yes" : "No"));
    NS_LOG_UNCOND("=======================================");
    
    // Validate traffic type
    if (GetTrafficTypeName(trafficType) == "ftp" && 
        (trafficType != "ftp" && trafficType != "0"))
    {
      NS_LOG_WARN("Invalid traffic type specified: " << trafficType << ". Using default: ftp");
      trafficType = "ftp";
    }
    

    
    // Validate transport protocol
    if (transportProtocol != "udp" && transportProtocol != "tcp" && 
        transportProtocol != "UDP" && transportProtocol != "TCP")
    {
      NS_LOG_WARN("Invalid transport protocol: " << transportProtocol << ". Using default: udp");
      transportProtocol = "udp";
    }
    
    // Validate application duration
    if (appDuration < 1 || appDuration > 3600)
    {
      NS_LOG_WARN("Invalid application duration: " << appDuration << " seconds. Using default: 2 seconds");
      appDuration = 30;
    }
  }
  else
  {
    NS_LOG_UNCOND("Using basic UDP applications (default behavior)");
  }
 
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

  double xMax = 3000.0;
  double yMax = 3000.0;

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
  ueNodes.Create(1);
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
  
  //     uePosAlloc->Add(Vector(x, y, z));
  // }

  // Create one user at fixed position 100 meters away from eNB
  //eNB is at center (xMax/2, yMax/2, gnbHeight)
  //Place UE 100 meters north of eNB
  double ueX = xMax/2.0+1000;  // Same X coordinate as eNB
  double ueY = yMax/2.0 + 1000.0;  // 100 meters north of eNB
  double ueZ = 1.7;  // Typical UE height
  
  uePosAlloc->Add(Vector(ueX, ueY, ueZ));
  
  // IAB and donor positions
  std::vector<Vector> clusterCenters = {
    posIab1, posIab2, posIab3, posIab4, posIab5, posIab6, posWired // 6 IABs + donor
  };

// Additional user positioning code (no longer needed)
// uint32_t totalUes = ueNodes.GetN();        // e.g., 20
// uint32_t clusterCount = clusterCenters.size(); // 7 clusters
// uint32_t baseUesPerCluster = totalUes / clusterCount;     // 2 UEs per cluster
// uint32_t extraUes = totalUes % clusterCount;              // Remaining UEs to distribute

  // Ptr<UniformRandomVariable> offsetX = CreateObject<UniformRandomVariable>();
  // Ptr<UniformRandomVariable> offsetY = CreateObject<UniformRandomVariable>();
  // double max_distance = 100;//sxMax - (xMax/2.0 + xOffset) - 100;
  // NS_LOG_UNCOND("max distance of UE from base station: "<<max_distance);

  // double zHeight = 1.7;
  // double offset = 50; // distance from IAB center to each UE

  // for (const Vector& center : clusterCenters)
  // {
  //     // Diagonal positions around each IAB node
  //     uePosAlloc->Add(Vector(center.x + offset, center.y + offset, zHeight)); // ↗
  //     uePosAlloc->Add(Vector(center.x - offset, center.y - offset, zHeight)); // ↙
  //     uePosAlloc->Add(Vector(center.x - offset, center.y + offset, zHeight)); // ↖
  //     uePosAlloc->Add(Vector(center.x + offset, center.y - offset, zHeight)); // ↘
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
  PrintGnuplottableBuildingListToFile("buildings.txt");// fileName.str ());
  PrintGnuplottableEnbListToFile("enbs.txt");
  PrintGnuplottableUeListToFile("ues.txt");
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
  mmwaveHelper->SetBeamformerType("ns3::MmWaveMMSESpectrumBeamforming");

  // Install and start applications on UEs and remote host
  uint16_t dlPort = 1234;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  
  if (useTrafficGenerators)
  {
    // Use Traffic Generators for more realistic traffic patterns
    NS_LOG_UNCOND("Installing traffic generators for " << ueNodes.GetN() << " UEs...");
    
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
      // Convert transport protocol to NS-3 format
      std::string ns3TransportProtocol = GetNs3TransportProtocol(transportProtocol);
      
      // Install appropriate server based on transport protocol
      if (ns3TransportProtocol == "ns3::UdpSocketFactory")
      {
        // Use UdpServer for UDP to get proper traces
        UdpServerHelper dlPacketSinkHelper(dlPort);
        serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
        NS_LOG_UNCOND("UE " << u << ": UdpServer installed on port " << dlPort << " for UDP traffic");
      }
      else
      {
        // Use PacketSink for TCP
        PacketSinkHelper dlPacketSinkHelper(ns3TransportProtocol, 
                                           InetSocketAddress(Ipv4Address::GetAny(), dlPort));
        serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
        NS_LOG_UNCOND("UE " << u << ": PacketSink installed on port " << dlPort << " for TCP traffic");
      }
      
      // Install traffic generator on remote host
      TrafficGeneratorHelper dlTrafficHelper(
        ns3TransportProtocol,
        InetSocketAddress(ueIpIface.GetAddress(u), dlPort),
        GetTrafficGeneratorTypeId(trafficType)
      );
      
      // Traffic generators handle packet sizing automatically based on their internal algorithms
      // No need to set PacketSize attribute - it's not supported
      NS_LOG_UNCOND("UE " << u << ": Traffic generator will use internal packet sizing algorithms for " << GetTrafficTypeName(trafficType) << " traffic");
      
      ApplicationContainer dlApp = dlTrafficHelper.Install(remoteHost);
      clientApps.Add(dlApp);
      
      NS_LOG_UNCOND("UE " << u << ": Traffic generator installed on port " << dlPort 
                         << " with traffic type " << GetTrafficTypeName(trafficType));
      
      // Initialize adaptive feedback for 3GPP video traffic generators
      if ((trafficType == "4" || trafficType == "3gpp-video") && g_enableAdaptiveFeedback)
      {
        // Get the traffic generator application
        Ptr<Application> app = dlApp.Get(0);
        Ptr<TrafficGenerator> tg = DynamicCast<TrafficGenerator>(app);
        
        if (tg)
        {
          // Try to cast to 3GPP video traffic generator
          Ptr<TrafficGenerator3gppGenericVideo> videoTg = DynamicCast<TrafficGenerator3gppGenericVideo>(tg);
          
          if (videoTg)
          {
            // Get the corresponding server application for this UE
            Ptr<Application> serverApp = serverApps.Get(u);
            Ptr<PacketSink> sink = DynamicCast<PacketSink>(serverApp);
            
            if (sink)
            {
              NS_LOG_UNCOND("Setting up adaptive feedback for UE " << u << " with 3GPP video traffic generator");
              
              // Initialize the adaptive feedback system for this UE
              InitializeAdaptiveFeedback(u, videoTg, sink);
            }
            else
            {
              NS_LOG_WARN("Could not get PacketSink for UE " << u << " - adaptive feedback disabled");
            }
          }
          else
          {
            NS_LOG_UNCOND("Traffic generator for UE " << u << " is not 3GPP video type - adaptive feedback disabled");
          }
        }
        else
        {
          NS_LOG_WARN("Could not get TrafficGenerator for UE " << u << " - adaptive feedback disabled");
        }
      }
      else if (trafficType == "4" || trafficType == "3gpp-video")
      {
        NS_LOG_UNCOND("Adaptive feedback disabled for UE " << u << " (use --enableAdaptiveFeedback=true to enable)");
      }
      
      dlPort++;
    }
    
    // Enable ping for ARP cache seeding if requested
    if (enablePing)
    {
      NS_LOG_UNCOND("Enabling ping for ARP cache seeding...");
      V4PingHelper pingHelper(ueIpIface.GetAddress(0));  // Ping first UE
      ApplicationContainer pingApps = pingHelper.Install(remoteHost);
      pingApps.Start(MilliSeconds(10));
      pingApps.Stop(MilliSeconds(500));
      NS_LOG_UNCOND("Ping application installed for ARP cache seeding");
    }
  }
  else
  {
    // Original UDP client-server setup
    NS_LOG_UNCOND("Installing basic UDP applications...");
    
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
      UdpServerHelper dlPacketSinkHelper(dlPort);
      serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
      UdpClientHelper dlClient(ueIpIface.GetAddress(u), dlPort);
      dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(interPacketInterval)));
      dlClient.SetAttribute("PacketSize", UintegerValue(packetSize));
      dlClient.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
      clientApps.Add(dlClient.Install(remoteHost));
    dlPort++;
    }
  }
  
  // Enable TCP packet reception tracing if using TCP
  if (useTrafficGenerators && GetNs3TransportProtocol(transportProtocol) == "ns3::TcpSocketFactory")
  {
    NS_LOG_UNCOND("Enabling TCP packet reception tracing...");
    
    // Create TCP trace file
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> tcpRxStream = ascii.CreateFileStream("TcpPacketRx.txt");
    
    // Write header to trace file
    *tcpRxStream->GetStream() << "Time(s)\tSource_IP\tSource_Port\tPacket_Size\tProtocol\tNode_ID\tUE_Index" << std::endl;
    
    // Connect the trace to each PacketSink application
    for (uint32_t i = 0; i < serverApps.GetN(); i++)
    {
      Ptr<Application> app = serverApps.Get(i);
      Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
      if (sink)
      {
        // Get the node that contains this PacketSink
        Ptr<Node> node = sink->GetNode();
        uint32_t nodeId = node->GetId();
        uint32_t ueIndex = i; // Use the index in serverApps as UE index
        
        // Connect the enhanced callback with node information
        sink->TraceConnectWithoutContext("Rx", 
            MakeBoundCallback(&TcpRxTraceWrapper, tcpRxStream, nodeId, ueIndex));
        NS_LOG_UNCOND("TCP tracing enabled for PacketSink " << i << " on Node " << nodeId << " (UE " << ueIndex << ")");
      }
    }
    
    NS_LOG_UNCOND("TCP tracing enabled. Output will be written to 'TcpPacketRx.txt'");
    
    // Create adaptive feedback logging file for 3GPP video traffic
    if (trafficType == "4" || trafficType == "3gpp-video")
    {
      g_adaptiveFeedbackLog = ascii.CreateFileStream("AdaptiveFeedback.txt");
      *g_adaptiveFeedbackLog->GetStream() << "# Adaptive Feedback Log - Format: Time, UE index, Packet Loss, Data received changes, FPS changes, DataRate changes" << std::endl;
      NS_LOG_UNCOND("Adaptive feedback logging enabled. Output will be written to 'AdaptiveFeedback.txt'");
    }
  }
  else
  {
    NS_LOG_UNCOND("TCP tracing disabled. useTrafficGenerators: " << useTrafficGenerators << " transportProtocol: " << transportProtocol);
  }
  
  // Print coordinates of UEs, ENBs, and IAB nodes using NS_LOG_UNCOND
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
  
  // Start and stop applications based on configuration
  serverApps.Start(Seconds(0.5));
  clientApps.Start(Seconds(0.5));
  
  if (useTrafficGenerators)
  {
    // For traffic generators, use configurable duration
    clientApps.Stop(Seconds(appDuration));
    serverApps.Stop(Seconds(appDuration));
    Simulator::Stop(Seconds(appDuration + 0.5));
    
    NS_LOG_UNCOND("Traffic generators will run for " << appDuration << " seconds");
  }
  else
  {
    // For basic UDP, use fixed duration
    clientApps.Stop(Seconds(2.0));
    serverApps.Stop(Seconds(2.0));
    Simulator::Stop(Seconds(2.5));
    
    NS_LOG_UNCOND("Basic UDP applications will run for 2 seconds");
  }
  
  Simulator::Run();
  
  // Print simulation summary
  if (useTrafficGenerators)
  {
    NS_LOG_UNCOND("\n=== SIMULATION SUMMARY ===");
    NS_LOG_UNCOND("Traffic Type: " << GetTrafficTypeName(trafficType));
    NS_LOG_UNCOND("Transport Protocol: " << transportProtocol);
    NS_LOG_UNCOND("Number of UEs: " << ueNodes.GetN());
    NS_LOG_UNCOND("Number of IAB Nodes: " << numRelays);
    NS_LOG_UNCOND("Simulation Duration: " << appDuration << " seconds");
    NS_LOG_UNCOND("Traffic Generators: ENABLED");
    
    // Add adaptive feedback information
    if (trafficType == "4" || trafficType == "3gpp-video")
    {
      if (g_enableAdaptiveFeedback)
      {
        NS_LOG_UNCOND("Adaptive Feedback: ENABLED (Interval: " << g_feedbackInterval << "s)");
        NS_LOG_UNCOND("Adaptive Feedback Log: AdaptiveFeedback.txt");
      }
      else
      {
        NS_LOG_UNCOND("Adaptive Feedback: DISABLED");
      }
    }
    
    NS_LOG_UNCOND("========================");
  }
  else
  {
    NS_LOG_UNCOND("\n=== SIMULATION SUMMARY ===");
    NS_LOG_UNCOND("Basic UDP Applications: ENABLED");
    NS_LOG_UNCOND("Number of UEs: " << ueNodes.GetN());
    NS_LOG_UNCOND("Number of IAB Nodes: " << numRelays);
    NS_LOG_UNCOND("Simulation Duration: 2 seconds");
    NS_LOG_UNCOND("========================");
  }
  
  /*GtkConfigStore config;
  config.ConfigureAttributes();*/
  Simulator::Destroy();
  return 0;
}