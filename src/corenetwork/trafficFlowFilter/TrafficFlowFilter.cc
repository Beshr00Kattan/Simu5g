//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

#include "corenetwork/trafficFlowFilter/TrafficFlowFilter.h"
#include <inet/common/IProtocolRegistrationListener.h>
#include <inet/common/ModuleAccess.h>
#include <inet/common/ProtocolTag_m.h>
#include <inet/networklayer/common/L3AddressResolver.h>
#include <inet/networklayer/ipv4/Ipv4Header_m.h>
#include <inet/transportlayer/udp/UdpHeader_m.h>
#include <inet/linklayer/ethernet/common/EthernetMacHeader_m.h>
#include <inet/linklayer/ethernet/common/Ethernet.h>
#include "common/LteControlInfo.h"

Define_Module(TrafficFlowFilter);

using namespace inet;
using namespace omnetpp;

void TrafficFlowFilter::initialize(int stage)
{
    // wait until all the IP addresses are configured
    if (stage != inet::INITSTAGE_NETWORK_LAYER)
        return;

    // get reference to the binder
    binder_ = getBinder();

    fastForwarding_ = par("fastForwarding");

    // reading and setting owner type
    ownerType_ = selectOwnerType(par("ownerType"));
    if (ownerType_ == PGW || ownerType_ == UPF)
    {
        std::string gwFullPath = binder_->getNetworkName() + "." + std::string(getParentModule()->getFullName());
        gateway_ = strcpy(new char[gwFullPath.length() + 1], gwFullPath.c_str());
    }
    else if(getParentModule()->hasPar("gateway") || getParentModule()->getParentModule()->hasPar("gateway"))
    {
        std::string gwFullPath = binder_->getNetworkName() + "." + getAncestorPar("gateway").stringValue();
        gateway_ = strcpy(new char[gwFullPath.length() + 1], gwFullPath.c_str());
    }

    // mec
    if(isBaseStation(ownerType_))
    {
        /*
          * @author Alessandro Noferi
          *
          */
        // obtain the IP address of externel MEC applications (if any)

        std::string extAddress = getAncestorPar("extMeAppsAddress").stringValue();
        if(strcmp(extAddress.c_str(), "") != 0)
        {
            std::vector<std::string> extAdd =  cStringTokenizer(extAddress.c_str(), "/").asVector();
            if(extAdd.size() != 2){
                throw cRuntimeError("TrafficFlowFilter::initialize - Bad extMeApps parameter. It must be like address/mask");
            }
            meAppsExtAddress_ = inet::L3AddressResolver().resolve(extAdd[0].c_str());
            meAppsExtAddressMask_ = atoi(extAdd[1].c_str());
            EV << "TrafficFlowFilter::initialize - emulation support:  meAppsExtAddres: " << meAppsExtAddress_.str()<<"/"<< meAppsExtAddressMask_<< endl;
        }
    }

    if(getParentModule()->hasPar("mecHost")){

        meHost = getParentModule()->par("mecHost").stringValue();
        if(isBaseStation(ownerType_) &&  strcmp(meHost.c_str(), ""))
        {
            std::stringstream meHostName;
            meHostName << meHost.c_str() << ".virtualisationInfrastructure";
            meHost = meHostName.str();
            meHostAddress = inet::L3AddressResolver().resolve(meHost.c_str());

            EV << "TrafficFlowFilter::initialize - meHost: " << meHost << " meHostAddress: " << meHostAddress.str() << endl;
        }
    }
    //end mec

    // register service processing IP-packets on the LTE Uu Link
    auto gateIn = gate("internetFilterGateIn");
    registerProtocol(LteProtocol::ipv4uu, gateIn, SP_INDICATION);
    registerProtocol(LteProtocol::ipv4uu, gateIn, SP_CONFIRM);

    // Real 3GPP Ethernet PDU Session work: resolve the real MacForwardingTable
    // this instance was given (Upf.ned sets this on the UPF's own
    // trafficFlowFilter only; ENB/GNB instances leave it at its "" default,
    // so macForwardingTable_ stays nullptr and the Ethernet branch below is
    // simply never reached for them). See doc/ethernet-pdu-session/07-tff-ethernet-branch.md.
    std::string macTablePath = par("macForwardingTableModule").stdstringValue();
    if (!macTablePath.empty())
        macForwardingTable_ = getModuleFromPar<MacForwardingTable>(par("macForwardingTableModule"), this);

    // Real 3GPP Ethernet PDU Session work: NO registerProtocol() for
    // ethernetMac here. Real frames reach ethernetFrameIn via a DIRECT wire
    // from the UPF's dedicated tsnEth interface (see Upf.ned), not through the
    // shared "nl" MessageDispatcher -- so no protocol registration is needed
    // or wanted (registering ethernetMac on "nl" collided with the one
    // inet::EthernetEncapsulation already makes there). See
    // doc/ethernet-pdu-session/15-real-switch-topology.md.
}

