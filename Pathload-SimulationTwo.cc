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

// This simulation is to test the bandwidth measurement tool, pathload

// Global variable to implement SLoPS scheme
vector<uint32_t> m_depatureTimeArray;
vector<uint32_t> m_arrivalTimeArray;
vector<uint32_t> m_oneWayDelayArray;

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

    // Classifies into connection socket and sending socket
    Ptr<Socket> m_connectionSocket;
    Ptr<Socket> m_sendingSocket;

    Address ads;
    Address m_peer_address;
    uint32_t m_remainingDataForTCP;
    uint32_t m_remainingDataForUDP;
    EventId m_sendEvent;

    // To implememt SLoPS scheme
    uint32_t m_packetCountForTCP;
    uint32_t m_packetCountForUDP;
    uint32_t m_fleetCount;

    uint32_t m_cumulativeSize;
    uint32_t m_packetSize;

    Address adsForUDP;
    Ptr<Socket> m_socketForUDP;
    uint32_t m_timePeriod;
    EventId m_probing;

    bool isIdlePeriod;
    Time m_cTime;
    uint32_t m_nextRoundTime;

    uint32_t m_packetSizeOfNextStream;
    uint32_t m_maxPacketSizeOfNextStream;
    uint32_t m_minPacketSizeOfNextStream;

    uint32_t m_numOfGroup;
    uint32_t m_numOfPacketsAtServer;

    bool m_pairwiseComparisonTest;
    bool m_pairwiseDifferenceTest;

    double m_pairwiseComparisonIndicator;
    double m_pairwiseDifferenceIndicator;
    uint32_t m_increasingCountOfComparison;
    uint32_t m_increasingCountOfDifference;
};

PathloadServerApp::PathloadServerApp() :
    m_connected(false), m_socket(0), m_peer_socket(0),
    m_connectionSocket(0), m_sendingSocket(0), 
    ads(), m_peer_address(), m_remainingDataForTCP(0), m_remainingDataForUDP(0),
    m_sendEvent(), m_packetCountForTCP(0), m_packetCountForUDP(0), m_fleetCount(0),
    m_cumulativeSize(0), m_packetSize(0), adsForUDP(), m_socketForUDP(0), m_timePeriod(0),
    m_probing(), isIdlePeriod(false), m_cTime(), m_nextRoundTime(0), m_packetSizeOfNextStream(0),
    m_maxPacketSizeOfNextStream(0), m_minPacketSizeOfNextStream(0), m_numOfGroup(0),
    m_numOfPacketsAtServer(0), m_pairwiseComparisonTest(false), m_pairwiseDifferenceTest(false),
    m_pairwiseComparisonIndicator(0.0), m_pairwiseDifferenceIndicator(0.0), m_increasingCountOfComparison(0),
    m_increasingCountOfDifference(0)
{

}

PathloadServerApp::~PathloadServerApp()
{
    m_socket = 0;
    m_socketForUDP = 0;
}

void PathloadServerApp::Setup(Address address, Address addressForUDP, uint32_t packetSize)
{
    ads = address;
    adsForUDP = addressForUDP;

    m_packetSize = packetSize;
    m_timePeriod = 100; // (us)
    m_nextRoundTime = 100000; // (us)

    m_numOfGroup = 10; // 100 packets to the 10 groups
    m_numOfPacketsAtServer = 100;

    NS_LOG_UNCOND("PathloadServerApp :: Setup");
}

void PathloadServerApp::StartApplication()
{
    m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_socket->Bind(ads);
    m_socket->Listen();
    m_socket->SetAcceptCallback(
        MakeCallback(&PathloadServerApp::ConnectionCallback, this),
        MakeCallback(&PathloadServerApp::AcceptCallback, this));

    NS_LOG_UNCOND("PathloadServerApp :: StartApplication");
}

void PathloadServerApp::StopApplication()
{
    m_connected = false;

    // Close sending socket
    if (m_socket)
    {
        m_socket->Close();
    }

    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }

    NS_LOG_UNCOND("PathloadServerApp :: StopApplication");
}

bool PathloadServerApp::ConnectionCallback(Ptr<Socket> socket, const Address& adss)
{
    m_connected = true;

    NS_LOG_UNCOND("PathloadServerApp :: ConnectionCallback");

    return true;
}

