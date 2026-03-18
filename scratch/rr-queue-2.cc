#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/dsr-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/applications-module.h"
#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/string.h"
#include "ns3/udp-echo-helper.h"
#include "ns3/ssid.h"
#include "ns3/uinteger.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/rr-queue-disc.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac.h"
#include "ns3/qos-txop.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include "ns3/queue-disc.h"
#include <queue>
 
 
using namespace ns3;
using namespace std;
 
NS_LOG_COMPONENT_DEFINE("WifiSimpleAdhocGrid");
std::ofstream queueFile("mac_queue_log.txt", std::ios::out);

void MacQueueTrace (std::string context, uint32_t oldValue, uint32_t newValue)
{
    if (queueFile.is_open()) {
        queueFile << Simulator::Now ().GetSeconds () << "\t" 
                  << context << "\t" 
                  << newValue << std::endl;
    }
}
 
int main(int argc, char* argv[])
{
	bool udp{true};
	std::string phyMode("DsssRate2Mbps");
	double distance = 200;  	// m
	uint32_t packetSize = 1024; // bytes
	uint32_t nodes = 3;
	bool verbose = false;
	bool tracing = false;
 
	CommandLine cmd(__FILE__);
	cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
	cmd.AddValue("distance", "distance (m)", distance);
	cmd.AddValue("packetSize", "size of application packet sent", packetSize);
	cmd.AddValue("udp", "UDP if set to 1, TCP otherwise", udp);
	cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
	cmd.AddValue("tracing", "turn on ascii and pcap tracing", tracing);
	cmd.AddValue("nodes", "number of nodes", nodes);
	cmd.Parse(argc, argv);
 
	// Open CSV file for output
	std::ofstream outFile("simulation_results_RR.csv", std::ios::out);
	if (!outFile.is_open()) {
    	std::cerr << "Error opening output file!" << std::endl;
    	return 1;
	}
	// Write CSV header, thêm cột OfferedLoad_Mbps
	outFile << "n_packets,FlowID,SourceAddress,DestinationAddress,TxBytes,RxBytes,TxPackets,RxPackets,start time, stop time, duration, Throughput_Mbps,OfferedLoad_Mbps\n";
	outFile << std::fixed << std::setprecision(6); // Set precision for throughput
 
	// Loop over n_packets from 1 to 244
	for (uint32_t n_packets = 1; n_packets <= 244; ++n_packets)
	{
    	double interval = 1.0 / n_packets; // Calculate interval based on n_packets
 
    	// Reset simulation state
    	Simulator::Destroy(); // Clean up previous simulation
 
    	// Create nodes
    	NodeContainer c;
    	c.Create(nodes);
 
    	// Configure WiFi
    	Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
   	 
    	// --- QUEUE CONFIGURATION: Only set MaxSize for the default RR queue ---
    	// WifiMacQueue is implicitly RR, so we only need to configure its attributes
    	// The path depends on the specific WifiMac being used. For AdhocWifiMac,
    	// it usually goes through a Txop (like DcaTxop or EdcaTxopN for QoS).
    	// The most general path is: /NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/*/Queue/MaxSize
    	// Or for simpler cases, just setting default for ns3::WifiMacQueue is often enough if it applies
    	// to all instances, which it does for MaxSize.
    	//Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize("100p")));
    	// --- END QUEUE CONFIGURATION ---
 
    	WifiHelper wifi;
    	if (verbose) {
        	WifiHelper::EnableLogComponents();
    	}
 
    	YansWifiPhyHelper wifiPhy;
    	wifiPhy.Set("RxGain", DoubleValue(0));
    	wifiPhy.Set("TxPowerStart", DoubleValue(16.0));
    	wifiPhy.Set("TxPowerEnd", DoubleValue(16.0));
    	wifiPhy.Set("TxPowerLevels", UintegerValue(1));
    	wifiPhy.Set("RxSensitivity", DoubleValue(-76.9));
    	wifiPhy.Set("CcaEdThreshold", DoubleValue(-90.5));
    	wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
 
    	YansWifiChannelHelper wifiChannel;
    	wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    	wifiChannel.AddPropagationLoss("ns3::TwoRayGroundPropagationLossModel",
                                   	"Frequency", DoubleValue(2.4e9),
                                   	"SystemLoss", DoubleValue(1),
                                   	"MinDistance", DoubleValue(0.5),
                                   	"HeightAboveZ", DoubleValue(1.5));
    	wifiPhy.SetChannel(wifiChannel.Create());
 
    	WifiMacHelper wifiMac;
        wifi.SetStandard(WIFI_STANDARD_80211n); 

        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(phyMode),
                                 "ControlMode", StringValue(phyMode));

        wifiMac.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(true));
        Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize("10p")));

        NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);
        
        for (uint32_t i = 0; i < devices.GetN(); ++i) {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
            Ptr<WifiMac> mac = wifiDev->GetMac();
            
            PointerValue ptr;
            
            // Cấu hình cho cả Voice (VO) và Video (VI) giống hệt nhau
            const std::vector<AcIndex> acs = {AC_VO, AC_VI, AC_BE, AC_BK};
            for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        Ptr<WifiMac> mac = wifiDev->GetMac();
        
        // Luồng 1 (VO): Ưu tiên cao - Đường nằm TRÊN
        mac->GetQosTxop(AC_VO)->SetAifsn(2); 
        mac->GetQosTxop(AC_VO)->SetMinCw(7);

        // Luồng 2 (VI): Ưu tiên thấp hơn - Đường nằm DƯỚI
        mac->GetQosTxop(AC_VI)->SetAifsn(4);
        mac->GetQosTxop(AC_VI)->SetMinCw(15);
    }
        }

        	// Set up mobility
    	MobilityHelper mobility;
    	Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    	positionAlloc->Add(Vector(0.0, 0.0, 0.0));	// Node 0
    	positionAlloc->Add(Vector(distance, 0.0, 0.0)); // Node 1
    	positionAlloc->Add(Vector(distance * 2, 0.0, 0.0)); // Node 2
    	mobility.SetPositionAllocator(positionAlloc);
    	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    	mobility.Install(c);
 
    	// Set up internet stack and routing
    	InternetStackHelper internet;
    	AodvHelper aodv;
    	internet.SetRoutingHelper(aodv);
    	internet.Install(c);
   	 
    	TrafficControlHelper tch;
