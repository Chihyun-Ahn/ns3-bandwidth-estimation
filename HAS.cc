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
};

DashServerApp::DashServerApp() :
    m_connected(false), m_socket(0), m_peer_socket(0), 
    ads(), m_peer_address(), m_remainingData(0), 
    m_sendEvent(), m_packetSize(0), m_packetCount(0)
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
}

void DashServerApp::StartApplication()
{
    m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());

    // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
    if (m_socket->GetSocketType () != Socket::NS3_SOCK_STREAM &&
      m_socket->GetSocketType () != Socket::NS3_SOCK_SEQPACKET)
    {
      NS_FATAL_ERROR ("Using BulkSend with an incompatible socket type. "
                      "BulkSend requires SOCK_STREAM or SOCK_SEQPACKET. "
                      "In other words, use TCP instead of UDP.");
    }

    m_socket->Bind(ads);
    m_socket->Listen();
    m_socket->SetAcceptCallback(
        MakeCallback(&DashServerApp::ConnectionCallback, this),
        MakeCallback(&DashServerApp::AcceptCallback, this));
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

bool DashServerApp::ConnectionCallback(Ptr<Socket> socket, const Address &ads)
{
    NS_LOG_UNCOND("Server: connection callback ");

    m_connected = true;
    return true;
}

void DashServerApp::AcceptCallback(Ptr<Socket> socket, const Address &ads)
{
    NS_LOG_UNCOND("Server: accept callback ");

    m_peer_address = ads;
    m_peer_socket = socket;

    socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback, this));
    socket->SetSendCallback(MakeCallback(&DashServerApp::TxCallback, this));
}

void DashServerApp::RxCallback(Ptr<Socket> socket)
{
    Address ads;
    Ptr<Packet> pckt = socket->RecvFrom(ads);
    
    if (ads == m_peer_address)
    {
        uint32_t data = 0;
        pckt->CopyData((uint8_t *) &data, 4);

        m_remainingData = data;
        m_packetCount = 0;

        SendData();
    }
}

void DashServerApp::TxCallback(Ptr<Socket> socket, uint32_t txSpace)
{
    if (m_connected)
        Simulator::ScheduleNow (&DashServerApp::SendData, this);
}

void DashServerApp::SendData()
{
    while (m_remainingData > 0)
    {
        // Time to send more
        uint32_t toSend = min (m_packetSize, m_remainingData);
        Ptr<Packet> packet = Create<Packet> (toSend);

        int actual = m_peer_socket->Send (packet);

        if (actual > 0)
        {
            m_remainingData -= toSend;
            m_packetCount++;
        }

        if ((unsigned)actual != toSend)
        {
            break;
        }
    }
}

//================================================================
// CLIENT APPLICATION
//================================================================

class DashClientApp: public Application
{
public:

    DashClientApp();
    virtual ~DashClientApp();
    enum
    {
        MAX_BUFFER_SIZE = 30000
    }; // 30 seconds

    void Setup(Address address, uint32_t chunkSize, uint32_t numChunks, string algorithm);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    // Request
    void SendRequest(void);
    void RequestNextChunk(void);
    void GetStatistics(void);
    void RxCallback(Ptr<Socket> socket);

    // Buffer Model
    void ClientBufferModel(void);
    void GetBufferState(void);
    void RxDrop (Ptr<const Packet> p);

    // Rate Adaptation Algorithm
    void DASH(void);
    void Conventional(void);
    void BBA(void);
    void Proposed(void);

    // Another
    uint32_t GetIndexByBitrate(uint32_t bitrate);

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_chunkSize;
    uint32_t m_numChunks;
    uint32_t m_chunkCount;
    int32_t m_bufferSize;
    uint32_t m_bufferPercent;
    uint32_t m_bpsAvg;
    uint32_t m_bpsLastChunk;
    vector<uint32_t> m_bitrate_array;
    EventId m_fetchEvent;
    EventId m_statisticsEvent;
    bool m_running;
    uint32_t m_comulativeSize;
    uint32_t m_lastRequestedSize;
    Time m_requestTime;
    uint32_t m_sessionData;
    uint32_t m_sessionTime;

    EventId m_bufferEvent;
    EventId m_bufferStateEvent;
    uint32_t m_downloadDuration;
    double m_throughput;
    uint32_t m_prevBitrate;
    uint32_t m_nextBitrate;
    uint32_t m_algorithm;
    uint32_t m_numOfSwitching;

    // Proposed

};

