#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dash-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/quic-module.h"

#include <fstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DashOverQuicExample");

// Global trace files
std::map<uint32_t, std::ofstream> g_clientRxFiles;
std::map<uint32_t, std::ofstream> g_serverTxFiles;
std::map<uint32_t, uint64_t> g_clientRxBytes;
std::map<uint32_t, uint64_t> g_serverTxBytes;
std::map<uint32_t, uint32_t> g_clientRxPackets;
std::map<uint32_t, uint32_t> g_serverTxPackets;

// Trace callbacks
static void
CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << newCwnd << std::endl;
}

static void
RttChange(Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << newRtt.GetMilliSeconds() << std::endl;
}

static void
RxCallback(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" 
                        << p->GetSize() << "\t"
                        << "PacketNum:" << q.GetPacketNumber() << std::endl;
}

// DASH Client Rx trace
void DashClientRxTrace(uint32_t clientId, Ptr<const Packet> packet, const Address& from)
{
    if (g_clientRxFiles.find(clientId) == g_clientRxFiles.end())
    {
        std::string filename = "DashClientRx_" + std::to_string(clientId) + ".txt";
        g_clientRxFiles[clientId].open(filename.c_str(), std::ios::out);
        g_clientRxFiles[clientId] << "# Time(s)\tPacketSize(bytes)\tTotalBytes\tTotalPackets\tFromIP\tFromPort" << std::endl;
        g_clientRxBytes[clientId] = 0;
        g_clientRxPackets[clientId] = 0;
    }
    
    g_clientRxBytes[clientId] += packet->GetSize();
    g_clientRxPackets[clientId]++;
    
    InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
    g_clientRxFiles[clientId] << Simulator::Now().GetSeconds() << "\t"
                              << packet->GetSize() << "\t"
                              << g_clientRxBytes[clientId] << "\t"
                              << g_clientRxPackets[clientId] << "\t"
                              << addr.GetIpv4() << "\t"
                              << addr.GetPort() << std::endl;
}

// DASH Server Tx trace
void DashServerTxTrace(uint32_t serverId, Ptr<const Packet> packet)
{
    if (g_serverTxFiles.find(serverId) == g_serverTxFiles.end())
    {
        std::string filename = "DashServerTx_" + std::to_string(serverId) + ".txt";
        g_serverTxFiles[serverId].open(filename.c_str(), std::ios::out);
        g_serverTxFiles[serverId] << "# Time(s)\tPacketSize(bytes)\tTotalBytes\tTotalPackets" << std::endl;
        g_serverTxBytes[serverId] = 0;
        g_serverTxPackets[serverId] = 0;
    }
    
    g_serverTxBytes[serverId] += packet->GetSize();
    g_serverTxPackets[serverId]++;
    
    g_serverTxFiles[serverId] << Simulator::Now().GetSeconds() << "\t"
                              << packet->GetSize() << "\t"
                              << g_serverTxBytes[serverId] << "\t"
                              << g_serverTxPackets[serverId] << std::endl;
}

