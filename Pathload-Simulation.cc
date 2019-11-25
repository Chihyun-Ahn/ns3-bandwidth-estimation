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

NS_LOG_COMPONENT_DEFINE ("PathloadApplication");

// 제안 알고리즘 구현에 필요한 전역 변수들
Time m_requestTime;
bool m_checkTCP;
bool m_checkUDP;

Time m_cTime;
Time m_dTime;
uint32_t m_oneWayDelay;
uint32_t m_srcGapSum = 0;
uint32_t m_lastSrcGapSum = 0;
uint32_t m_dstGapSum = 0;
uint32_t m_srcGap;
uint32_t m_srcGapNext;
uint32_t m_step;
uint32_t m_gB;
bool m_isServerStop; //서버에서 트레인 이제 그만 보낼 시점
uint32_t m_incGapSum;
float m_b_bw = 10.0; //보틀넥 링크 용량(Mbps)
float m_c_bw;
float m_a_bw;
bool m_isServerSending = false;

//================================================================
// SERVER APPLICATION
//================================================================

class PathloadServerApp: public Application
{
public:
    PathloadServerApp();
    virtual ~PathloadServerApp();
    void Setup(Address address, Address addressForUDP, uint32_t packetSize);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void RxCallbackForTCP(Ptr<Socket> socket);
    void TxCallbackForTCP(Ptr<Socket> socket, uint32_t txSpace);

    void RxCallbackForUDP(Ptr<Socket> socket);
    void TxCallbackForUDP(Ptr<Socket> socket, uint32_t txSpace);

    // Need to estabilish connection between server and client using TCP socket
    bool ConnectionCallback(Ptr<Socket> s, const Address &ad);
    void AcceptCallback(Ptr<Socket> s, const Address &ad);
    void SendPacketsForUDP(void);
    void SendPeriod(void);
    void SendData(void);

    bool m_connected;
    Ptr<Socket> m_socket;
    Ptr<Socket> m_peer_socket;

    // 소켓은, 연결 맺기 소켓하고, 전송 소켓 2개로 나눔
    Ptr<Socket> m_connectionSocket;
    Ptr<Socket> m_sendingSocket;

    Address ads;
    Address m_peer_address;
    uint32_t m_remainingDataForTCP;
    uint32_t m_remainingDataForUDP;
    EventId m_sendEvent;

    // 제안 알고리즘에 필요한 변수들
    uint32_t m_packetCountForTCP;
    uint32_t m_packetCountForUDP;
    uint32_t m_trainCount;

    uint32_t m_cumulativeSize;
    uint32_t m_packetSize;

    Address adsForUDP;
    Ptr<Socket> m_socketForUDP;
    // uint32_t m_srcGap;
    uint32_t m_trainSize;
    EventId m_probing;
    Time m_lastPacketTime;
    uint32_t m_nextRoundTime;

};

// 변수 초기화. 선언 순서와 동일해야됨. 주의하기. 
PathloadServerApp::PathloadServerApp() :
    m_connected(false), m_socket(0), m_peer_socket(0),
    m_connectionSocket(0), m_sendingSocket(0), 
    ads(), m_peer_address(), m_remainingDataForTCP(0), m_remainingDataForUDP(0),
    m_sendEvent(), m_packetCountForTCP(0), m_packetCountForUDP(0), m_trainCount(0),
    m_cumulativeSize(0), m_packetSize(0), adsForUDP(), m_socketForUDP(0),
    m_trainSize(0),m_probing()
{

}

PathloadServerApp::~PathloadServerApp()
{
    m_socket = 0;
    m_socketForUDP = 0;
}

// 서버앱 셋업 메소드
void PathloadServerApp::Setup(Address address, Address addressForUDP, uint32_t packetSize)
{
    //셋업시 주소를 2개를 받는겨. 
    ads = address;
    adsForUDP = addressForUDP;

    m_lastPacketTime = Simulator::Now();

    // 패킷 사이즈도 전달받는겨. 패킷 간격이 m_timePeriod
    m_packetSize = packetSize;
    // m_srcGap = 200; // (us)초기 200마이크로초 갭부터 시작 
    m_trainSize = 100; // 패캣 트레인은 256개의 패킷으로 구성
    m_gB = 600; // gB값. 0.6ms (마이크로초)
    m_srcGap = m_gB / 2; // 초기의 갭값. 
    m_srcGapNext = m_gB / 2;
    m_step = m_gB / 8; //스텝값. 
    m_isServerStop = false;
    m_nextRoundTime = 500; //트레인간 간격 1000ms
}