void PathloadServerApp::AcceptCallback(Ptr<Socket> socket, const Address& adss)
{
    m_peer_address = adss;
    m_peer_socket = socket;

    socket->SetRecvCallback(MakeCallback(&PathloadServerApp::RxCallbackForTCP, this));
    socket->SetSendCallback(MakeCallback(&PathloadServerApp::TxCallbackForTCP, this));

    m_socketForUDP = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    if (~m_socketForUDP->Bind())
    {
        NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Bind " << adsForUDP);

        if (~m_socketForUDP->Connect(adsForUDP))
        {
            m_socketForUDP->SetRecvCallback(MakeCallback(&PathloadServerApp::RxCallbackForUDP, this));
            NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Connect " << adsForUDP);

            Simulator::ScheduleNow(&PathloadServerApp::SendPacketsForUDP, this);
        }
    }

    else {
        NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback :: UDP Bind Fail");
    }

    NS_LOG_UNCOND("PathloadServerApp :: AcceptCallback");
}

void PathloadServerApp::RxCallbackForUDP(Ptr<Socket> socket)
{
    // NS_LOG_UNCOND("PathloadServerApp :: RxCallbackForUDP");
}

void PathloadServerApp::TxCallbackForUDP(Ptr<Socket> socket, uint32_t txSpace)
{
    // NS_LOG_UNCOND("PathloadServerApp :: TxCallbackForUDP");
}

void PathloadServerApp::SendPacketsForUDP(void)
{
    uint32_t bytesSend = m_packetSize;

    Ptr<Packet> packet = Create<Packet>((uint8_t*)&bytesSend, bytesSend);
    m_socketForUDP->SendTo(packet, 0, adsForUDP);

    m_packetCountForUDP++;

    if (isIdlePeriod)
    {
        // NS_LOG_UNCOND("PathloadServerApp :: SendPacketsForUDP :: Probing After Idle Period 100 ms");
    }

    // else {

        if (m_packetCountForUDP > 0)
        {
            // NS_LOG_UNCOND("PathloadServerApp :: SendPacketsForUDP :: Probing " << m_packetCountForUDP << "th Packet..");

            m_cTime = Simulator::Now();

            m_depatureTimeArray.push_back(m_cTime.GetMilliSeconds());

            // NS_LOG_UNCOND("PathloadServerApp :: SendPacketsForUDP :: Current Time Check :: " << m_cTime.GetMicroSeconds());
        }

    Simulator::ScheduleNow(&PathloadServerApp::SendPeriod, this);

    // }

    // NS_LOG_UNCOND("PathloadServerApp :: SendPacketsForUDP");
}

void PathloadServerApp::RxCallbackForTCP(Ptr<Socket> socket)
{
    // Address ads;
    // Ptr<Packet> pckt = socket->RecvFrom(ads);

    // if (ads == m_peer_address)
    // {
    //     uint32_t data = 0;
    //     pckt->CopyData((uint8_t*)&data, 4);

    //     m_remainingDataForTCP = data;

    // }
}

void PathloadServerApp::TxCallbackForTCP(Ptr<Socket> socket, uint32_t txSpace)
{
    if (m_connected)
    {
        Simulator::ScheduleNow(&PathloadServerApp::SendData, this);
    }
}

void PathloadServerApp::SendData(void)
{
    // while (m_remainingDataForTCP > 0)
    // {
    //     uint32_t toSend = min(m_packetSize, m_remainingDataForTCP);
    //     Ptr<Packet> packet = Create<Packet>((uint8_t*)&toSend, 4);

    //     int actual = m_peer_socket->Send(packet);

    //     if (actual > 0)
    //     {
    //         m_remainingDataForTCP -= toSend;
    //     }

    //     if ((unsigned)actual != toSend)
    //     {
    //         break;
    //     }
    // }
}