CoreNodeType TrafficFlowFilter::selectOwnerType(const char * type)
{
    EV << "TrafficFlowFilter::selectOwnerType - setting owner type to " << type << endl;
    if(strcmp(type,"ENODEB") == 0)
        return ENB;
    else if(strcmp(type,"GNODEB") == 0)
        return GNB;
    else if(strcmp(type,"PGW") == 0)
        return PGW;
    else if(strcmp(type,"UPF") == 0)
        return UPF;
    else if(strcmp(type, "UPF_MEC") == 0)
        return UPF_MEC;
    else
        error("TrafficFlowFilter::selectOwnerType - unknown owner type [%s]. Aborting...",type);

    // never gets here
    return ENB;
}

void TrafficFlowFilter::handleMessage(cMessage *msg)
{
    EV << "TrafficFlowFilter::handleMessage - Received Packet:" << endl;
    EV << "name: " << msg->getFullName() << endl;

    Packet* pkt = check_and_cast<Packet *>(msg);

    // Real 3GPP Ethernet PDU Session work: real Ethernet frames (no IP/UDP
    // shell -- see doc/ethernet-pdu-session/16-router-removal-unified-frame.md)
    // arrive on their own dedicated gate, DIRECT-wired to the UPF's tsnEth
    // interface in Upf.ned (no dispatcher, no protocol registration).
    // Dispatching on arrival gate here (rather than sniffing packet content)
    // keeps this branch and the untouched IP branch below completely
    // independent.
    if (msg->getArrivalGate() == gate("ethernetFrameIn")) {
        handleEthernetPacket(pkt);
        return;
    }

    // receive and read IP datagram
    // TODO: needs to be adapted for IPv6
    const auto& ipv4Header = pkt->peekAtFront<Ipv4Header>();
    const Ipv4Address &destAddr = ipv4Header->getDestAddress();
    const Ipv4Address &srcAddr = ipv4Header->getSrcAddress();
    pkt->addTagIfAbsent<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);
    pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ipv4);

    // TODO check for source and dest port number

    EV << "TrafficFlowFilter::handleMessage - Received datagram : " << pkt->getName() << " - src[" << srcAddr << "] - dest[" << destAddr << "]\n";

    // run packet filter and associate a flowId to the connection (default bearer?)
    // search within tftTable the proper entry for this destination
    TrafficFlowTemplateId tftId = findTrafficFlow(srcAddr, destAddr);   // search for the tftId in the binder

    // add control info to the normal ip datagram. This info will be read by the GTP-U application
    auto tftInfo = pkt->addTag<TftControlInfo>();
    tftInfo->setTft(tftId);

    EV << "TrafficFlowFilter::handleMessage - setting tft=" << tftId << endl;

    // send the datagram to the GTP-U module
    send(pkt,"gtpUserGateOut");
}