// 서버앱 시작 메소드
void PathloadServerApp::StartApplication()
{

    // 연결 맺기 소켓은 Tcp로 만드는겨. 
    m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_socket->Bind(ads);
    m_socket->Listen();
    m_socket->SetAcceptCallback(
        MakeCallback(&PathloadServerApp::ConnectionCallback, this),
        MakeCallback(&PathloadServerApp::AcceptCallback, this));
}

// 서버앱 종료 메소드. 중요치 않음. 
void PathloadServerApp::StopApplication()
{
    m_connected = false;
    // Close sending socket
    if (m_socket){m_socket->Close();}
    if (m_sendEvent.IsRunning()){Simulator::Cancel(m_sendEvent);}

    // NS_LOG_UNCOND("PathloadServerApp :: StopApplication");
}

// 연결 요청이 들어오면, 콜백할 메소드
bool PathloadServerApp::ConnectionCallback(Ptr<Socket> socket, const Address& adss)
{
    // NS_LOG_UNCOND("PathloadServerApp :: ConnectionCallback");
    m_connected = true; //커넥션 맺었다. 저장. 
    return true;
}

// 연결 요청 승인 콜백
void PathloadServerApp::AcceptCallback(Ptr<Socket> socket, const Address& adss)
{
    m_peer_address = adss;
    m_peer_socket = socket;

    socket->SetRecvCallback(MakeCallback(&PathloadServerApp::RxCallbackForTCP, this));
    socket->SetSendCallback(MakeCallback(&PathloadServerApp::TxCallbackForTCP, this));

    // Udp용 소켓 만들고. 
    m_socketForUDP = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    // 이제 Udp 패킷 보낸다는겨. 시뮬레이터 스케쥴링으로.
    if (~m_socketForUDP->Bind()){
        NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Bind " << adsForUDP);

        if (~m_socketForUDP->Connect(adsForUDP)){
            m_socketForUDP->SetRecvCallback(MakeCallback(&PathloadServerApp::RxCallbackForUDP, this));
            NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Connect " << adsForUDP);

            Simulator::ScheduleNow(&PathloadServerApp::SendPacketsForUDP, this);
        }
    }else {
        NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Bind Fail");
    }

    NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback");
}

void PathloadServerApp::RxCallbackForUDP(Ptr<Socket> socket)
{
    NS_LOG_UNCOND("PathloadServerApp :: RxCallbackForUDP");
}

void PathloadServerApp::TxCallbackForUDP(Ptr<Socket> socket, uint32_t txSpace)
{
    NS_LOG_UNCOND("PathloadServerApp :: TxCallbackForUDP");
}

// Udp 패킷 한개 보낼 때마다 실행될 메소드
void PathloadServerApp::SendPacketsForUDP(void)
{
    if(m_isServerStop){
        Simulator::Cancel(m_probing);
    }else{
        m_packetCountForUDP++; // 1개 보낼 때마다 몇 개 보냈는지, m_packetCountForUDP에 저장. 
        if(m_packetCountForUDP <= m_trainSize){
            m_isServerSending = true;
            if(m_packetCountForUDP == 1){ m_srcGapSum = 0;}

            uint32_t bytesSend = m_packetSize;

            Time curTime = Simulator::Now();
            //패킷을 지정된 패킷사이즈(750바이트)로 만들어서, Udp 주소로 보낸다. 
            Ptr<Packet> packet = Create<Packet>((uint8_t*)&bytesSend, bytesSend);
            m_socketForUDP->SendTo(packet, 0, adsForUDP);
            m_cTime = curTime;

            // NS_LOG_UNCOND("서버에서 보낸 패킷 개수: " << m_packetCountForUDP);
            Simulator::ScheduleNow(&PathloadServerApp::SendPeriod, this); // 호출했던 함수로 다시 돌아감. 
            if(m_packetCountForUDP > 1){
                uint32_t m_gapNow = curTime.GetMicroSeconds() - m_lastPacketTime.GetMicroSeconds();
                NS_LOG_UNCOND("서버에서 보낸 패킷 개수: " << m_packetCountForUDP << " 소스 갭: " << m_gapNow);
                m_srcGapSum += m_gapNow;
            } 
            m_lastPacketTime = curTime;
        }else if(m_packetCountForUDP > m_trainSize){
            m_isServerSending = false;
            m_srcGap = m_srcGapNext;
            m_packetCountForUDP = 0;
            m_trainCount++;
            m_lastSrcGapSum = m_srcGapSum;
            NS_LOG_UNCOND(m_trainCount << "번째 트레인 서버에서 전송 완료.m_lastSrcGapSum: " << m_lastSrcGapSum);
            // Simulator::Cancel(m_probing);
            // Time tNextProcess(MilliSeconds(m_nextRoundTime));
            // Simulator::Schedule(tNextProcess, &PathloadServerApp::SendPeriod, this);
            Simulator::ScheduleNow(&PathloadServerApp::SendPeriod, this); // 호출했던 함수로 다시 돌아감. 
        }
    }
    
}