DashClientApp::DashClientApp() :
    m_socket(0), m_peer(), m_chunkSize(0), m_numChunks(0), m_chunkCount(0),
    m_bufferSize(0), m_bufferPercent(0), m_bpsAvg(0), m_bpsLastChunk(0),
    m_fetchEvent(), m_statisticsEvent(), m_running(false),
    m_comulativeSize(0), m_lastRequestedSize(0), m_requestTime(), m_sessionData(0), m_sessionTime(0),
    m_bufferEvent(), m_bufferStateEvent(), m_downloadDuration(0), m_throughput(0.0), 
    m_prevBitrate(0), m_nextBitrate(0), m_algorithm(0), m_numOfSwitching(0)
{

}

DashClientApp::~DashClientApp()
{
    m_socket = 0;
}

void DashClientApp::Setup(Address address, uint32_t chunkSize,
                          uint32_t numChunks, string algorithm)
{
    m_peer = address;
    m_chunkSize = chunkSize;
    m_numChunks = numChunks;

    //bitrate profile of the content
    m_bitrate_array.push_back(700000);
    m_bitrate_array.push_back(1400000);
    m_bitrate_array.push_back(2100000);
    m_bitrate_array.push_back(2800000);
    m_bitrate_array.push_back(3500000);
    m_bitrate_array.push_back(4200000);

    if (algorithm.compare("DASH") == 0)
        m_algorithm = 0;
    else if (algorithm.compare("Conventional") == 0)
        m_algorithm = 1;
    else if (algorithm.compare("BBA") == 0)
        m_algorithm = 2;
    else if (algorithm.compare("Proposed") == 0)
        m_algorithm = 3;
    else {
        cout << "Unknown algorithm" << endl;
        exit(1);
    }
}

void DashClientApp::RxDrop(Ptr<const Packet> p)
{
    NS_LOG_UNCOND ("RxDrop at " << Simulator::Now ().GetSeconds ());
}

void DashClientApp::StartApplication(void)
{
    m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_socket->TraceConnectWithoutContext("Drop",
                                         MakeCallback (&DashClientApp::RxDrop, this));

    m_running = true;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    m_socket->SetRecvCallback(MakeCallback(&DashClientApp::RxCallback, this));

    SendRequest();

    Time tNext("1s");
    m_bufferStateEvent = Simulator::Schedule(tNext, &DashClientApp::GetBufferState, this);
}

void DashClientApp::RxCallback(Ptr<Socket> socket)
{
    Ptr<Packet> packet;

    while ((packet = socket->Recv())) {
        if (packet->GetSize() == 0) {   // EOF
            break;
        }

        // For calculate throughput
        m_comulativeSize += packet->GetSize();

        if (m_comulativeSize >= m_lastRequestedSize)
        {
            // Received the complete chunk
            // Update the buffer size and initiate the next request

            m_chunkCount++;
            
            // Estimating
            m_downloadDuration = Simulator::Now().GetMilliSeconds() - m_requestTime.GetMilliSeconds();
            m_bpsLastChunk = (m_comulativeSize * 8) / (m_downloadDuration / 1000.0);

            // Update buffer
            m_bufferSize += m_chunkSize * 1000;
            m_bufferPercent = (uint32_t) (m_bufferSize * 100) / MAX_BUFFER_SIZE;

            // Scheduling
            Simulator::ScheduleNow(&DashClientApp::RequestNextChunk, this);

            // Start BufferModel
            if (m_chunkCount == 1) {
                Time tNext("100ms");
                m_bufferEvent = Simulator::Schedule(tNext, &DashClientApp::ClientBufferModel, this);
            }

            // Monitoring
            m_statisticsEvent = Simulator::ScheduleNow(&DashClientApp::GetStatistics, this);

            m_comulativeSize = 0;
        }
    }
}

void DashClientApp::RequestNextChunk(void)
{
    switch (m_algorithm) {
        case 0:
            DASH();
            break;
        case 1:
            Conventional();
            break;
        case 2:
            BBA();
            break;
        case 3:
            Proposed();
            break;
    }
}

void DashClientApp::DASH(void)
{
    // Estimating
    uint32_t bandwidth = m_bpsLastChunk;

    // Quantizing
    for (uint32_t i = 0; i < m_bitrate_array.size(); i++) {
        if (bandwidth <= m_bitrate_array[i])
            break;
        m_nextBitrate = m_bitrate_array[i];
    }

    // Scheduling
    if (m_bufferSize < MAX_BUFFER_SIZE)
        Simulator::ScheduleNow(&DashClientApp::SendRequest, this);
    else {
        Time tNext(Seconds(m_chunkSize));
        Simulator::Schedule(tNext, &DashClientApp::SendRequest, this);
    }
}