void PathloadServerApp::SendPeriod(void)
{
    // NS_LOG_UNCOND("PathloadServerApp :: SendPeriod");

    if (m_timePeriod)
    {
        if (m_packetCountForUDP > m_numOfPacketsAtServer - 1) {

            for (uint32_t index = 0; index < m_depatureTimeArray.size(); index++)
            {
                // NS_LOG_UNCOND("Depature Time of " << (index + 1) << "th Packet :: " << m_depatureTimeArray[index]);
            }

            // NS_LOG_UNCOND("Depature :: " << m_depatureTimeArray.size());

            m_packetCountForUDP = 0;

            m_fleetCount++;

            m_depatureTimeArray.clear();

            NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: Fleet Count At Server :: " << m_fleetCount);

            NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: " << m_fleetCount << "th Probing Round End");

            NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: OWD Delay Trend Check..");

            // if (m_fleetCount > 1)
            // {
            //     m_increasingCountOfComparison = 0;
            //     m_increasingCountOfDifference = 0;

            //     for (uint32_t biasValue = 0; biasValue < 10; biasValue++)
            //     {
            //         m_pairwiseComparisonIndicator = 0.0;
            //         m_pairwiseDifferenceIndicator = 0.0;

            //         for (uint32_t indexOf = 0 + (biasValue * 10); indexOf < 9 + (biasValue * 10); indexOf++)
            //         {

            //             if (m_oneWayDelayArray[indexOf + 1] > m_oneWayDelayArray[indexOf])
            //             {
            //                 m_pairwiseComparisonIndicator++;
            //             }

            //             m_pairwiseDifferenceIndicator += abs(m_oneWayDelayArray[indexOf + 1] - m_oneWayDelayArray[indexOf]);
            //         }

            //         m_pairwiseComparisonIndicator /= ((double)m_numOfGroup - 1);

            //         NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: Pairwise Comparison of :: " << (biasValue + 1) << "th Group :: " << m_pairwiseComparisonIndicator);

            //         if (m_pairwiseComparisonIndicator > 0.55)
            //         {
            //             m_increasingCountOfComparison++;
            //         }

            //         if (m_pairwiseDifferenceIndicator == 0.0)
            //         {
            //             m_pairwiseDifferenceIndicator = 0.0;
            //         }

            //         else {
            //             m_pairwiseDifferenceIndicator = 1.0 / m_pairwiseDifferenceIndicator;
            //         }

            //         m_pairwiseDifferenceIndicator = (m_oneWayDelayArray[(biasValue * 10) + m_numOfGroup - 1] - m_oneWayDelayArray[(biasValue * 10)]) * m_pairwiseDifferenceIndicator;

            //         NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: Pairwise Difference of :: " << (biasValue + 1) << "th Group :: " << m_pairwiseDifferenceIndicator);

            //         if (m_pairwiseDifferenceIndicator > 0.4)
            //         {
            //             m_increasingCountOfDifference++;
            //         }
            //     }

            //     NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: Comparison Count :: " << m_increasingCountOfComparison);
            //     NS_LOG_UNCOND("PathloadServerApp :: SendPeriod :: Difference Count :: " << m_increasingCountOfDifference);
            // }

            isIdlePeriod = true;

            Simulator::Cancel(m_probing);

            // Probing cancel during inter-stream duration
            Time tNextProcess(MicroSeconds(m_nextRoundTime));

            Simulator::Schedule(tNextProcess, &PathloadServerApp::SendPeriod, this);
            // Simulator::Schedule(tNextProcess, &PathloadServerApp::SendPacketsForUDP, this);
        }

        else {

            isIdlePeriod = false;

            Time tNext(MicroSeconds(m_timePeriod));
            m_probing = Simulator::Schedule(tNext, &PathloadServerApp::SendPacketsForUDP, this);
        }
    }
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

    // To implment SLoPs scheme
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

    Address adsForUDP;

    uint32_t m_numOfIncrease;
    uint32_t m_numOfNonIncrease;
    uint32_t m_thresholdForTrendJudgement;

    uint32_t m_localFleetCount;
    uint32_t m_defaultPropagationDelay;
};

PathloadClientApp::PathloadClientApp() :
    m_socketForTCP(0), m_peer(), m_running(false), m_numOfPackets(0),
    m_sizeOfPackets(0), m_timePeriod(0), m_maxSizeOfPackets(0), m_minSizeOfPackets(0),
    m_rateOfStream(0), m_packetSize(0), m_socketForUDP(0), m_sizeOfRequestPackets(0),
    m_cumulativeSize(0), m_packetCountForUDP(0), adsForUDP(), m_numOfIncrease(0), m_numOfNonIncrease(0),
    m_thresholdForTrendJudgement(0), m_localFleetCount(0), m_defaultPropagationDelay(0)
{

}

PathloadClientApp::~PathloadClientApp()
{
    m_socketForTCP = 0;
    m_socketForUDP = 0;
}