TrafficFlowTemplateId TrafficFlowFilter::findTrafficFlow(L3Address srcAddress, L3Address destAddress)
{
    // check whether the destination address is a (simulated) MEC host's address
    if (binder_->isMecHost(destAddress))
    {
        // check if the destination belongs to another core network (for multi-operator scenarios)
        std::string destGw = binder_->getNetworkName() + "." + (inet::L3AddressResolver().findHostWithAddress(destAddress))->getAncestorPar("gateway").stdstringValue();
        if (strcmp(gateway_, destGw.c_str()) != 0)
        {
            // the destination is a MEC host under a different core network, send the packet to the gateway
            return -1;
        }

        EV << "TrafficFlowFilter::findTrafficFlow - returning flowId (-3) for tunneling to " << destAddress.str() << endl;
        return -3;
    }
    // emulation mode
    else if (!meAppsExtAddress_.isUnspecified() && destAddress.matches(meAppsExtAddress_, meAppsExtAddressMask_))
    {
        // the destination is a MecApplication running outside the simulator, forward to meHost (it has forwarding enabled)
        EV << "TrafficFlowFilter::findTrafficFlow - returning flowId (-3) for tunneling to " << destAddress.str() << " (external) " << endl;
        return -3;
    }

    MacNodeId destId = binder_->getMacNodeId(destAddress.toIpv4());
    destId = (destId != 0) ? destId : binder_->getNrMacNodeId(destAddress.toIpv4());
    if (destId == 0)
    {
        EV << "TrafficFlowFilter::findTrafficFlow - destination "<< destAddress.str() << " is not a UE. ";
        if (ownerType_ == UPF || ownerType_ == PGW)
        {
            EV << "Remove packet from the simulation." << endl;
            return -2;   // the destination UE has been removed from the simulation
        }
        else // BS or MEC
        {
            EV << "Forward packet to the gateway." << endl;
            return -1;   // the destination might be outside the cellular network, send the packet to the gateway
        }
    }

    MacNodeId destBS = binder_->getNextHop(destId);
    if (destBS == 0)
    {
        EV << "TrafficFlowFilter::findTrafficFlow - destination " << destAddress.str() << " is a UE [" << destId << "] not attached to any BS. Remove packet from the simulation." << endl;
        return -2;   // the destination UE is not attached to any nodeB
    }

    // the serving node for the UE might be a secondary node in case of NR Dual Connectivity
    // obtains the master node, if any (the function returns destEnb if it is a master already)
    MacNodeId destMaster = binder_->getMasterNode(destBS);
    MacNodeId srcMaster = binder_->getNextHop(binder_->getMacNodeId(srcAddress.toIpv4()));

    if (isBaseStation(ownerType_))
    {
        if (fastForwarding_ && srcMaster == destMaster)
            return 0;                 // local delivery

        return -1;   // send the packet to the PGW/UPF. It will forward the packet to the correct BS
                     // TODO if the BS is within the same core network, there should be a direct tunnel to
                     //      it without going through the gateway (for now, this is not implemented as it
                     //      may cause packets being transmitted via the X2

    }

    // MEC host or PGW/UPF

    // check if the destination belongs to another core network (for multi-operator scenarios)
    std::string destGw = binder_->getNetworkName() + "." + binder_->getModuleByMacNodeId(destMaster)->par("gateway").stdstringValue();
    if (strcmp(gateway_,destGw.c_str()) != 0)
    {
        // the destination is a Base Station under a different core network, send the packet to the gateway
        EV << "Forward packet to the gateway" << endl;
        return -1;
    }

    EV << "Forward packet to BS " << destMaster << endl;
    return destMaster;
}