tch.SetRootQueueDisc("ns3::RRQueueDisc",
                 	"Queues", UintegerValue(4),
                 	"MaxSize", QueueSizeValue(QueueSize("100p")));
QueueDiscContainer qdiscs = tch.Install(devices);
 
    	// Assign IP addresses
    	Ipv4AddressHelper ipv4;
    	NS_LOG_INFO("Assign IP Addresses.");
    	ipv4.SetBase("10.1.1.0", "255.255.255.0");
    	Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
 
    	// Set up applications
    	uint16_t port = 9;
    	PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    	ApplicationContainer sinkApp = sinkHelper.Install(c.Get(0));
    	sinkApp.Start(Seconds(0.0));
    	sinkApp.Stop(Seconds(100.0));
 
    	// Configure source nodes (1 to N-1) to send to node 0
	// Configure source nodes (1 to N-1) to send to node 0
    uint32_t numSources = c.GetN() - 1;
    for (uint32_t n = 1; n < c.GetN(); ++n) {
        UdpClientHelper client(interfaces.GetAddress(0), port);
        client.SetAttribute("MaxPackets", UintegerValue(n_packets * 100));
        client.SetAttribute("Interval", TimeValue(Seconds(interval)));
        client.SetAttribute("PacketSize", UintegerValue(packetSize));
        
        ApplicationContainer sourceApp = client.Install(c.Get(n));
        
        // Tạo đường dẫn đến Socket của Node để dán nhãn
        std::ostringstream oss;
        oss << "/NodeList/" << c.Get(n)->GetId() << "/$ns3::Ipv4L3Protocol/SocketList/*";
        
        if (n == 1) {
            // Dán nhãn luồng 1 là Voice (Ưu tiên nhất)
            Config::Set(oss.str() + "/Priority", UintegerValue(7)); 
            sourceApp.Start(Seconds(1.0)); 
        } else {
            // Dán nhãn luồng 2 là Video (Ưu tiên nhì)
            Config::Set(oss.str() + "/Priority", UintegerValue(4)); 
            sourceApp.Start(Seconds(1.01)); 
        }
        sourceApp.Stop(Seconds(100.0));
    }
 
    	// Calculate total offered load for this iteration
    	double singleFlowRateMbps = (double)packetSize * 8.0 / interval / 1e6;
    	double totalOfferedLoadMbps = singleFlowRateMbps * numSources;
 
    	// Enable tracing
    	wifiPhy.EnablePcap("rr_trace", devices);
    	if (tracing) {
        	AsciiTraceHelper ascii;
        	wifiPhy.EnableAsciiAll(ascii.CreateFileStream("rr_q_" + std::to_string(n_packets) + ".tr"));
    	}
 
    	// Set up flow monitor
    	Ptr<FlowMonitor> flowMonitor;
    	FlowMonitorHelper flowHelper;
    	flowMonitor = flowHelper.InstallAll();
 
    	// Run simulation
    	Simulator::Stop(Seconds(110.0));


    if (queueFile.is_open()) {
        queueFile << "Time\tNode\tQueueSize" << std::endl;
    }


    Config::Connect ("/NodeList/*/$ns3::WifiNetDevice/Mac/*Queue/NPackets", 
                     MakeCallback (&MacQueueTrace));
                     
    	Simulator::Run();
 
    	// Collect and output results to CSV
    	flowMonitor->CheckForLostPackets();
    	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    	std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();
 
    	for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter) {
        	Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);
        	double duration = iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstRxPacket.GetSeconds();
        	float throughput = iter->second.rxBytes * 8.0 / duration / 1e6;
       	 
        	outFile << n_packets << ","
                	<< iter->first << ","
                	<< t.sourceAddress << ","
                	<< t.destinationAddress << ","
                	<< iter->second.txBytes << ","
                	<< iter->second.rxBytes << ","
                	<< iter->second.txPackets << ","
                	<< iter->second.rxPackets << ","
                	<< iter->second.timeFirstRxPacket.GetSeconds() << ","
                	<< iter->second.timeLastRxPacket.GetSeconds() << ","
                	<< duration << ","
                	<< throughput << ","
                	<< totalOfferedLoadMbps << "\n";
    	}
 
    	Simulator::Destroy();
	}
 
	outFile.close();
	return 0;
}