void PathloadClientApp::Setup(Address address, Address addressForUDP, uint32_t packetSize)
{
    
    NS_LOG_UNCOND("PathloadClientApp :: Setup");

    m_peer = address;
    adsForUDP = addressForUDP;

    m_packetSize = packetSize;

    m_numOfPackets = 100;
    m_timePeriod = 100;

    m_defaultPropagationDelay = 50; // (ms)

    // Set maximum and minimum value for size of packets (bytes)
    m_maxSizeOfPackets = 1500;
    m_minSizeOfPackets = 200;

    if (packetSize < m_minSizeOfPackets && packetSize > m_maxSizeOfPackets) 
    {
        m_sizeOfPackets = (m_maxSizeOfPackets + m_minSizeOfPackets) / 2;
    }

    else 
    {
        m_sizeOfPackets = packetSize;
    }

    // Calculate expected stream rate using number of packets, size of packets, and sending period of each packet (unit of rate is 'bps')
    m_rateOfStream = (double)(m_numOfPackets * m_sizeOfPackets * 8) / ((double)(m_timePeriod * m_numOfPackets) / pow(10, 6));

    // Expected stream rate is represented to 'Mbps' unit
    NS_LOG_UNCOND("PathloadClient :: Setup :: Initially Expected Stream Rate :: " << (double)m_rateOfStream / pow(10, 6) << " Mbps" << " Checked At Client");
}

void PathloadClientApp::RxDrop(Ptr<const Packet> p)
{
    NS_LOG_UNCOND("PathloadClientApp :: RxDrop :: At " << Simulator::Now().GetSeconds());
}

void PathloadClientApp::StartApplication(void)
{
    NS_LOG_UNCOND("PathloadClientApp :: StartApplication");

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
    // NS_LOG_UNCOND("PathloadClientApp :: RequestNextPacket");
}

void PathloadClientApp::RxCallbackForUDP(Ptr<Socket> socket)
{
    // NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP");

    Ptr<Packet> packet;

    while ((packet = socket->Recv()))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }

        m_cumulativeSize += packet->GetSize();

        if (m_cumulativeSize >= m_sizeOfPackets)
        {
            m_packetCountForUDP++;

            m_arrivalTimeArray.push_back(Simulator::Now().GetMilliSeconds());

            m_cumulativeSize = 0;
        }
    }

    // NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP :: Current Time Check :: " << Simulator::Now().GetMilliSeconds());

    if (m_packetCountForUDP > m_numOfPackets - 1)
    {
        m_packetCountForUDP = 0;

        m_localFleetCount++;

        NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP :: Local Fleet Count ? :: " << m_localFleetCount);

        NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP :: " << m_localFleetCount << "th Receiving Round End");

        for (uint32_t index = 0; index < m_arrivalTimeArray.size(); index++)
            {
                // NS_LOG_UNCOND("Arrival Time of " << (index + 1) << "th Packet :: " << m_arrivalTimeArray[index]);
            }

        // NS_LOG_UNCOND("Arrival :: " << m_arrivalTimeArray.size());

        for (uint32_t index = 0; index < m_arrivalTimeArray.size(); index++)
            {
                // if (m_depatureTimeArray[index] > m_arrivalTimeArray[index])
                // {
                //     m_oneWayDelayArray.push_back(m_defaultPropagationDelay);
                // }

                // else
                // {
                    m_oneWayDelayArray.push_back(m_arrivalTimeArray[index] - m_depatureTimeArray[index]);
                // }
            }

        for (uint32_t indexTo = 0; indexTo < m_oneWayDelayArray.size(); indexTo++)
            {
                NS_LOG_UNCOND("PathloadClientApp :: RxCallbackForUDP :: One-Way Delay Measurement of :: " << (indexTo + 1) << "th Packet :: " << m_oneWayDelayArray[indexTo]);
            }

            m_arrivalTimeArray.clear();

            m_oneWayDelayArray.clear();
    }
}

void PathloadClientApp::HandleProbing(void)
{
    // NS_LOG_UNCOND("PathloadClientApp :: HandleProbing");
}

