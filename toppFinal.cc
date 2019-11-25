#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("DashApplication");

//================================================================
// SERVER APPLICATION
//================================================================

class DashServerApp: public Application
{
public:
    DashServerApp();
    virtual ~DashServerApp();
    void Setup(Address address, uint32_t packetSize);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void RxCallback(Ptr<Socket> socket);
    void TxCallback(Ptr<Socket> socket, uint32_t txSpace);
    bool ConnectionCallback(Ptr<Socket> s, const Address &ad);
    void AcceptCallback(Ptr<Socket> s, const Address &ad);
    void SendData();

    bool m_connected;
    Ptr<Socket> m_socket;
    Ptr<Socket> m_peer_socket;

    Address ads;
    Address m_peer_address;
    uint32_t m_remainingData;
    EventId m_sendEvent;
    uint32_t m_packetSize;
    uint32_t m_packetCount;
    vector<uint32_t> m_bitrate_array;
    vector<uint32_t> m_sendTime_array;
    Time m_receiveTime;
    uint32_t m_cumulativeSize;

    uint32_t m_receive_packet;
    uint32_t m_receive_rate;
};

DashServerApp::DashServerApp() :
    m_connected(false), m_socket(0), m_peer_socket(0), 
    ads(), m_peer_address(), m_remainingData(0), 
    m_sendEvent(), m_packetSize(0), m_packetCount(0), m_receiveTime(), m_cumulativeSize(0),m_receive_packet(0), m_receive_rate(0)
{

}

DashServerApp::~DashServerApp()
{
    m_socket = 0;
}

void DashServerApp::Setup(Address address, uint32_t packetSize)
{
    ads = address;
    m_packetSize = packetSize;

    //bitrate profile of the content
    m_bitrate_array.push_back(50);
    m_bitrate_array.push_back(100);
    m_bitrate_array.push_back(150);
    m_bitrate_array.push_back(200);
    m_bitrate_array.push_back(250);
    m_bitrate_array.push_back(300);
    m_bitrate_array.push_back(350);
    m_bitrate_array.push_back(400);
    m_bitrate_array.push_back(450);
    m_bitrate_array.push_back(500);

    //probing packet sending timing
    m_sendTime_array.push_back(5);
    m_sendTime_array.push_back(4);
    m_sendTime_array.push_back(3);
    m_sendTime_array.push_back(2);
    m_sendTime_array.push_back(1);
}

void DashServerApp::StartApplication()
{
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
    // if (m_socket->GetSocketType () != Socket::NS3_SOCK_STREAM &&
    //   m_socket->GetSocketType () != Socket::NS3_SOCK_SEQPACKET)
    // {
    //   NS_FATAL_ERROR ("Using BulkSend with an incompatible socket type. "
    //                   "BulkSend requires SOCK_STREAM or SOCK_SEQPACKET. "
    //                   "In other words, use TCP instead of UDP.");
    // }

    m_socket->Bind(ads);
    m_socket->Listen();
    m_socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback, this));
    m_socket->SetSendCallback(MakeCallback(&DashServerApp::TxCallback, this));
}

void DashServerApp::StopApplication()
{
    m_connected = false;
    if (m_socket)
        m_socket->Close();
    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }
}

void DashServerApp::RxCallback(Ptr<Socket> socket)
{
    Ptr<Packet> pckt = socket->RecvFrom(ads);
    
    uint32_t data = 0;
    pckt->CopyData((uint8_t *) &data, 4);
    m_remainingData = data;

    NS_LOG_UNCOND("Server Time : " << Simulator::Now().GetMilliSeconds());

    while(m_remainingData>0){
        uint32_t toSend = min((uint32_t)1500, m_remainingData);
        Ptr<Packet> packet = Create<Packet>(toSend);

        int actual = socket->SendTo(packet, 0, ads);

        if(actual > 0){
            m_remainingData -= toSend;
        }

        if((unsigned)actual != toSend){
            break;
        }
    }
}

void DashServerApp::TxCallback(Ptr<Socket> socket, uint32_t txSpace)
{
    Simulator::ScheduleNow (&DashServerApp::SendData, this);
}

void DashServerApp::SendData()
{
    
}

//================================================================
// CLIENT APPLICATION
//================================================================

class DashClientApp: public Application
{
public:

    DashClientApp();
    virtual ~DashClientApp();

    void Setup(Address address);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    // Request
    void SendRequest(void);
    void RequestNextChunk(void);
    void RxCallback(Ptr<Socket> socket);

    // Rate Adaptation Algorithm
    void Proposed(void);

    Ptr<Socket> m_socket;
    Address m_peer;
    vector<uint32_t> m_bitrate_array;
    vector<uint32_t> m_sendTime_array;
    bool m_running;
    uint32_t m_cumulativeSize;

    // Proposed
    uint32_t num_Send_Packet;
    uint32_t m_packet_rate;
    uint32_t m_next_rate;
    uint32_t m_pair_count;
    uint32_t m_packetCount;

    uint32_t m_received_rate;
    uint32_t m_received_gap;
    uint32_t m_received_pair;

};

