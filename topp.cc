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

    int64x64_t m_dstIntervalSum;
    // vector<uint32_t> m_bitrate_array;
    // vector<uint32_t> m_sendTime_array;
    Time m_firstPcktRcvTime;
    Time m_receiveTime;

    uint32_t m_receive_packet;
    uint32_t m_receive_rate;
    uint32_t m_trainSize;
};
// 생성자. 초기값 설정
DashServerApp::DashServerApp() :
    m_connected(false), m_socket(0), m_peer_socket(0), 
    ads(), m_peer_address(), m_remainingData(0), 
    m_sendEvent(), m_packetSize(0), m_packetCount(0), 
    m_dstIntervalSum(0), m_firstPcktRcvTime(0),
    m_receiveTime(0), m_receive_packet(0),
    m_receive_rate(0),m_trainSize(256)
{

}

DashServerApp::~DashServerApp()
{
    m_socket = 0;
}

// 대쉬서버 설정시, 주소와 패킷 사이즈를 받아서 저장해놓고, 
// 조절할 센딩 레이트(비트레이트) 값들을 넣어놓고, 
// 또한 조절할 센딩 타이밍 값들을 넣어 놓음. (m_bitrate_array와, m_sendTime_array)
void DashServerApp::Setup(Address address, uint32_t packetSize)
{
    ads = address;
    m_packetSize = 0;
    m_packetSize = packetSize;
}
// 서버를 시작하는 메소드
void DashServerApp::StartApplication()
{
    //소켓을 만들어서, m_socket에다 저장한다.
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    // 소켓에,,, 주소 할당하고, 리스닝 시키고, 메세지 송신, 수신시 콜백할 함수 지정. 
    m_socket->Bind(ads);
    m_socket->Listen();
    m_socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback, this));
    m_socket->SetSendCallback(MakeCallback(&DashServerApp::TxCallback, this));
}

// 서버 닫아주는 메소드. 볼 필요 없다. 
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

// 서버에서 메세지 수신시에 뭘 할지. 
void DashServerApp::RxCallback(Ptr<Socket> socket)
{
    // 소켓 객체에서, 받아진 패킷을 저장. pckt
    Address ads;
    Ptr<Packet> pckt = socket->RecvFrom(ads);
    
    //받은 패킷 정보를 data에 저장
    uint32_t data = 0;
    pckt->CopyData((uint8_t *) &data, 4);

    //설정은 한 다음에, 시뮬레이터 동작시 실시간 설정 부분. 
    Time cTime = Simulator::Now();
    if(m_packetCount < m_trainSize){
        m_packetCount ++;
        if(m_packetCount == 1){m_firstPcktRcvTime = cTime;}
        int64x64_t dstGap = cTime.GetMicroSeconds() - m_receiveTime.GetMicroSeconds();
        m_receiveTime = cTime;
        m_dstIntervalSum += dstGap;
        NS_LOG_UNCOND("Time: " << cTime.GetMicroSeconds() << 
            " Received Data: " << data << " dstGap: " << dstGap << 
            " trainCount: " << m_packetCount);
    }else{
        m_dstIntervalSum = m_dstIntervalSum - m_firstPcktRcvTime.GetMicroSeconds();
        NS_LOG_UNCOND("Time: " << cTime << "dstIntervalSum: " << m_dstIntervalSum);
    }
}

// 서버에서 패킷을 보낼 때 콜백함수
void DashServerApp::TxCallback(Ptr<Socket> socket, uint32_t txSpace)
{
    Simulator::ScheduleNow (&DashServerApp::SendData, this);
}

// 서버에서 패킷을 보낼 때 콜백의 콜백
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

    // Rate Adaptation Algorithm
    void Proposed(void);

    Ptr<Socket> m_socket;
    Address m_peer;
    // vector<uint32_t> m_bitrate_array;
    // vector<uint32_t> m_sendTime_array;
    bool m_running;

    // Proposed
    uint32_t num_Send_Packet;
    uint32_t m_packet_rate;
    uint32_t m_next_size;

    uint32_t m_packetSize;
    uint32_t m_interval;
    uint32_t m_trainSize;
    uint32_t trainCount;
    uint32_t m_firstPcktSent;
    uint32_t m_prevPcktTime;
    uint32_t m_srcIntervalSum;
};

// 생성자. 초기값 설정. 
DashClientApp::DashClientApp() :
    m_socket(0), m_peer(), m_running(false),
    num_Send_Packet(0), m_packet_rate(0), m_next_size(750),
    m_trainSize(256),trainCount(0), m_firstPcktSent(0),
    m_prevPcktTime(0), m_srcIntervalSum(0)
{

}

DashClientApp::~DashClientApp()
{
    m_socket = 0;
}