// TCP부분은 중요치 않음. 스킵.
void PathloadServerApp::RxCallbackForTCP(Ptr<Socket> socket)
{
    Address ads;
    Ptr<Packet> pckt = socket->RecvFrom(ads);

    if (ads == m_peer_address){
        uint32_t data = 0;
        pckt->CopyData((uint8_t*)&data, 4);
        m_remainingDataForTCP = data;
        Simulator::ScheduleNow(&PathloadServerApp::SendPeriod, this);
    }
}

// TCP부분은 중요치 않음. 스킵.
void PathloadServerApp::TxCallbackForTCP(Ptr<Socket> socket, uint32_t txSpace)
{
    if (m_connected)
    {
        Simulator::ScheduleNow(&PathloadServerApp::SendData, this);
    }
}

// TCP부분은 중요치 않음. 스킵.
void PathloadServerApp::SendData(void)
{
    while (m_remainingDataForTCP > 0){
        uint32_t toSend = min(m_packetSize, m_remainingDataForTCP);
        Ptr<Packet> packet = Create<Packet>((uint8_t*)&toSend, 4);
        int actual = m_peer_socket->Send(packet);
        if (actual > 0){
            m_remainingDataForTCP -= toSend;
        }
        if ((unsigned)actual != toSend){
            break;
        }
    }
}

// 프로프 udp패킷을 시간간격 대로 보내는 부분. 
void PathloadServerApp::SendPeriod(void)
{
    // NS_LOG_UNCOND("PathloadServerApp :: SendPeriod");
    Time tNext(MicroSeconds(m_srcGap));
    m_probing = Simulator::Schedule(tNext, &PathloadServerApp::SendPacketsForUDP, this);
}

//================================================================
// CLIENT APPLICATION
//================================================================

class PathloadClientApp: public Application
{
public:

    PathloadClientApp();
    virtual ~PathloadClientApp();

    void Setup(Address address, Address addressForUDP, uint32_t packetSize);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void RequestNextPacket(void);
    void SendRequest(void);

    // Need to classify Rx callback for TCP and UDP
    void RxCallbackForTCP(Ptr<Socket> socket);
    void RxCallbackForUDP(Ptr<Socket> socket);

    void RxDrop(Ptr<const Packet> p);

    // Handle periodic request of probing packet
    void HandleProbing(void);

    Ptr<Socket> m_socketForTCP;
    Address m_peer;
    bool m_running;

    // 알고리즘 구현을 위한 변수
    uint32_t m_numOfPackets;
    uint32_t m_sizeOfPackets;
    uint32_t m_timePeriod;

    uint32_t m_maxSizeOfPackets;
    uint32_t m_minSizeOfPackets;

    double m_rateOfStream;

    uint32_t m_packetSize;

    Ptr<Socket> m_socketForUDP;
    uint32_t m_sizeOfRequestPackets;
    uint32_t m_cumulativeSize;
    uint32_t m_packetCountForUDP;
    uint32_t m_trainSize;
    Time m_lastPacketTime;
    uint32_t m_trainCount;

    Address adsForUDP;
    uint32_t m_oneWayDelay;
    float m_equalNorm;
    // uint32_t m_c_bw;
    uint32_t m_a_bw;

};

PathloadClientApp::PathloadClientApp() :
    m_socketForTCP(0), m_peer(), m_running(false), m_numOfPackets(0),
    m_sizeOfPackets(0), m_timePeriod(0), m_maxSizeOfPackets(0), m_minSizeOfPackets(0),
    m_rateOfStream(0), m_packetSize(0), m_socketForUDP(0), m_sizeOfRequestPackets(0),
    m_cumulativeSize(0), m_packetCountForUDP(0), m_trainSize(0),adsForUDP(), m_oneWayDelay(0)
{

}

PathloadClientApp::~PathloadClientApp()
{
    m_socketForTCP = 0;
    m_socketForUDP = 0;
}