DashClientApp::DashClientApp() :
    m_socket(0), m_peer(), m_running(false),m_cumulativeSize(0),
    num_Send_Packet(0), m_packet_rate(0), m_next_rate(0), m_pair_count(0), m_packetCount(0),
    m_received_rate(0), m_received_gap(0), m_received_pair(0)
{

}

DashClientApp::~DashClientApp()
{
    m_socket = 0;
}

void DashClientApp::Setup(Address address)
{
    m_peer = address;

    //bitrate profile of the content
    m_bitrate_array.push_back(1000000);
    m_bitrate_array.push_back(2000000);
    m_bitrate_array.push_back(3000000);
    m_bitrate_array.push_back(4000000);
    m_bitrate_array.push_back(5000000);
    m_bitrate_array.push_back(6000000);
    m_bitrate_array.push_back(7000000);
    m_bitrate_array.push_back(8000000);
    m_bitrate_array.push_back(9000000);

    //probing packet sending timing
    m_sendTime_array.push_back(4);
    m_sendTime_array.push_back(3);
    m_sendTime_array.push_back(2);
    m_sendTime_array.push_back(1);
}

void DashClientApp::StartApplication(void)
{
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    m_running = true;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    m_socket->SetRecvCallback(MakeCallback(&DashClientApp::RxCallback, this));

    Proposed();
}

void DashClientApp::RequestNextChunk(void)
{
    Proposed();
}

void DashClientApp::RxCallback(Ptr<Socket> socket){
    Ptr<Packet> packet;

    while((packet = socket->Recv())){
        if(packet->GetSize() == 0){
            break;
        }
        // NS_LOG_UNCOND("Time in client: " << Simulator::Now().GetMilliSeconds());

        m_cumulativeSize += packet->GetSize();
        m_packetCount++;

        if(m_cumulativeSize >= 0){
            //algorithm
            // NS_LOG_UNCOND("Time in client: " << Simulator::Now().GetMilliSeconds());
        }
    }
    NS_LOG_UNCOND("Time in client : " << Simulator::Now().GetMilliSeconds() << " packet count : " << m_packetCount);

}

void DashClientApp::Proposed(void)
{
    // Propose idea code writing!!
    m_next_rate = m_bitrate_array[m_packet_rate];

    // Time scheduling
    Time tNext;
    if(num_Send_Packet >= m_sendTime_array.size()){
        num_Send_Packet = 0;
        m_pair_count++;
        
        if(m_pair_count>1){
            m_packet_rate++;
            m_pair_count = 0;
            tNext=Seconds(m_sendTime_array[num_Send_Packet]);
        }else{
            
        }
        if(m_packet_rate >= m_bitrate_array.size()){
            m_packet_rate = 0;
        }
    }
    Simulator::Schedule(tNext, &DashClientApp::SendRequest, this);
}

void DashClientApp::SendRequest(void)
{
    uint32_t bytesReq = m_next_rate/8;
    // while(){
        Ptr<Packet> packet = Create<Packet>((uint8_t *) &bytesReq, 4);

        m_socket->Send(packet);
        NS_LOG_UNCOND("Time : "<< Simulator::Now().GetMilliSeconds() << " Send Packet Size : " << m_next_rate);
    // }

    Simulator::ScheduleNow(&DashClientApp::RequestNextChunk, this);
}

void DashClientApp::StopApplication(void)
{
    m_running = false;

    if (m_socket)
    {
        m_socket->Close();
    }
}

//=================================================================
// SIMULATION
//================================================================