void DashClientApp::Conventional(void)
{
    // Estimating
    uint32_t bandwidth = m_bpsLastChunk;

    // Smoothing
    if (m_bpsAvg == 0)
        m_bpsAvg = bandwidth;
    else
        m_bpsAvg = 0.8 * m_bpsAvg + 0.2 * bandwidth;

    // Quantizing
    for (uint32_t i = 0; i < m_bitrate_array.size(); i++) {
        if (m_bpsAvg <= m_bitrate_array[i])
            break;
        m_nextBitrate = m_bitrate_array[i];
    }

    // Scheduling
    if (m_bufferSize < MAX_BUFFER_SIZE)
        Simulator::ScheduleNow(&DashClientApp::SendRequest, this);
    else {
        Time tNext(Seconds(m_chunkSize));
        Simulator::Schedule(tNext, &DashClientApp::SendRequest, this);
    }
}

void DashClientApp::BBA(void)
{
    // Buffer-based Adaptation
    if (m_bufferSize < 5000)
        m_nextBitrate = m_bitrate_array[0];
    else if (m_bufferSize < 10000)
        m_nextBitrate = m_bitrate_array[1];
    else if (m_bufferSize < 15000)
        m_nextBitrate = m_bitrate_array[2];
    else if (m_bufferSize < 20000)
        m_nextBitrate = m_bitrate_array[3];
    else if (m_bufferSize < 25000)
        m_nextBitrate = m_bitrate_array[4];
    else
        m_nextBitrate = m_bitrate_array[5];

    // Scheduling
    if (m_bufferSize < MAX_BUFFER_SIZE)
        Simulator::ScheduleNow(&DashClientApp::SendRequest, this);
    else {
        Time tNext(Seconds(m_chunkSize));
        Simulator::Schedule(tNext, &DashClientApp::SendRequest, this);
    }
}

void DashClientApp::Proposed(void)
{
    // Propose idea code writing!!
    
}

uint32_t DashClientApp::GetIndexByBitrate(uint32_t bitrate)
{
    for (uint32_t i = 0; i < m_bitrate_array.size(); i++) {
        if (bitrate == m_bitrate_array[i])
            return i;
    }

    return -1;
}

void DashClientApp::SendRequest(void)
{
    if (m_nextBitrate == 0)
        m_nextBitrate = m_bitrate_array[0];

    uint32_t bytesReq = (m_nextBitrate * m_chunkSize) / 8;
    Ptr<Packet> packet = Create<Packet>((uint8_t *) &bytesReq, 4);

    m_lastRequestedSize = bytesReq;
    m_socket->Send(packet);

    if (m_prevBitrate != 0 && m_prevBitrate != m_nextBitrate) {
        m_numOfSwitching++;
        NS_LOG_UNCOND ("Number of Quality Switching : " << m_numOfSwitching);
    }

    m_prevBitrate = m_nextBitrate;

    m_requestTime = Simulator::Now();
}

void DashClientApp::ClientBufferModel(void)
{
    if (m_running)
    {
        if (m_bufferSize < 100) {
            m_bufferSize = 0;
            m_bufferPercent = 0;
            m_chunkCount = 0;

            Simulator::Cancel(m_bufferEvent);
            return;
        }

        m_bufferSize -= 100;

        Time tNext("100ms");
        m_bufferEvent = Simulator::Schedule(tNext, &DashClientApp::ClientBufferModel, this);
    }
}

void DashClientApp::GetStatistics()
{
    NS_LOG_UNCOND ("=======START=========== " << GetNode() << " =================");
    NS_LOG_UNCOND ("Time: " << Simulator::Now ().GetMilliSeconds () <<
                   " bpsAverage: " << m_bpsAvg <<
                   " bpsLastChunk: " << m_bpsLastChunk / 1000 <<
                   " nextBitrate: " << m_nextBitrate <<
                   " chunkCount: " << m_chunkCount <<
                   " totalChunks: " << m_numChunks <<
                   " downloadDuration: " << m_downloadDuration);
}

void DashClientApp::GetBufferState()
{
    m_throughput = m_comulativeSize * 8 / 1 / 1000;

    NS_LOG_UNCOND ("=======BUFFER========== " << GetNode() << " =================");
    NS_LOG_UNCOND ("Time: " << Simulator::Now ().GetMilliSeconds () <<
                   " bufferSize: " << m_bufferSize / 1000 <<
                   " hasThroughput: " << m_throughput <<
                   " estimatedBW: " << m_bpsLastChunk / 1000.0 <<
                   " videoLevel: " << (m_lastRequestedSize * 8) / m_chunkSize / 1000);
    
    Time tNext("1s");
    m_bufferStateEvent = Simulator::Schedule(tNext, &DashClientApp::GetBufferState, this);
}

