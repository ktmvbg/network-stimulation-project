/* PLEASE, RUN THIS CODE IN THE LASTEST VERSION OF NS-3 (3.38)! */
/* Configure your CMakeList as follows to solve linker errors:
    build_example(
        NAME final.cc
        SOURCE_FILES final.cc
        LIBRARIES_TO_LINK
            ${libcore}
            ${libpoint-to-point}
            ${libcsma}
            ${libwifi}
            ${libinternet}
            ${libapplications}
    )
*/

/* RESOURCE DUMP
    https://netdb.cis.upenn.edu/rapidnet/doxygen/html/classns3_1_1_adhoc_wifi_mac.html
*/

/* DEV NOTE
    RegularWifiMac -> WifiMac since 2.26+
    fatal-error.h -> wifi-phy-state.h
    See src/wifi/model/wifi-*.h
    ‘Callback<[...],ns3::Ptr<ns3::Packet>, ns3::Ptr<ns3::WifiNetDevice>>’ to
‘Callback<[...],ns3::Ptr<const ns3::Packet>, ns3::Mac48Address, ns3::Mac48Address>’ 
    PacketSocketHelper
*/

// Include the necessary header files
#include "ns3/adhoc-wifi-mac.h" // Adhoc
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h" // The CA in CSMA/CA
#include "ns3/net-device.h"
#include "ns3/network-module.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ssid.h"
#include "ns3/wifi-mac.h" // Subclass of Adhoc
#include "ns3/wifi-module.h"
#include "ns3/wifi-phy-state.h" // For WifiPhysState
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <cstring>

using namespace ns3;

// Define some constants and parameters
#define NUM_NODES 30      // Number of nodes in the network
#define PACKET_SIZE 64    // Size of each packet in bytes
#define DATA_RATE "1Mbps" // Data rate of the channel

void
TxCallback(Ptr<OutputStreamWrapper> stream, std::string context, Ptr<const Packet> packet)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " " << context << " "
                         << packet->GetSize() << std::endl;

    std::cout << Simulator::Now().GetSeconds() << " " << context << " "
                         << packet->GetSize() << std::endl;
}

void
RxCallback(Ptr<OutputStreamWrapper> stream,
           std::string context,
           Ptr<const Packet> packet,
           const Address& address)
{
    std::cout << Simulator::Now().GetSeconds() << " " << context << " "
                         << packet->GetSize() << std::endl;

    *stream->GetStream() << Simulator::Now().GetSeconds() << " " << context << " "
                         << packet->GetSize() << std::endl;
}

void
TransmitPacket(Ptr<const Packet> packet, Mac48Address from, Mac48Address to)
{
    std::cout << "Received packet from " << from << " to " << to << std::endl;
}

// Packet recieve callback (app)
bool
ReceivePacket(Ptr<NetDevice> device,
              Ptr<const Packet> packet,
              uint16_t protocol,
              const Address& address)
{
    // Print some information about the received packet
    std::cout << "Node " << device->GetNode()->GetId() << " received a packet of size "
              << packet->GetSize() << " bytes from " << address << std::endl;
    return true;
}

int
main(int argc, char* argv[])
{
    // Create a node container and add nodes to it
    NodeContainer nodes;
    nodes.Create(NUM_NODES);
    // Create a wifi helper and set some attributes
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate1Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate1Mbps"));

    // Create a wifi MAC helper and set it to use an ad hoc network
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    // Create a wifi PHY helper and set it to use a YansWifiChannel
    YansWifiPhyHelper phy;
    Ptr<YansWifiChannel> chan = CreateObject<YansWifiChannel>();
    phy.SetChannel(chan);

    // Install the wifi devices on the nodes
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // Set the callback functions for packet transmission and reception
    for (uint32_t i = 0; i < devices.GetN(); i++)
    {
        // Cast to a WifiNetDevice, since it's easier to work with
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices.Get(i));

        // Diable RTS/CTS by setting very high threshold
        device->GetRemoteStationManager()->SetRtsCtsThreshold(314159);

        // Use the GetMac method to get a pointer to the MAC layer
        Ptr<WifiMac> mac = device->GetMac();

        // Setup packet transmission
        mac->SetForwardUpCallback(MakeCallback(&TransmitPacket));

        // Setup packet recieving
        device->SetReceiveCallback(MakeCallback(&ReceivePacket));
    }

    // Create a mobility helper and set the nodes to have random positions
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                  "X",
                                  StringValue("0.0"),
                                  "Y",
                                  StringValue("0.0"),
                                  "Rho",
                                  StringValue("ns3::UniformRandomVariable[Min=2|Max=30]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Create an internet stack helper and install it on the nodes
    InternetStackHelper internet;
    internet.Install(nodes);

    // Assign IP addresses to the devices
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        PacketSinkHelper sink("ns3::UdpSocketFactory", Address());
        sink.SetAttribute("Local", AddressValue(InetSocketAddress(interfaces.GetAddress(i), 80)));

        // Logging
        ApplicationContainer sinkApp = sink.Install(nodes.Get(i));
        AsciiTraceHelper asciiTraceHelper;
        Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("csma-ca-sink.log");
        sinkApp.Get(0)->TraceConnectWithoutContext(
            "Rx",
            MakeBoundCallback(&RxCallback, stream, "PacketSinkHelper"));
    }

    OnOffHelper onOffHelper("ns3::UdpSocketFactory", Address());
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onOffHelper.SetAttribute("DataRate", DataRateValue(DataRate(DATA_RATE)));
    onOffHelper.SetAttribute("PacketSize", UintegerValue(PACKET_SIZE));

    ApplicationContainer apps;

    // Create a OnOffHelper on each node and set it to send packets periodically
    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        // Select someone to target
        uint32_t target = i;
        while (target == i) // Avoid choosing the same device as source, because it is useless :)
        {
            target = rand() % interfaces.GetN();
        }

        // Get the address of the i-th interface
        Ipv4Address x = interfaces.GetAddress(i, 0);
        AddressValue remoteAddress(InetSocketAddress(x, 80));

        // set Remote attribute for OnOffHelper
        onOffHelper.SetAttribute("Remote", remoteAddress);

        // install OnOffApplication on the source node
        apps.Add(onOffHelper.Install(devices.Get(i)->GetNode()));
    }

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        AsciiTraceHelper asciiTraceHelper;
        Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream("csma-ca-addi.log");
        apps.Get(0)->TraceConnectWithoutContext(
            "Tx",
            MakeBoundCallback(&TxCallback, stream, "OnOffHelper"));
    }

    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(10.0));

    // Enable pcap tracing for the devices
    phy.EnablePcapAll("csma-ca");

    // Run the simulation
    Simulator::Stop(Seconds(11.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}