void PathloadClientApp::SendRequest(void)
{
    // NS_LOG_UNCOND("PathloadClientApp :: SendRequest");

    // uint32_t bytesReq = m_sizeOfPackets;

    // while (bytesReq > 0)
    // {
    //     uint32_t toSend = min(bytesReq, m_sizeOfPackets);
    //     Ptr<Packet> packet = Create<Packet>(toSend);

    //     int actual = m_socketForUDP->Send(packet);

    //     if ((unsigned)actual != toSend)
    //     {
    //         break;
    //     }
    // }
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
    std::string animFile = "dash-animation.xml" ;  // Name of file for animation output
    LogComponentEnable("PathloadApplication", LOG_LEVEL_ALL);

    PointToPointHelper bottleNeck;
    bottleNeck.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    bottleNeck.SetChannelAttribute("Delay", StringValue("50ms"));
    bottleNeck.SetQueue("ns3::DropTailQueue", "Mode", StringValue("QUEUE_MODE_PACKETS"));

    PointToPointHelper pointToPointLeaf;
    pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    pointToPointLeaf.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointDumbbellHelper dB(10, pointToPointLeaf, 10, pointToPointLeaf, bottleNeck);

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

    OnOffHelper onoff1("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(1), port));
    onoff1.SetConstantRate(DataRate("50000kb/s"));

    ApplicationContainer cbrApp1 = onoff1.Install(dB.GetLeft(1));
    cbrApp1.Start(Seconds(4.0));
    cbrApp1.Stop(Seconds(8.0));

    PacketSinkHelper cbrSink1("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    cbrApp1 = cbrSink1.Install(dB.GetRight(1));
    cbrApp1.Start(Seconds(4.0));
    cbrApp1.Stop(Seconds(8.0));

    // OnOffHelper onoff2("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(2), port));
    // onoff2.SetConstantRate(DataRate("15000kb/s"));

    // ApplicationContainer cbrApp2 = onoff2.Install(dB.GetLeft(2));
    // cbrApp2.Start(Seconds(20.0));
    // cbrApp2.Stop(Seconds(100.0));

    // PacketSinkHelper cbrSink2("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    // cbrApp2 = cbrSink2.Install(dB.GetRight(2));
    // cbrApp2.Start(Seconds(20.0));
    // cbrApp2.Stop(Seconds(100.0));

    // OnOffHelper onoff3("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(3), port));
    // onoff3.SetConstantRate(DataRate("15000kb/s"));

    // ApplicationContainer cbrApp3 = onoff3.Install(dB.GetLeft(3));
    // cbrApp3.Start(Seconds(35.0));
    // cbrApp3.Stop(Seconds(100.0));

    // PacketSinkHelper cbrSink3("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    // cbrApp3 = cbrSink3.Install(dB.GetRight(3));
    // cbrApp3.Start(Seconds(35.0));
    // cbrApp3.Stop(Seconds(100.0));

    // OnOffHelper onoff4("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(4), port));
    // onoff4.SetConstantRate(DataRate("15000kb/s"));

    // ApplicationContainer cbrApp4 = onoff4.Install(dB.GetLeft(4));
    // cbrApp4.Start(Seconds(50.0));
    // cbrApp4.Stop(Seconds(100.0));

    // PacketSinkHelper cbrSink4("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    // cbrApp4 = cbrSink4.Install(dB.GetRight(4));
    // cbrApp4.Start(Seconds(50.0));
    // cbrApp4.Stop(Seconds(100.0));

    // OnOffHelper onoff5("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(5), port));
    // onoff5.SetConstantRate(DataRate("15000kb/s"));

    // ApplicationContainer cbrApp5 = onoff5.Install(dB.GetLeft(5));
    // cbrApp5.Start(Seconds(65.0));
    // cbrApp5.Stop(Seconds(100.0));

    // PacketSinkHelper cbrSink5("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    // cbrApp5 = cbrSink5.Install(dB.GetRight(5));
    // cbrApp5.Start(Seconds(65.0));
    // cbrApp5.Stop(Seconds(100.0));

    Address TCPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), serverPortTCP));
    Address TCPServerAddress(InetSocketAddress(dB.GetLeftIpv4Address(9), serverPortTCP));
    Address UDPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), serverPortUDP));
    Address UDPServerAddress(InetSocketAddress(dB.GetRightIpv4Address(9), serverPortUDP));

    // Pathload server
    Ptr<PathloadServerApp> serverApp1 = CreateObject<PathloadServerApp>();
    serverApp1->Setup(TCPBindAddress, UDPServerAddress, 800);
    dB.GetLeft(9)->AddApplication(serverApp1);
    serverApp1->SetStartTime(Seconds(0.0));
    serverApp1->SetStopTime(Seconds(10.0));

    // Pathload client
    Ptr<PathloadClientApp> clientApp1 = CreateObject<PathloadClientApp>();
    clientApp1->Setup(TCPServerAddress, UDPBindAddress, 800);
    dB.GetRight(9)->AddApplication(clientApp1);
    clientApp1->SetStartTime(Seconds(0.0));
    clientApp1->SetStopTime(Seconds(10.0));

    // Set the bounding box for animation
    dB.BoundingBox (1, 1, 100, 100);
 
    // Create the animation object and configure for specified output
    AnimationInterface anim (animFile);
    anim.EnablePacketMetadata (); // Optional
    anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (3)); // Optional 



    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(10.0));

    Simulator::Run();
    std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
    Simulator::Destroy();

    return 0;
}