// 설정시에, 서버 주소를 받아서 m_peer에 저장해놓고, 
// 조절할 비트레이트값들, 보낼 시간값들을 벡터에 저장해놓기
void DashClientApp::Setup(Address address)
{
    m_peer = address;

    m_packetSize = 750;
    m_interval = 500;
}

// 앱을 시작. 
void DashClientApp::StartApplication(void)
{
    // 소켓 만들고, 
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    // 소켓에 서버 주소 연결
    m_running = true;
    m_socket->Bind();
    m_socket->Connect(m_peer);

    // 제안될 메소드 실행. 
    Proposed();
}

void DashClientApp::RequestNextChunk(void)
{
    Proposed();
}

// 제안할 메소드. 클라이언트앱 시작시 실행
void DashClientApp::Proposed(void)
{
    // 다음 보낼 패킷 사이즈 지정
    m_next_size = 750;

    // Time scheduling
    Time tNext(MicroSeconds(m_interval));
    ///////
    NS_LOG_UNCOND("tNext: " << tNext);
 
    Simulator::Schedule(tNext, &DashClientApp::SendRequest, this);
}

// 전송 요청 부분
void DashClientApp::SendRequest(void)
{
    // 패킷 사이즈가 m_next_rate인 패킷을 만들어서 보낸다... 로 이해하자 일단
    uint32_t bytesReq = m_packetSize;
    Ptr<Packet> packet = Create<Packet>((uint8_t *) &bytesReq, 4);
    trainCount++;
    if(trainCount <= m_trainSize){
        m_socket->Send(packet);
        Time cTime = Simulator::Now();
        if(trainCount == 1){
            m_firstPcktSent = cTime.GetMicroSeconds();
        }
        int64x64_t srcGap = cTime.GetMicroSeconds() - m_prevPcktTime;
        m_prevPcktTime = cTime.GetMicroSeconds();
        m_srcIntervalSum += cTime.GetMicroSeconds();
        NS_LOG_UNCOND("Time : "<< cTime << " Send Packet Size : " 
            << m_next_size << " trainCount: " << trainCount << " srcGap:"<< srcGap);
        Simulator::ScheduleNow(&DashClientApp::RequestNextChunk, this);
    }else{
        m_srcIntervalSum = m_srcIntervalSum - m_prevPcktTime;
        NS_LOG_UNCOND("source gap total : "<< m_srcIntervalSum );
        StopApplication();
    }
}

// 클라이언트앱 스톱. 볼 필요 없음. 
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

    std::string animFile = "dash-animation.xml" ;  // Name of file for animation output

    LogComponentEnable("DashApplication", LOG_LEVEL_ALL);

    PointToPointHelper bottleNeck;
    bottleNeck.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleNeck.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleNeck.SetQueue("ns3::DropTailQueue", "Mode", StringValue ("QUEUE_MODE_BYTES"));

    PointToPointHelper pointToPointLeaf;
    pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPointLeaf.SetChannelAttribute("Delay", StringValue("10ms"));

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

    // 경쟁 트래픽 설정
    uint32_t dstPort = 1000;
    Address dstAddress (InetSocketAddress (dB.GetLeftIpv4Address (0), dstPort));
    PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", dstAddress);

    OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", dstAddress);
    crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=2.0]"));
    crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.000001]"));
    crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("3.6Mb/s")));
    crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (750));
    ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetRight(0));

    ApplicationContainer dstApp1;
    dstApp1 = sinkHelper.Install(dB.GetLeft(0));
    dstApp1.Start(Seconds(0.0));
    dstApp1.Stop(Seconds(5.0));
    srcApp1.Start(Seconds(0.0));
    srcApp1.Stop(Seconds(5.0));

    // DASH server
    Address bindAddress1(InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    Ptr<DashServerApp> serverApp1 = CreateObject<DashServerApp>();
    serverApp1->Setup(bindAddress1, 6000);
    dB.GetLeft(1)->AddApplication(serverApp1);
    serverApp1->SetStartTime(Seconds(0.0));
    serverApp1->SetStopTime(Seconds(5.0));

    // DASH client
    Address serverAddress1(InetSocketAddress(dB.GetLeftIpv4Address(1), serverPort));
    Ptr<DashClientApp> clientApp1 = CreateObject<DashClientApp>();
    clientApp1->Setup(serverAddress1);
    dB.GetRight(1)->AddApplication(clientApp1);
    clientApp1->SetStartTime(Seconds(0.0));
    clientApp1->SetStopTime(Seconds(5.0));

    // Set the bounding box for animation
    dB.BoundingBox (1, 1, 100, 100);
 
    // Create the animation object and configure for specified output
    AnimationInterface anim (animFile);
    anim.EnablePacketMetadata (); // Optional
    anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (5)); // Optional 

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(5.0));

    Simulator::Run();
    std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
    Simulator::Destroy();

    return 0;
}