// 클라이언트앱 셋업
void PathloadClientApp::Setup(Address address, Address addressForUDP, uint32_t packetSize)
{
    // NS_LOG_UNCOND("PathloadClientApp :: Setup");

    m_peer = address;
    adsForUDP = addressForUDP;

    m_packetSize = packetSize;

    m_trainSize = 100; //트레인 사이즈 256개
    m_timePeriod = 200; // 패킷 간격 100마이크로초
    m_lastPacketTime = Simulator::Now(); 
    m_trainCount = 0;

    // Calculate expected stream rate using number of packets, size of packets, and sending period of each packet (unit of rate is 'bps')
    m_rateOfStream = (double)(m_trainSize * m_sizeOfPackets * 8) / ((double)(m_timePeriod * m_trainSize) / pow(10, 6));

    // Expected stream rate is represented to 'Mbps' unit
    NS_LOG_UNCOND("PathloadClient :: Setup :: Expected Stream Rate :: " << (double)m_rateOfStream / pow(10, 6) << " Mbps" << " Checked At Client");
}

void PathloadClientApp::RxDrop(Ptr<const Packet> p)
{
    NS_LOG_UNCOND("PathloadClientApp :: RxDrop :: At " << Simulator::Now().GetSeconds());
}

// 클라이언트앱 시작 메소드
void PathloadClientApp::StartApplication(void)
{
    // NS_LOG_UNCOND("PathloadClientApp :: StartApplication");

    // UDP socket of Pathload client
    m_socketForUDP = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    if (~m_socketForUDP->Bind(adsForUDP))
    {
        m_socketForUDP->SetRecvCallback(MakeCallback(&PathloadClientApp::RxCallbackForUDP, this));

        NS_LOG_UNCOND("PathloadClientApp :: StartApplication :: UDP Bind " << adsForUDP);

        m_socketForTCP = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        m_socketForTCP->TraceConnectWithoutContext("Drop", MakeCallback(&PathloadClientApp::RxDrop, this));
        m_running = true;
        m_socketForTCP->Bind();

        if (~m_socketForTCP->Connect(m_peer))
        {
            NS_LOG_UNCOND("PathloadClientApp :: StartApplication :: TCP Connect");
        }
    }
}

void PathloadClientApp::RequestNextPacket(void)
{
    NS_LOG_UNCOND("PathloadClientApp :: RequestNextPacket");
}

void PathloadClientApp::RxCallbackForUDP(Ptr<Socket> socket)
{
    // NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP");

    Ptr<Packet> packet;

    // Udp 패킷을 1개 소켓에서 받으면, 
    while ((packet = socket->Recv()))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }
        uint32_t m_recvPcktSize = packet->GetSize();
        m_packetCountForUDP++; // 카운트 1 증가. (초기값 = 0)
        if(m_packetCountForUDP == 1){m_dstGapSum = 0;} // 첫 패킷일 경우, 지난 목적지 갭 합산값 초기화
        NS_LOG_UNCOND("클라이언트에 받은 패킷 개수: " << m_packetCountForUDP << " 패킷 사이즈: " << m_recvPcktSize);
        Time curTime = Simulator::Now(); //받은 시간값 저장
        // 2번째 패킷부터, 목적지 갭 계산 후 합산값에 누적. 
        if(m_packetCountForUDP > 1 && m_packetCountForUDP <= m_trainSize){
            uint32_t m_dstGap = curTime.GetMicroSeconds() - m_lastPacketTime.GetMicroSeconds();
            NS_LOG_UNCOND("클라이언트: 현재 패킷의 목적지 갭: " << m_dstGap);
            m_dstGapSum += m_dstGap;
        }
        m_lastPacketTime = curTime;
        // 100번째 패킷이 왓을 경우, 
        if(m_packetCountForUDP == m_trainSize){
            m_packetCountForUDP = 0;  // 패킷 카운트 초기화
            m_trainCount++; // 트레인 카운트
            m_incGapSum = m_dstGapSum - m_lastSrcGapSum; 
            NS_LOG_UNCOND("m_lastSrcGapSum: " << m_lastSrcGapSum << " dstGapSum: " << m_dstGapSum);
            NS_LOG_UNCOND("Increased Gap Sum: " << m_incGapSum);
            m_equalNorm = ((float) m_incGapSum) / ((float)m_dstGapSum);
            NS_LOG_UNCOND("m_equalNorm: " << m_equalNorm);
            if(m_equalNorm < 0.1){
                NS_LOG_UNCOND("m_b_bw: " << m_b_bw);
                m_c_bw = m_b_bw * m_equalNorm; //경쟁 트래픽 스루풋
                m_a_bw = m_b_bw - m_c_bw; //가용대역폭
                m_isServerStop = true;
                NS_LOG_UNCOND("보낸 트레인 개수: " << m_trainCount <<"경쟁 트래픽(Mbps): " << m_c_bw << "가용대역폭(Mbps): " << m_a_bw);
                Simulator::Stop();
            }else{
                m_srcGapNext += m_step; 
                NS_LOG_UNCOND("클라이언트에서: 소스갭 다음으로 변경: " << m_srcGapNext);
            }
        }       
    }
}