// Setup QUIC-specific traces
static void
SetupQuicTraces(uint32_t nodeId, std::string prefix)
{
    AsciiTraceHelper asciiTraceHelper;
    
    // Congestion Window trace
    std::ostringstream pathCW;
    pathCW << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/CongestionWindow";
    std::ostringstream fileCW;
    fileCW << prefix << "_QUIC_cwnd_" << nodeId << ".txt";
    Ptr<OutputStreamWrapper> streamCW = asciiTraceHelper.CreateFileStream(fileCW.str().c_str());
    *streamCW->GetStream() << "# Time(s)\tCwnd(bytes)" << std::endl;
    Config::ConnectWithoutContextFailSafe(pathCW.str().c_str(), MakeBoundCallback(&CwndChange, streamCW));
    
    // RTT trace
    std::ostringstream pathRTT;
    pathRTT << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/RTT";
    std::ostringstream fileRTT;
    fileRTT << prefix << "_QUIC_rtt_" << nodeId << ".txt";
    Ptr<OutputStreamWrapper> streamRTT = asciiTraceHelper.CreateFileStream(fileRTT.str().c_str());
    *streamRTT->GetStream() << "# Time(s)\tRTT(ms)" << std::endl;
    Config::ConnectWithoutContextFailSafe(pathRTT.str().c_str(), MakeBoundCallback(&RttChange, streamRTT));
    
    // Rx data trace
    std::ostringstream pathRx;
    pathRx << "/NodeList/" << nodeId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
    std::ostringstream fileRx;
    fileRx << prefix << "_QUIC_rx_data_" << nodeId << ".txt";
    Ptr<OutputStreamWrapper> streamRx = asciiTraceHelper.CreateFileStream(fileRx.str().c_str());
    *streamRx->GetStream() << "# Time(s)\tPacketSize(bytes)\tPacketNum" << std::endl;
    Config::ConnectWithoutContextFailSafe(pathRx.str().c_str(), MakeBoundCallback(&RxCallback, streamRx));
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    bool tracing = true;  // Enable tracing by default
    uint32_t users = 1;
    double target_dt = 35.0;
    double stopTime = 100.0;
    std::string linkRate = "5Mbps";
    std::string delay = "10ms";
    std::string algorithm = "ns3::FdashClient";
    uint32_t bufferSpace = 30000000;
    std::string window = "10s";
    std::string tracePrefix = "dash_quic";

    // Enable logging (optional)
    // LogComponentEnable("DashClient", LOG_LEVEL_INFO);
    // LogComponentEnable("DashServer", LOG_LEVEL_INFO);
    // LogComponentEnable("MpegPlayer", LOG_LEVEL_INFO);

    // Command line arguments
    CommandLine cmd;
    cmd.AddValue("tracing", "Flag to enable/disable tracing", tracing);
    cmd.AddValue("users", "The number of concurrent videos", users);
    cmd.AddValue("targetDt",
                 "The target time difference between receiving and playing a frame.",
                 target_dt);
    cmd.AddValue("stopTime", "The time when the clients will stop requesting segments", stopTime);
    cmd.AddValue("linkRate",
                 "The bitrate of the link connecting the clients to the server (e.g. 5Mbps)",
                 linkRate);
    cmd.AddValue("delay",
                 "The delay of the link connecting the clients to the server (e.g. 10ms)",
                 delay);
    cmd.AddValue("algorithms",
                 "The adaptation algorithms used. It can be a comma separated list of "
                 "protocols, such as 'ns3::FdashClient,ns3::OsmpClient'. "
                 "You may find the list of available algorithms in src/dash/model/algorithms",
                 algorithm);
    cmd.AddValue("bufferSpace",
                 "The space in bytes that is used for buffering the video",
                 bufferSpace);
    cmd.AddValue("window", "The window for measuring the average throughput (Time).", window);
    cmd.AddValue("tracePrefix", "Prefix for trace file names", tracePrefix);
    cmd.Parse(argc, argv);

    NS_LOG_INFO("Create nodes.");
    NodeContainer nodes;
    nodes.Create(2);

    NS_LOG_INFO("Create channels.");
    
    // Create the point-to-point link
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue(linkRate));
    pointToPoint.SetChannelAttribute("Delay", StringValue(delay));
    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    // Install QUIC stack on the nodes (instead of regular Internet stack)
    QuicHelper quicStack;
    
    // Configure QUIC socket buffer sizes
    Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(1 << 21)); // 2MB
    Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(1 << 21)); // 2MB
    Config::SetDefault("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue(1 << 21)); // 2MB
    Config::SetDefault("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue(1 << 21)); // 2MB
    
    quicStack.InstallQuic(nodes);

    // Assign IP addresses
    NS_LOG_INFO("Assign IP Addresses.");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

    NS_LOG_INFO("Create Applications.");

    // Parse algorithms for multiple users
    std::vector<std::string> algorithms;
    std::stringstream ss(algorithm);
    std::string proto;
    uint32_t protoNum = 0;
    while (std::getline(ss, proto, ',') && protoNum++ < users)
    {
        algorithms.push_back(proto);
    }

    uint16_t port = 80;
    std::vector<DashClientHelper> clients;
    std::vector<ApplicationContainer> clientApps;

    // Install DASH clients using QUIC
    for (uint32_t user = 0; user < users; user++)
    {
        // Use QuicSocketFactory instead of TcpSocketFactory
        DashClientHelper client("ns3::QuicSocketFactory",
                                InetSocketAddress(i.GetAddress(1), port),
                                algorithms[user % protoNum]);
        
        client.SetAttribute("VideoId", UintegerValue(user + 1)); // VideoId should be positive
        client.SetAttribute("TargetDt", TimeValue(Seconds(target_dt)));
        client.SetAttribute("window", TimeValue(Time(window)));
        client.SetAttribute("bufferSpace", UintegerValue(bufferSpace));

        ApplicationContainer clientApp = client.Install(nodes.Get(0));
        clientApp.Start(Seconds(0.25));
        clientApp.Stop(Seconds(stopTime));

        clients.push_back(client);
        clientApps.push_back(clientApp);
    }

    // Install DASH server using QUIC
    DashServerHelper server("ns3::QuicSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = server.Install(nodes.Get(1));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(stopTime + 5.0));

    // Run simulation first to allow apps to initialize
    NS_LOG_INFO("Run Simulation.");
    Simulator::Stop(Seconds(stopTime + 10.0));
    
    // Set up tracing if enabled (schedule after apps start)
    if (tracing)
    {
        // PCAP tracing
        AsciiTraceHelper ascii;
        pointToPoint.EnableAsciiAll(ascii.CreateFileStream(tracePrefix + ".tr"));
        pointToPoint.EnablePcapAll(tracePrefix, false);
        
        // Setup QUIC-specific traces after a delay to ensure sockets are created
        Simulator::Schedule(Seconds(1.0), &SetupQuicTraces, 0, tracePrefix);  // Client node
        Simulator::Schedule(Seconds(1.0), &SetupQuicTraces, 1, tracePrefix);  // Server node
        
        NS_LOG_INFO("Tracing enabled. Output files:");
        NS_LOG_INFO("  - " << tracePrefix << ".tr (ASCII trace)");
        NS_LOG_INFO("  - " << tracePrefix << "-*.pcap (PCAP files)");
        NS_LOG_INFO("  - " << tracePrefix << "_QUIC_cwnd_*.txt (Congestion window)");
        NS_LOG_INFO("  - " << tracePrefix << "_QUIC_rtt_*.txt (RTT)");
        NS_LOG_INFO("  - " << tracePrefix << "_QUIC_rx_data_*.txt (QUIC Rx data)");
    }

    // Print simulation info
    std::cout << "\n========== DASH over QUIC Simulation ==========" << std::endl;
    std::cout << "Video Parameters:" << std::endl;
    std::cout << "  - Frames per segment: 100 frames" << std::endl;
    std::cout << "  - Frame rate: 50 fps (20ms between frames)" << std::endl;
    std::cout << "  - Segment duration: 2 seconds" << std::endl;
    std::cout << "  - Estimated segments for " << stopTime << "s: ~" << (int)(stopTime/2) << " segments" << std::endl;
    std::cout << "  - Estimated total frames: ~" << (int)(stopTime * 50) << " frames" << std::endl;
    std::cout << "\nNetwork Parameters:" << std::endl;
    std::cout << "  - Link rate: " << linkRate << std::endl;
    std::cout << "  - Link delay: " << delay << std::endl;
    std::cout << "  - Number of users: " << users << std::endl;
    std::cout << "  - Algorithm: " << algorithm << std::endl;
    std::cout << "===============================================\n" << std::endl;

    Simulator::Run();
    
    // Close trace files
    for (auto& pair : g_clientRxFiles)
    {
        if (pair.second.is_open())
        {
            pair.second.close();
        }
    }
    for (auto& pair : g_serverTxFiles)
    {
        if (pair.second.is_open())
        {
            pair.second.close();
        }
    }
    
    // Print statistics
    std::cout << "\n========== Simulation Results ==========" << std::endl;
    NS_LOG_INFO("Simulation complete.");
    uint32_t k;
    for (k = 0; k < users; k++)
    {
        Ptr<DashClient> app = DynamicCast<DashClient>(clientApps[k].Get(0));
        std::cout << "\n" << algorithms[k % protoNum] << "-Node: " << k << std::endl;
        app->GetStats();
        
        if (g_clientRxBytes.find(k) != g_clientRxBytes.end())
        {
            std::cout << "  Client " << k << " total received: " << g_clientRxBytes[k] 
                     << " bytes in " << g_clientRxPackets[k] << " packets" << std::endl;
            std::cout << "  Average throughput: " 
                     << (g_clientRxBytes[k] * 8.0) / (stopTime * 1000000.0) 
                     << " Mbps" << std::endl;
        }
    }
    
    if (g_serverTxBytes.find(0) != g_serverTxBytes.end())
    {
        std::cout << "\nServer total transmitted: " << g_serverTxBytes[0] 
                 << " bytes in " << g_serverTxPackets[0] << " packets" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;
    
    Simulator::Destroy();
    NS_LOG_INFO("Done.");

    return 0;
}
