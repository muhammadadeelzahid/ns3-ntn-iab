
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
#include "ns3/tcp-header.h"
#include "ns3/tcp-socket-base.h"

#include <fstream>
#include <map>

using namespace ns3;

std::ofstream tcpCombinedLog;
std::map<uint32_t, double> txTimestamps;

void TcpTxTrace(Ptr<const Packet> packet, const TcpHeader &header, Ptr<const TcpSocketBase>)
{
    uint32_t seq = header.GetSequenceNumber().GetValue();
    double now = Simulator::Now().GetSeconds();
    txTimestamps[seq] = now;
    tcpCombinedLog << now << "s - Packet SENT: Seq=" << seq
                   << ", Size=" << packet->GetSize() << " bytes";
}

void TcpRxTrace(Ptr<const Packet> packet, const TcpHeader &header, Ptr<const TcpSocketBase>)
{
    uint32_t seq = header.GetSequenceNumber().GetValue();
    double now = Simulator::Now().GetSeconds();
    double sentTime = txTimestamps.count(seq) ? txTimestamps[seq] : -1.0;
    tcpCombinedLog << now << "s - Packet RECEIVED: Seq=" << seq
                   << ", Size=" << packet->GetSize() << " bytes"
                   << ", SentTime=" << (sentTime >= 0 ? std::to_string(sentTime) + "s" : "Unknown") << "\n";
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(1);

    LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("PacketSink", LOG_LEVEL_INFO);

    Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
    Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
    mmwaveHelper->SetEpcHelper(epcHelper);
    mmwaveHelper->Initialize();

    NodeContainer enbNodes, ueNodes;
    enbNodes.Create(1);
    ueNodes.Create(5);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(1000.0, 1000.0, 30.0));
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        posAlloc->Add(Vector(1000.0 + 20.0 * (i + 1), 1000.0, 1.7));

    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(enbNodes);
    mobility.Install(ueNodes);

    NetDeviceContainer enbDevs = mmwaveHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = mmwaveHelper->InstallUeDevice(ueNodes);

    InternetStackHelper internet;
    internet.Install(ueNodes);

    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(ueDevs);
    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        Ptr<NetDevice> ueDevice = ueDevs.Get(i);
        Ptr<MmWaveUeNetDevice> mmwaveUeDevice = DynamicCast<MmWaveUeNetDevice>(ueDevice);
        if (!mmwaveUeDevice)
        {
            // NS_LOG_ERROR("Failed to cast NetDevice to MmWaveUeNetDevice for UE " << i);
            return 1;
        }
        uint64_t imsi = mmwaveUeDevice->GetImsi();
        Ptr<EpcTft> tft = Create<EpcTft>();
        EpsBearer bearer(EpsBearer::GBR_CONV_VOICE);
        epcHelper->AddUe(ueDevice, imsi);
        epcHelper->ActivateEpsBearer(ueDevice, imsi, tft, bearer);

        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(
            ueNodes.Get(i)->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));

    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);

    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    mmwaveHelper->AttachToClosestEnb(ueDevs, enbDevs);

    tcpCombinedLog.open("TcpSentReceivedCombined.txt");

    Config::ConnectWithoutContextFailSafe("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/Tx", MakeCallback(&TcpTxTrace));
    Config::ConnectWithoutContextFailSafe("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/Rx", MakeCallback(&TcpRxTrace));

    uint16_t basePort = 50000;

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Address sinkLocalAddress(InetSocketAddress(Ipv4Address::GetAny(), basePort + i));
        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkLocalAddress);
        ApplicationContainer serverApp = sinkHelper.Install(ueNodes.Get(i));
        serverApp.Start(Seconds(0.5));
        serverApp.Stop(Seconds(2.0));

        Address destAddress(InetSocketAddress(ueIpIfaces.GetAddress(i), basePort + i));
        OnOffHelper clientHelper("ns3::TcpSocketFactory", destAddress);
        clientHelper.SetAttribute("DataRate", StringValue("100Mb/s"));
        clientHelper.SetAttribute("PacketSize", UintegerValue(1400));
        clientHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        clientHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ApplicationContainer clientApp = clientHelper.Install(remoteHost);
        clientApp.Start(Seconds(1.0));
        clientApp.Stop(Seconds(2.0));
    }

    mmwaveHelper->EnableTraces();
    Simulator::Stop(Seconds(3.0));
    Simulator::Run();
    Simulator::Destroy();

    tcpCombinedLog.close();
    return 0;
}
