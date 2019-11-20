/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
 /*
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation;
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  *
  * Author: George F. Riley<riley@ece.gatech.edu>
  */
 
 #include <iostream>
 
 #include "ns3/address.h"
 #include "ns3/on-off-helper.h"
 #include "ns3/core-module.h"
 #include "ns3/network-module.h"
 #include "ns3/internet-module.h"
 #include "ns3/point-to-point-module.h"
 #include "ns3/netanim-module.h"
 #include "ns3/applications-module.h"
 #include "ns3/point-to-point-layout-module.h"
 #include "ns3/packet-sink-helper.h"
 
 using namespace ns3;
 
 int main (int argc, char *argv[])
 {
   Config::SetDefault ("ns3::OnOffApplication::PacketSize", UintegerValue (512));
   Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue ("3.6Mb/s"));
 
   uint32_t    nLeftLeaf = 2;
   uint32_t    nRightLeaf = 2;
   uint32_t    nLeaf = 0; // If non-zero, number of both left and right
   uint32_t    serverPort = 1000;

   std::string animFile = "dumbbell-animation.xml" ;  // Name of file for animation output
 
   CommandLine cmd;
   cmd.AddValue ("nLeftLeaf", "Number of left side leaf nodes", nLeftLeaf);
   cmd.AddValue ("nRightLeaf","Number of right side leaf nodes", nRightLeaf);
   cmd.AddValue ("nLeaf",     "Number of left and right side leaf nodes", nLeaf);
   cmd.AddValue ("animFile",  "File Name for Animation Output", animFile);
 
   cmd.Parse (argc,argv);
   if (nLeaf > 0)
     {
       nLeftLeaf = nLeaf;
       nRightLeaf = nLeaf;
     }
 
   // Create the point-to-point link helpers
   PointToPointHelper pointToPointRouter;
   pointToPointRouter.SetDeviceAttribute  ("DataRate", StringValue ("10Mbps"));
   pointToPointRouter.SetChannelAttribute ("Delay", StringValue ("1ms"));
   PointToPointHelper pointToPointLeaf;
   pointToPointLeaf.SetDeviceAttribute    ("DataRate", StringValue ("100Mbps"));
   pointToPointLeaf.SetChannelAttribute   ("Delay", StringValue ("1ms"));
 
   PointToPointDumbbellHelper d (nLeftLeaf, pointToPointLeaf,
                                 nRightLeaf, pointToPointLeaf,
                                 pointToPointRouter);
 
   // Install Stack
   InternetStackHelper stack;
   d.InstallStack (stack);
 
   // Assign IP Addresses
   d.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"),
                          Ipv4AddressHelper ("10.2.1.0", "255.255.255.0"),
                          Ipv4AddressHelper ("10.3.1.0", "255.255.255.0"));
 
   //서버 주소 설정 에러나서 두개로 설정함. 
   Address serverAddress (InetSocketAddress (d.GetLeftIpv4Address (1), serverPort));
   AddressValue remoteAddress (InetSocketAddress (d.GetLeftIpv4Address (1), serverPort));
   PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", serverAddress);

   // 일단... TCP소켓 트래픽을 발생시킨다...on off방식.. 시간은 랜덤 변수로. 
   OnOffHelper clientHelper ("ns3::TcpSocketFactory", Address ());
   clientHelper.SetAttribute ("OnTime", StringValue ("ns3::UniformRandomVariable"));
   clientHelper.SetAttribute ("OffTime", StringValue ("ns3::UniformRandomVariable"));
   
   ApplicationContainer clientApps;
   ApplicationContainer serverApps;
   serverApps = sinkHelper.Install(d.GetLeft(1));

   //리모트 주소가 곧 목적지 주소네. 위부터 0, 1... 이 순서로. 
   clientHelper.SetAttribute ("Remote", remoteAddress);
   clientApps.Add (clientHelper.Install (d.GetRight (1)));
   

  //========위에는 경쟁 트래픽 발생시킨 거임. 이제 프로브 패킷을 발생시켜야되========//
   Address udpServerAddress (InetSocketAddress (d.GetLeftIpv4Address(0), serverPort));

   UdpServerHelper udpServer(serverPort);
   UdpClientHelper udpClient(udpServerAddress, serverPort);

   udpClient.SetAttribute("MaxPackets", UintegerValue(256));
   udpClient.SetAttribute("Interval", TimeValue (Seconds (0.001)));
   udpClient.SetAttribute("PacketSize", UintegerValue(750));

   ApplicationContainer udpClientApps = udpClient.Install(d.GetRight(0));
   ApplicationContainer udpServerApps = udpServer.Install(d.GetLeft(0));

   




   


   udpServerApps.Start(Seconds (0.0));
   udpServerApps.Stop(Seconds (1.0));
   udpClientApps.Start(Seconds (0.1));
   udpClientApps.Stop(Seconds (0.9));

   serverApps.Start (Seconds (0.0));
   serverApps.Stop (Seconds (10.0));
   clientApps.Start (Seconds (0.1));
   clientApps.Stop (Seconds (9.0));

   // Set the bounding box for animation
   d.BoundingBox (1, 1, 100, 100);
 
   // Create the animation object and configure for specified output
   AnimationInterface anim (animFile);
   anim.EnablePacketMetadata (); // Optional
   anim.EnableIpv4L3ProtocolCounters (Seconds (0), Seconds (10)); // Optional
   
   // Set up the actual simulation
   Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
 
   Simulator::Run ();
   std::cout << "Animation Trace file created:" << animFile.c_str ()<< std::endl;
   Simulator::Destroy ();
   return 0;
 }