void TrafficFlowFilter::handleEthernetPacket(Packet *pkt)
{
    // Real 3GPP Ethernet PDU Session work: this packet arrived via the real
    // tsnSwitch -> upf.tsnEth path (see
    // doc/ethernet-pdu-session/20-reuse-encapsulation-direct-wired.md). On
    // the wire it is a real Ethernet II frame the server's real
    // inet::EthernetEncapsulation built:
    //   [outer EthernetMacHeader][our EthernetMacAddressFields + VLAN + payload][EthernetFcs]
    // The outer header/FCS are the physical L2 frame the switch forwarded by
    // (encap's job, on the server side); our own inner frame (the
    // tenant/TSN frame TsnEtherApp itself built) is its payload. Strip the
    // outer header and trailing FCS here, leaving exactly the inner frame
    // that continues over GTP-U/PDCP/RLC/MAC/PHY to the UE (where
    // TsnEtherApp pops EthernetMacAddressFields + Ieee8021qTagEpdHeader).
    pkt->popAtFront<EthernetMacHeader>();
    pkt->popAtBack<EthernetFcs>(inet::ETHER_FCS_BYTES);

    const auto& addrFields = pkt->peekAtFront<EthernetMacAddressFields>();
    const MacAddress &destMac = addrFields->getDest();
    const MacAddress &srcMac = addrFields->getSrc();

    pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac);

    // Real destination resolution: look up the real, aging-capable
    // MacForwardingTable first (this is where genuine uplink-learned entries
    // would be found, had this scope implemented the uplink direction too --
    // see doc/ethernet-pdu-session/07-tff-ethernet-branch.md). On a miss,
    // fall back to the Binder's provisioned UE MAC registration (populated at
    // UE attach time, see LteMacUe.cc) and learn it into the table so
    // subsequent lookups for the same destination hit the real table.
    int destNodeIdAsInterfaceId = macForwardingTable_->getUnicastAddressForwardingInterface(destMac);
    MacNodeId destId;
    if (destNodeIdAsInterfaceId != -1) {
        destId = static_cast<MacNodeId>(destNodeIdAsInterfaceId);
    }
    else {
        destId = binder_->getNodeIdForMacAddress(destMac);
        if (destId == 0) {
            // Genuinely unknown destination MAC: TS 23.501 SS5.6.10.2 allows
            // the UPF to flood such downlink frames to every session; this
            // scope drops them instead (a UE that never registered a tsnMac
            // cannot be a valid Ethernet PDU session destination anyway) --
            // documented as a minor, deliberate scope simplification rather
            // than full N-way GtpUser fan-out. See the final report's
            // Modeled/Not-Modeled table.
            EV_ERROR << "[ETH-PDU][UPF] DROP -- srcMac=" << srcMac.str()
               << " destMac=" << destMac.str() << " (unknown/unregistered), dropping." << endl;
            delete pkt;
            return;
        }
        macForwardingTable_->learnUnicastAddressForwardingInterface(static_cast<int>(destId), destMac);
    }

    // Bug fix (found during review): TftControlInfo.tft is what
    // GtpUser::handleFromTrafficFlowFilter's UNCHANGED "send to a BS" branch
    // passes to binder_->getModuleNameByMacNodeId() to resolve the GTP-U
    // tunnel's IP peer -- exactly mirroring the pre-existing IP path's
    // findTrafficFlow(), which returns destMaster (the serving BASE STATION's
    // MacNodeId), never the UE's own MacNodeId, for this exact reason. The
    // first version of this function set tftInfo->setTft(destId) using the
    // UE's own node id, which made GtpUser resolve and send the GTP-U tunnel
    // packet to the UE's own IP address instead of to "gnb" -- the UE has no
    // GtpUser/socket listening there, so the packet was silently lost even
    // though the UdpSocket send itself "succeeded" (visible as animated
    // traffic leaving the UPF, but never arriving anywhere meaningful). This
    // is why earlier runs showed movement in the animation but zero packets
    // received at the UE's TsnEtherApp.
    MacNodeId destBS = binder_->getNextHop(destId);
    if (destBS == 0) {
        EV_ERROR << "[ETH-PDU][UPF] DROP -- srcMac=" << srcMac.str() << " destMac=" << destMac.str()
           << " -> UE destId=" << destId << " not attached to any BS. Dropping." << endl;
        delete pkt;
        return;
    }
    MacNodeId destMaster = binder_->getMasterNode(destBS);

    // The UE's own MacNodeId is still needed at the gNB (GtpUser reads it
    // back to populate the real GTP-U teid, see GtpUser::handleFromTrafficFlowFilter) --
    // carried separately via FlowControlInfo.destId, since TftControlInfo.tft
    // above now holds the base station's id instead.
    pkt->addTagIfAbsent<FlowControlInfo>()->setDestId(destId);

    auto tftInfo = pkt->addTag<TftControlInfo>();
    tftInfo->setTft(destMaster);

    // Real 3GPP Ethernet PDU Session work: EV_ERROR (not EV_INFO/plain EV,
    // both of which default to INFO level) purely so this line -- the actual
    // MAC-based forwarding decision -- visually stands out in Cmdenv/Qtenv
    // against the very high volume of scheduler/HARQ INFO logging. This is a
    // logging-visibility choice, not an actual error condition. See
    // doc/ethernet-pdu-session/27-mac-visibility-logging.md.
    EV_ERROR << "[ETH-PDU][UPF] FORWARD -- srcMac=" << srcMac.str() << " destMac=" << destMac.str()
       << " -> UE destId=" << destId << " -> serving BS(destMaster)=" << destMaster
       << " (tunnel peer)" << endl;

    send(pkt, "gtpUserGateOut");
}

void TrafficFlowFilter::finish() {
	 if (ownerType_ == PGW || ownerType_ == UPF) {
		 delete[] gateway_;
	 }
}

