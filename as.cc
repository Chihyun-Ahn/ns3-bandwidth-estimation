#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("DashApplication");

//================================================================
// SERVER APPLICATION
//================================================================

class DashServerApp : public Application
{
public:
	DashServerApp();
	virtual ~DashServerApp();
	void Setup(Address address, Address myudp, uint32_t packetSize);

private:
	virtual void StartApplication(void);
	virtual void StopApplication(void);

	void RxCallback(Ptr<Socket> socket);
	void RxCallbackUDP(Ptr<Socket> socket);
	void TxCallback(Ptr<Socket> socket, uint32_t txSpace);
	bool ConnectionCallback(Ptr<Socket> s, const Address& ad);
	void AcceptCallback(Ptr<Socket> s, const Address& ad);
	void SendData();
	void SendRequest(void);
	void UpdatePacket(void);
	bool m_connected;
	Ptr<Socket> m_socket;
	Ptr<Socket> p_socket;
	Ptr<Socket> m_peer_socket;

	Address ads;
	Address adsUdp;
	Address m_peer_address;
	uint32_t m_remainingData;
	EventId m_sendEvent;
	EventId sendprobe;
	uint32_t m_packetSize;
	uint32_t m_packetCount;
	uint32_t p_GapTime;
	Time p_prePacketTime;
};

DashServerApp::DashServerApp() :
	m_connected(false), m_socket(0), p_socket(0), m_peer_socket(0),
	ads(), adsUdp(), m_peer_address(), m_remainingData(0),
	m_sendEvent(), sendprobe(), m_packetSize(0), m_packetCount(0), p_GapTime(0),p_prePacketTime()
{

}

DashServerApp::~DashServerApp()
{
	m_socket = 0;
	p_socket = 0;
}

void DashServerApp::Setup(Address address, Address myudp, uint32_t packetSize)
{
	ads = address;
	adsUdp = myudp;
	m_packetSize = packetSize;
}

void DashServerApp::StartApplication()
{
	m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
	m_socket->Bind(ads);
	m_socket->Listen();
	m_socket->SetAcceptCallback(
		MakeCallback(&DashServerApp::ConnectionCallback, this),
		MakeCallback(&DashServerApp::AcceptCallback, this));
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
	if (sendprobe.IsRunning()) {
		Simulator::Cancel(sendprobe);
	}
}

bool DashServerApp::ConnectionCallback(Ptr<Socket> socket, const Address& adss)
{
	NS_LOG_UNCOND("Server : connection callback ");

	m_connected = true;
	return true;
}

void DashServerApp::AcceptCallback(Ptr<Socket> socket, const Address& adss)
{
	NS_LOG_UNCOND("Server : accept callback ");

	m_peer_address = adss;
	m_peer_socket = socket;

	socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback, this));
	socket->SetSendCallback(MakeCallback(&DashServerApp::TxCallback, this));

	p_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId()); //  durl
	if (~p_socket->Bind()) { 
		NS_LOG_UNCOND("Server : UDP Bind " << adsUdp);
		if (~p_socket->Connect(adsUdp)) {
			p_socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallbackUDP, this));
			NS_LOG_UNCOND("Server : UDP Connect " << adsUdp);

			p_GapTime = 2048;
			Simulator::ScheduleNow(&DashServerApp::UpdatePacket, this);
		}
	}
	else {
		NS_LOG_UNCOND("Server : UDP Bind Fail");
	}
}
void DashServerApp::RxCallbackUDP(Ptr<Socket> socket) {

}
void DashServerApp::UpdatePacket(void) {
	if (p_GapTime) {
		Time tNext(MilliSeconds(p_GapTime));
		sendprobe = Simulator::Schedule(tNext, &DashServerApp::SendRequest, this);

		p_GapTime /= 2;
	}
	else {
		Simulator::Cancel(sendprobe);
	}
}
void DashServerApp::SendRequest(void)
{
	//NS_LOG_UNCOND("Gap << " << Simulator::Now().GetMilliSeconds() - p_prePacketTime.GetMilliSeconds());
	uint32_t bytesReq = 1200;
	Ptr<Packet> packet = Create<Packet>((uint8_t*)&bytesReq, 1200);
	p_socket->SendTo(packet, 0, adsUdp);	
	Simulator::ScheduleNow(&DashServerApp::UpdatePacket, this);
	p_prePacketTime = Simulator::Now();
}

