#include "ns3/log.h"
#include "ns3/netanim-module.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/olsr-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MANET_EXPERIMENT");

static inline void PrintPacketInfo(Ptr<Socket> socket, Ptr<Packet> packet, Address senderAddress);


class Experiment {
public:
    Experiment (int nNodes);
    void Run();
private:
    NodeContainer c;
    NetDeviceContainer devices;
    Ipv4InterfaceContainer interfaces;
    int nNodes;
    int nSinks;
    int totalBytes;
    int totalPackets;

    void SetPosition(Ptr<Node> node, Vector position);
    Vector GetPosition(Ptr<Node> node);
    void DisplayNodesPosition();
    void CheckTransferredData();

    void ReceivePacket(Ptr<Socket> socket);
    Ptr<Socket> SetupPacketReceive(Ipv4Address addr, Ptr<Node> node);
};

void Experiment::Run() {
    AnimationInterface anim("animation.xml");

    for (uint32_t i = 0; i < this->c.GetN(); ++i) {
        anim.UpdateNodeSize(i, 15, 15);
    }

    // Simulator::Schedule(Seconds(1.0), &Experiment::DisplayNodesPosition, this);

    OnOffHelper onoff1("ns3::UdpSocketFactory", Address());
    onoff1.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    onoff1.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));

    for (int i = 0; i < this->nSinks; i++) {
        Ptr<Socket> sink = SetupPacketReceive(this->interfaces.GetAddress(i), this->c.Get(i));
        
        AddressValue remoteAdress(InetSocketAddress(this->interfaces.GetAddress(i), 9));
        onoff1.SetAttribute("Remote", remoteAdress);

        Ptr<UniformRandomVariable> var = CreateObject<UniformRandomVariable>();
        ApplicationContainer temp = onoff1.Install(this->c.Get(i + this->nSinks));
        temp.Start(Seconds(100.0));
        temp.Stop(Seconds(200.0));
    }

    CheckTransferredData();

    Simulator::Stop(Seconds(200.0));
    Simulator::Run();
    Simulator::Destroy();
}

Experiment::Experiment(int nNodes) {
    this->nNodes = nNodes;
    this->nSinks = 10;
    this->totalBytes = 0;
    this->totalPackets = 0;

    this->c.Create(this->nNodes);

    // Setting MobilityModel
    MobilityHelper mobility;
    ObjectFactory pos;

    pos.SetTypeId("ns3::RandomRectanglePositionAllocator");
    pos.Set("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    pos.Set("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    Ptr<PositionAllocator> taPositionAlloc = pos.Create()->GetObject<PositionAllocator>();

    mobility.SetPositionAllocator(taPositionAlloc);
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
        "Speed", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=20]"),
        "Pause", StringValue("ns3::ConstantRandomVariable[Constant=0]"),
        "PositionAllocator", PointerValue(taPositionAlloc));
    mobility.Install(this->c);

    // Global configs
    Config::SetDefault("ns3::OnOffApplication::PacketSize", StringValue("64"));
    Config::SetDefault("ns3::OnOffApplication::DataRate", StringValue("2048bps"));
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue("DsssRate11Mbps"));

    // Wifi and Channel
    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel;

    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
    wifiPhy.SetChannel(wifiChannel.Create());

    // MAC address and disable rate control
    WifiMacHelper wifiMac;

    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
        "DataMode", StringValue("DsssRate11Mbps"),
        "ControlMode", StringValue("DsssRate11Mbps"));
    wifiPhy.Set("TxPowerStart", DoubleValue(0.75));
    wifiPhy.Set("TxPowerEnd", DoubleValue(0.75));

    wifiMac.SetType("ns3::AdhocWifiMac");
    this->devices = wifi.Install(wifiPhy, wifiMac, this->c);

    // Routing protocol
    OlsrHelper olsr;
    Ipv4ListRoutingHelper list;
    InternetStackHelper internet;

    list.Add(olsr, 100);
    internet.Install(this->c);

    // Address assigning
    Ipv4AddressHelper ipAddresses;
    Ipv4InterfaceContainer ipInterface;

    ipAddresses.SetBase("10.0.0.0", "255.255.255.0");
    this->interfaces = ipAddresses.Assign(this->devices);
}

void Experiment::CheckTransferredData() {
    std::cout << "Time [" << Simulator::Now().GetSeconds() << "] Packets: " << this->totalPackets <<
        " Bytes: " << totalBytes << std::endl;
    
    Simulator::Schedule(Seconds(1.0), &Experiment::CheckTransferredData, this);
}

void Experiment::SetPosition(Ptr<Node> node, Vector position) {
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    mobility->SetPosition(position);
}

Vector Experiment::GetPosition(Ptr<Node> node) {
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    return mobility->GetPosition();
}

void Experiment::DisplayNodesPosition() {
    std::cout << "Displaying nodes position at " << Simulator::Now().GetSeconds()  << "s" << std::endl;
    for (int i = 0; i < this->nNodes; i++) {
        std::cout << "[" << i << "] " << GetPosition(c.Get(i)) << std::endl; 
    }
}

void Experiment::ReceivePacket(Ptr<Socket> socket) {
    Ptr<Packet> packet;
    Address senderAddress;

    while ((packet = socket->RecvFrom(senderAddress))) {
        this->totalBytes += packet->GetSize();
        this->totalPackets += 1;
        PrintPacketInfo(socket, packet, senderAddress);
    }
}

static inline void PrintPacketInfo(Ptr<Socket> socket, Ptr<Packet> packet, Address senderAddress) {
    std::cout << Simulator::Now().GetSeconds() << " " << socket->GetNode()->GetId();

    if (InetSocketAddress::IsMatchingType(senderAddress)) {
        InetSocketAddress addr = InetSocketAddress::ConvertFrom(senderAddress);
        std::cout << " received one packet from " << addr.GetIpv4() << std::endl;
    }
    else {
        std::cout << " received one packet" << std::endl;
    }
}

Ptr<Socket> Experiment::SetupPacketReceive(Ipv4Address addr, Ptr<Node> node) {
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> sink = Socket::CreateSocket(node, tid);
    InetSocketAddress local = InetSocketAddress(addr, 9);
    sink->Bind(local);
    sink->SetRecvCallback(MakeCallback(&Experiment::ReceivePacket, this));
    
    return sink;
}


int main(int argc, char *argv[]) {
    int nNodes = 20;

    CommandLine cmd;
    cmd.AddValue ("nNodes", "number of nodes", nNodes);
    cmd.Parse(argc, argv);

    Experiment exp = Experiment(nNodes);
    exp.Run();
    
    return 0;
}