void PathloadClientApp::HandleProbing(void)
{
    NS_LOG_UNCOND("PathloadClientApp :: HandleProbing");
}

void PathloadClientApp::SendRequest(void)
{
    NS_LOG_UNCOND("PathloadClientApp :: SendRequest");

    uint32_t bytesReq = m_sizeOfPackets;

    while (bytesReq > 0)
    {
        uint32_t toSend = min(bytesReq, m_sizeOfPackets);
        Ptr<Packet> packet = Create<Packet>(toSend);

        int actual = m_socketForUDP->Send(packet);

        if ((unsigned)actual != toSend)
        {
            break;
        }
    }
}

void PathloadClientApp::StopApplication(void)
{
    m_running = false;

    if (m_socketForUDP)
    {
        m_socketForUDP->Close();
    }
}

//=================================================================
// SIMULATION
//================================================================

int main(int argc, char *argv[])
{

    float m_stopTime = 300.0;

    std::string animFile = "dash-animation.xml" ;  // Name of file for animation output
    LogComponentEnable("PathloadApplication", LOG_LEVEL_ALL);

    PointToPointHelper bottleNeck;
    bottleNeck.SetDeviceAttribute("DataRate", StringValue("10Mbps")); //주의: 맨 위에 전역 변수 m_b_bw값도 넣어주기
    bottleNeck.SetChannelAttribute("Delay", StringValue("5ms"));
    bottleNeck.SetQueue("ns3::DropTailQueue", "Mode", StringValue("QUEUE_MODE_PACKETS"));

    PointToPointHelper pointToPointLeaf;
    pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPointLeaf.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointDumbbellHelper dB(2, pointToPointLeaf, 2, pointToPointLeaf, bottleNeck);

    // Install stack
    InternetStackHelper stack;
    dB.InstallStack(stack);

    // Assign IP addresses
    dB.AssignIpv4Addresses(Ipv4AddressHelper("10.1.1.0", "255.255.255.0"),
        Ipv4AddressHelper("10.2.1.0", "255.255.255.0"),
        Ipv4AddressHelper("10.3.1.0", "255.255.255.0"));

    uint16_t serverPortTCP = 8080;
    uint16_t serverPortUDP = 8090;

    uint16_t port = 8100;

    // 경쟁 트래픽
    OnOffHelper onoff1("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(0), port));
    onoff1.SetConstantRate(DataRate("4.0Mbps"), 2000);
    ApplicationContainer cbrApp1 = onoff1.Install(dB.GetLeft(0));

    cbrApp1.Start(Seconds(0.02));
    cbrApp1.Stop(Seconds(m_stopTime));
    
    PacketSinkHelper cbrSink1("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    cbrApp1 = cbrSink1.Install(dB.GetRight(0));
    cbrApp1.Start(Seconds(0.0));
    cbrApp1.Stop(Seconds(m_stopTime));



    Address TCPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), serverPortTCP));
    Address TCPServerAddress(InetSocketAddress(dB.GetLeftIpv4Address(1), serverPortTCP));
    Address UDPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), serverPortUDP));
    Address UDPServerAddress(InetSocketAddress(dB.GetRightIpv4Address(1), serverPortUDP));

    // 패스로드 서버
    Ptr<PathloadServerApp> serverApp1 = CreateObject<PathloadServerApp>();
    serverApp1->Setup(TCPBindAddress, UDPServerAddress, 750); //패킷 사이즈는 750
    dB.GetLeft(1)->AddApplication(serverApp1);
    serverApp1->SetStartTime(Seconds(0.0));
    serverApp1->SetStopTime(Seconds(m_stopTime));

    // 패스로드 클라이언트
    Ptr<PathloadClientApp> clientApp1 = CreateObject<PathloadClientApp>();
    clientApp1->Setup(TCPServerAddress, UDPBindAddress, 750);  //패킷 사이즈는 750
    dB.GetRight(1)->AddApplication(clientApp1);
    clientApp1->SetStartTime(Seconds(0.0));
    clientApp1->SetStopTime(Seconds(m_stopTime));

    // Set the bounding box for animation
    dB.BoundingBox (1, 1, 100, 100);
 
    // Create the animation object and configure for specified output
    AnimationInterface anim (animFile);
    anim.EnablePacketMetadata (); // Optional
    anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (m_stopTime)); // Optional 

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(m_stopTime));

    Simulator::Run();
    std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
    Simulator::Destroy();

    return 0;
}