void DashServerApp::RxCallback(Ptr<Socket> socket)
{
	Address ads;
	Ptr<Packet> pckt = socket->RecvFrom(ads);

	if (ads == m_peer_address)
	{
		uint32_t data = 0;
		pckt->CopyData((uint8_t*)&data, 4);

		m_remainingData = data;
		m_packetCount = 0;

		//SendData();
		p_GapTime = 256;
		Simulator::ScheduleNow(&DashServerApp::UpdatePacket, this);
	}
}

void DashServerApp::TxCallback(Ptr<Socket> socket, uint32_t txSpace)
{
	if (m_connected)
		Simulator::ScheduleNow(&DashServerApp::SendData, this);
}

void DashServerApp::SendData()
{
	while (m_remainingData > 0)
	{
		// Time to send more
		uint32_t toSend = min(m_packetSize, m_remainingData);
		Ptr<Packet> packet = Create<Packet>((uint8_t*)&toSend, 4);

		int actual = m_peer_socket->Send(packet);

		if (actual > 0)
		{
			m_remainingData -= toSend;
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

class DashClientApp : public Application
{
public:

	DashClientApp();
	virtual ~DashClientApp();
	enum
	{
		MAX_BUFFER_SIZE = 30000
	}; // 30 seconds

	void Setup(Address address, Address address1, uint32_t chunkSize, uint32_t numChunks, string algorithm);

private:
	virtual void StartApplication(void);
	virtual void StopApplication(void);

	// Request
	void SendRequest(void);
	void RequestNextChunk(void);
	void GetStatistics(void);
	void RxCallback(Ptr<Socket> socket);

	void RxCallbackUDP(Ptr<Socket> socket);
	// Buffer Model
	void ClientBufferModel(void);
	void GetBufferState(void);
	void RxDrop(Ptr<const Packet> p);

	// Rate Adaptation Algorithm

	void Proposed(void);
	void SendData();
	// Another
	uint32_t GetIndexByBitrate(uint32_t bitrate);

	Ptr<Socket> m_socket;
	Ptr<Socket> p_socket;
	Address m_peer;
	Address ads;
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
	EventId Recvprobe;
	uint32_t m_downloadDuration;
	double m_throughput;
	uint32_t m_prevBitrate;
	uint32_t m_nextBitrate;
	uint32_t m_algorithm;
	uint32_t m_numOfSwitching;
	Time recvpreb;
	clock_t pretime;
	// Proposed

};

DashClientApp::DashClientApp() :
	m_socket(0), p_socket(0), m_peer(), ads(), m_chunkSize(0), m_numChunks(0), m_chunkCount(0),
	m_bufferSize(0), m_bufferPercent(0), m_bpsAvg(0), m_bpsLastChunk(0),
	m_fetchEvent(), m_statisticsEvent(), m_running(false),
	m_comulativeSize(0), m_lastRequestedSize(0), m_requestTime(), m_sessionData(0), m_sessionTime(0),
	m_bufferEvent(), m_bufferStateEvent(), Recvprobe(), m_downloadDuration(0), m_throughput(0.0),
	m_prevBitrate(0), m_nextBitrate(0), m_algorithm(0), m_numOfSwitching(0), recvpreb(), pretime(0)
{

}

DashClientApp::~DashClientApp()
{
	m_socket = 0;
	p_socket = 0;
}

void DashClientApp::Setup(Address address, Address myUDP, uint32_t chunkSize,
	uint32_t numChunks, string algorithm)
{
	m_peer = address;
	ads = myUDP;
	m_chunkSize = chunkSize;
	m_numChunks = numChunks;

	//bitrate profile of the content
	m_bitrate_array.push_back(700000);
	m_bitrate_array.push_back(1400000);
	m_bitrate_array.push_back(2100000);
	m_bitrate_array.push_back(2800000);
	m_bitrate_array.push_back(3500000);
	m_bitrate_array.push_back(4200000);

	m_algorithm = 3;
}

void DashClientApp::RxDrop(Ptr<const Packet> p)
{
	NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
}

void DashClientApp::StartApplication(void)
{

	p_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
	uint32_t udpflag = p_socket->Bind(ads);
	if (~udpflag) {
		p_socket->SetRecvCallback(MakeCallback(&DashClientApp::RxCallbackUDP, this));
		NS_LOG_UNCOND("Client : UDP Bind " << ads);

		m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
		m_socket->TraceConnectWithoutContext("Drop", MakeCallback(&DashClientApp::RxDrop, this));
		m_running = true;
		m_socket->Bind();
		if (~m_socket->Connect(m_peer)) {
			NS_LOG_UNCOND("Client : TCP Connect");
		}
	}
	Time tNext("1s");
	//m_bufferStateEvent = Simulator::Schedule(tNext, &DashClientApp::GetBufferState, this);
}


void DashClientApp::RxCallbackUDP(Ptr<Socket> socket)
{
	
	
		
	Ptr<Packet> packet;
	uint32_t _size = 0;
	while ((packet = socket->Recv()))
	{
		if (packet->GetSize() == 0)
		{
			break;
		}
		_size += packet->GetSize();
	}
	NS_LOG_UNCOND("Packet Size : " << _size);
	NS_LOG_UNCOND("CurTime :" << Simulator::Now().GetMilliSeconds() << " Recv Gap  : " << Simulator::Now().GetMilliSeconds() - recvpreb.GetMilliSeconds());
	recvpreb = Simulator::Now();
	
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
			m_bufferPercent = (uint32_t)(m_bufferSize * 100) / MAX_BUFFER_SIZE;

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
	Proposed();
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
	uint32_t _packetsize = 1500;
	uint32_t _remain = 1500;
	while (_remain > 0)
	{
		// Time to send more
		uint32_t toSend = min(_remain, _packetsize);
		Ptr<Packet> packet = Create<Packet>(toSend);

		int actual = p_socket->Send(packet);

		if ((unsigned)actual != toSend)
		{
			break;
		}
	}
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
	NS_LOG_UNCOND("=======START=========== " << GetNode() << " =================");
	NS_LOG_UNCOND("Time: " << Simulator::Now().GetMilliSeconds() <<
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

	NS_LOG_UNCOND("=======BUFFER========== " << GetNode() << " =================");
	NS_LOG_UNCOND("Time: " << Simulator::Now().GetMilliSeconds() <<
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

int main(int argc, char* argv[])
{
	string algorithm;

    std::string animFile = "dash-animation.xml" ;  // Name of file for animation output

	algorithm = "Proposed";
	LogComponentEnable("DashApplication", LOG_LEVEL_ALL);

	PointToPointHelper bottleNeck;
	bottleNeck.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
	bottleNeck.SetChannelAttribute("Delay", StringValue("10ms"));
	//bottleNeck.SetQueue("ns3::DropTailQueue", "Mode", StringValue ("QUEUE_MODE_BYTES"));

	PointToPointHelper pointToPointLeaf;
	pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
	pointToPointLeaf.SetChannelAttribute("Delay", StringValue("1ms"));

	PointToPointDumbbellHelper dB(10, pointToPointLeaf, 10, pointToPointLeaf,
		bottleNeck);

	// install stack
	InternetStackHelper stack;
	dB.InstallStack(stack);

	// assign IP addresses
	dB.AssignIpv4Addresses(Ipv4AddressHelper("10.1.1.0", "255.255.255.0"),
		Ipv4AddressHelper("10.2.1.0", "255.255.255.0"),
		Ipv4AddressHelper("10.3.1.0", "255.255.255.0"));

	uint16_t TCPserverPort = 9999;
	uint16_t UDPserverPort = 8888;

	// Cross Traffic CBR

	// Scenario 1 (Freqeunt Bandwidth Change)
	 /*OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), UDPserverPort));
	 crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=50]"));
	 crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=50]"));
	 crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("1000kb/s")));
	 crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
	 ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
	 srcApp1.Start(Seconds(1.0));
	 srcApp1.Stop(Seconds(800.0));*/

	// Scenario 2 (Drastic Bandwidth Decrease -> Increase)
	OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(0), UDPserverPort));
	crossTrafficSrc1.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=2000]"));
	crossTrafficSrc1.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
	crossTrafficSrc1.SetAttribute("DataRate", DataRateValue(DataRate("3000kb/s")));
	crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue(512));
	ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
	srcApp1.Start(Seconds(0.0));
	srcApp1.Stop(Seconds(20.0));
	OnOffHelper crossTrafficSrc2("ns3::UdpSocketFactory", InetSocketAddress(dB.GetRightIpv4Address(0), UDPserverPort));
	crossTrafficSrc2.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=10]"));
	crossTrafficSrc2.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
	crossTrafficSrc2.SetAttribute("DataRate", DataRateValue(DataRate("1000kb/s")));
	crossTrafficSrc2.SetAttribute("PacketSize", UintegerValue(512));
	ApplicationContainer srcApp2 = crossTrafficSrc2.Install(dB.GetLeft(0));
	srcApp2.Start(Seconds(10.0));
	srcApp2.Stop(Seconds(20.0));

	// Scenario 3 (Drastic Bandwidth Decrease -> Maintain)
	// OnOffHelper crossTrafficSrc1("ns3::UdpSocketFactory", InetSocketAddress (dB.GetRightIpv4Address(0), serverPort));
	// crossTrafficSrc1.SetAttribute("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=500]"));
	// crossTrafficSrc1.SetAttribute("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
	// crossTrafficSrc1.SetAttribute("DataRate", DataRateValue (DataRate("2000kb/s")));
	// crossTrafficSrc1.SetAttribute("PacketSize", UintegerValue (512));
	// ApplicationContainer srcApp1 = crossTrafficSrc1.Install(dB.GetLeft(0));
	// srcApp1.Start(Seconds(300.0));
	// srcApp1.Stop(Seconds(800.0));


	Address TCPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), TCPserverPort));
	Address TCPServerAddress(InetSocketAddress(dB.GetLeftIpv4Address(9), TCPserverPort));
	Address UDPBindAddress(InetSocketAddress(Ipv4Address::GetAny(), UDPserverPort));
	Address UDPServerAddress(InetSocketAddress(dB.GetRightIpv4Address(9), UDPserverPort));

	// DASH server
	Ptr<DashServerApp> serverApp1 = CreateObject<DashServerApp>();
	serverApp1->Setup(TCPBindAddress, UDPServerAddress, 512);
	dB.GetLeft(9)->AddApplication(serverApp1);
	serverApp1->SetStartTime(Seconds(0.0));
	serverApp1->SetStopTime(Seconds(25.0));

	// DASH client
	Ptr<DashClientApp> clientApp1 = CreateObject<DashClientApp>();
	clientApp1->Setup(TCPServerAddress, UDPBindAddress, 2, 512, algorithm);
	dB.GetRight(9)->AddApplication(clientApp1);
	clientApp1->SetStartTime(Seconds(0.0));
	clientApp1->SetStopTime(Seconds(20.0));

    // Set the bounding box for animation
    dB.BoundingBox (1, 1, 100, 100);
 
    // Create the animation object and configure for specified output
    AnimationInterface anim (animFile);
    anim.EnablePacketMetadata (); // Optional
    anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (3)); // Optional 


	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	Simulator::Stop(Seconds(25.0));

	Simulator::Run();
    std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
	Simulator::Destroy();

	return 0;
}