void DashClientApp::StopApplication(void)
{
    m_running = false;

    if (m_bufferEvent.IsRunning())
    {
        Simulator::Cancel(m_bufferEvent);
    }

    if (m_fetchEvent.IsRunning())
    {
        Simulator::Cancel(m_fetchEvent);
    }

    if (m_statisticsEvent.IsRunning())
    {
        Simulator::Cancel(m_statisticsEvent);
    }

    if (m_bufferStateEvent.IsRunning())
    {
        Simulator::Cancel(m_bufferStateEvent);
    }

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
    string algorithm;

    std::string animFile = "dash-animation.xml" ;  // Name of file for animation output
    cout << "Input the adaptation algorithm : ";
    cin >> algorithm;

    LogComponentEnable("DashApplication", LOG_LEVEL_ALL);

    PointToPointHelper bottleNeck;
    bottleNeck.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    bottleNeck.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleNeck.SetQueue("ns3::DropTailQueue", "Mode", StringValue ("QUEUE_MODE_BYTES"));

    PointToPointHelper pointToPointLeaf;
    pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPointLeaf.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointDumbbellHelper dB(2, pointToPointLeaf, 2, pointToPointLeaf,
                                  bottleNeck);

    // install stack
    InternetStackHelper stack;
    dB.InstallStack(stack);

    // assign IP addresses
    dB.AssignIpv4Addresses(Ipv4AddressHelper("10.1.1.0", "255.255.255.0"),
                           Ipv4AddressHelper("10.2.1.0", "255.255.255.0"),
                           Ipv4AddressHelper("10.3.1.0", "255.255.255.0"));

    uint16_t serverPort = 8080;

    // Cross Traffic CBR

    // Scenario 1 (Freqeunt Bandwidth Change)
    // OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), serverPort));
    // crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=50]"));
    // crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=50]"));
    // crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("1000kb/s")));
    // crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
    // ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
    // srcApp1.Start(Seconds(350.0));
    // srcApp1.Stop(Seconds(800.0));

    // Scenario 2 (Drastic Bandwidth Decrease -> Increase)
    OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), serverPort));
    crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=100]"));
    crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=100]"));
    crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("2000kb/s")));
    crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
    ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
    srcApp1.Start(Seconds(0.0));///////////////
    srcApp1.Stop(Seconds(5.0));///////////////
    OnOffHelper crossTrafficSrc2("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), serverPort));
    crossTrafficSrc2.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=100]"));
    crossTrafficSrc2.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=100]"));
    crossTrafficSrc2.SetAttribute("DataRate", DataRateValue (DataRate("2000kb/s")));
    crossTrafficSrc2.SetAttribute("PacketSize", UintegerValue (512));
    ApplicationContainer srcApp2 = crossTrafficSrc2.Install(dB.GetLeft(0));
    srcApp2.Start(Seconds(0.0));///////////////
    srcApp2.Stop(Seconds(5.0));///////////////

    // Scenario 3 (Drastic Bandwidth Decrease -> Maintain)
    // OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), serverPort));
    // crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=500]"));
    // crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    // crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("2000kb/s")));
    // crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
    // ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
    // srcApp1.Start(Seconds(300.0));
    // srcApp1.Stop(Seconds(800.0));

    // DASH server
    Address bindAddress1(InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    Ptr<DashServerApp> serverApp1 = CreateObject<DashServerApp>();
    serverApp1->Setup(bindAddress1, 512);
    dB.GetLeft(1)->AddApplication(serverApp1);
    serverApp1->SetStartTime(Seconds(0.1));
    serverApp1->SetStopTime(Seconds(10.0));

    // DASH client
    Address serverAddress1(
        InetSocketAddress(dB.GetLeftIpv4Address(1), serverPort));
    Ptr<DashClientApp> clientApp1 = CreateObject<DashClientApp>();
    clientApp1->Setup(serverAddress1, 2, 512, algorithm);
    dB.GetRight(1)->AddApplication(clientApp1);
    clientApp1->SetStartTime(Seconds(0.1));
    clientApp1->SetStopTime(Seconds(9.0));


    // Set the bounding box for animation
    dB.BoundingBox (1, 1, 100, 100);
 
    // Create the animation object and configure for specified output
    AnimationInterface anim (animFile);
    anim.EnablePacketMetadata (); // Optional
    anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (10)); // Optional 

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(10.0));

    Simulator::Run();
    std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
    Simulator::Destroy();

    return 0;
}