int main(int argc, char *argv[])
{

    LogComponentEnable("DashApplication", LOG_LEVEL_ALL);
    // std::string animFile = "dash-animation.xml" ;  // Name of file for animation output

    NodeContainer nodes;
    nodes.Create(12);

    NodeContainer n01 = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer n21 = NodeContainer(nodes.Get(2), nodes.Get(1));
    NodeContainer n13 = NodeContainer(nodes.Get(1), nodes.Get(3));
    NodeContainer n43 = NodeContainer(nodes.Get(4), nodes.Get(3));
    NodeContainer n53 = NodeContainer(nodes.Get(5), nodes.Get(3));
    NodeContainer n36 = NodeContainer(nodes.Get(3), nodes.Get(6));
    NodeContainer n76 = NodeContainer(nodes.Get(7), nodes.Get(6));
    NodeContainer n86 = NodeContainer(nodes.Get(8), nodes.Get(6));
    NodeContainer n69 = NodeContainer(nodes.Get(6), nodes.Get(9));
    NodeContainer n109 = NodeContainer(nodes.Get(10), nodes.Get(9));
    NodeContainer n119 = NodeContainer(nodes.Get(11), nodes.Get(9));

    InternetStackHelper internet;
    internet.Install(nodes);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));

    NetDeviceContainer d01 = p2p.Install(n01);
    NetDeviceContainer d21 = p2p.Install(n21);
    NetDeviceContainer d43 = p2p.Install(n43);
    NetDeviceContainer d53 = p2p.Install(n53);
    NetDeviceContainer d76 = p2p.Install(n76);
    NetDeviceContainer d86 = p2p.Install(n86);
    NetDeviceContainer d109 = p2p.Install(n109);
    NetDeviceContainer d119 = p2p.Install(n119);

    PointToPointHelper bottle;
    bottle.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
    bottle.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer d13 = bottle.Install(n13);
    NetDeviceContainer d36 = bottle.Install(n36);
    NetDeviceContainer d69 = bottle.Install(n69);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i01 = address.Assign(d01);
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i21 = address.Assign(d21);
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i13 = address.Assign(d13);
    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i43 = address.Assign(d43);
    address.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer i53 = address.Assign(d53);
    address.SetBase("10.1.6.0", "255.255.255.0");
    Ipv4InterfaceContainer i36 = address.Assign(d36);
    address.SetBase("10.1.7.0", "255.255.255.0");
    Ipv4InterfaceContainer i76 = address.Assign(d76);
    address.SetBase("10.1.8.0", "255.255.255.0");
    Ipv4InterfaceContainer i86 = address.Assign(d86);
    address.SetBase("10.1.9.0", "255.255.255.0");
    Ipv4InterfaceContainer i69 = address.Assign(d69);
    address.SetBase("10.1.10.0", "255.255.255.0");
    Ipv4InterfaceContainer i109 = address.Assign(d109);
    address.SetBase("10.1.11.0", "255.255.255.0");
    Ipv4InterfaceContainer i119 = address.Assign(d119);

    uint16_t serverPort = 8080;

    // Cross Traffic CBR
    // Scenario 2 (Drastic Bandwidth Decrease -> Increase)
    OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (i21.GetAddress(0), serverPort));
    crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=200]"));
    crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("1000kb/s")));
    crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
    ApplicationContainer srcApp1 = crossTrafficSrc1.Install(nodes.Get(2));
    srcApp1.Start(Seconds(0.0));
    srcApp1.Stop(Seconds(50.0));
    PacketSinkHelper cbrSink1("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    ApplicationContainer cbrApp = cbrSink1.Install(nodes.Get(4));
    cbrApp.Start(Seconds(0.0));
    cbrApp.Stop(Seconds(50.0));

    OnOffHelper crossTrafficSrc2("ns3::UdpSocketFactory", InetSocketAddress (i53.GetAddress(0), serverPort));
    crossTrafficSrc2.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=200]"));
    crossTrafficSrc2.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    crossTrafficSrc2.SetAttribute("DataRate", DataRateValue (DataRate("1000kb/s")));
    crossTrafficSrc2.SetAttribute("PacketSize", UintegerValue (512));
    ApplicationContainer srcApp2 = crossTrafficSrc2.Install(nodes.Get(5));
    srcApp2.Start(Seconds(0.0));
    srcApp2.Stop(Seconds(50.0));
    PacketSinkHelper cbrSink2("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    ApplicationContainer cbrApp1 = cbrSink2.Install(nodes.Get(7));
    cbrApp1.Start(Seconds(0.0));
    cbrApp1.Stop(Seconds(50.0));

    OnOffHelper crossTrafficSrc3("ns3::UdpSocketFactory", InetSocketAddress (i86.GetAddress(0), serverPort));
    crossTrafficSrc3.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=200]"));
    crossTrafficSrc3.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    crossTrafficSrc3.SetAttribute("DataRate", DataRateValue (DataRate("1000kb/s")));
    crossTrafficSrc3.SetAttribute("PacketSize", UintegerValue (512));
    ApplicationContainer srcApp3 = crossTrafficSrc3.Install(nodes.Get(8));
    srcApp3.Start(Seconds(0.0));
    srcApp3.Stop(Seconds(50.0));
    PacketSinkHelper cbrSink3("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    ApplicationContainer cbrApp2 = cbrSink3.Install(nodes.Get(11));
    cbrApp2.Start(Seconds(0.0));
    cbrApp2.Stop(Seconds(50.0));

    // DASH server
    Address bindAddress1(InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    Ptr<DashServerApp> serverApp1 = CreateObject<DashServerApp>();
    serverApp1->Setup(bindAddress1, 512);
    nodes.Get(10)->AddApplication(serverApp1);
    serverApp1->SetStartTime(Seconds(0.0));
    serverApp1->SetStopTime(Seconds(50.0));

    // DASH client
    Address serverAddress1(
        InetSocketAddress(i109.GetAddress(0), serverPort));
    Ptr<DashClientApp> clientApp1 = CreateObject<DashClientApp>();
    clientApp1->Setup(serverAddress1);
    nodes.Get(0)->AddApplication(clientApp1);
    clientApp1->SetStartTime(Seconds(0.0));
    clientApp1->SetStopTime(Seconds(50.0));

    // // NetAnim
    // // Set the bounding box for animation
    // dB.BoundingBox (1, 1, 100, 100);
 
    // // Create the animation object and configure for specified output
    // AnimationInterface anim (animFile);
    // anim.EnablePacketMetadata (); // Optional
    // anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (5)); // Optional 




    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(50.0));

    Simulator::Run();
    Simulator::Destroy();

    return 0;
}