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
#include "corenetwork/gtp/GtpUser.h"
#include "corenetwork/trafficFlowFilter/TftControlInfo_m.h"
#include <iostream>
#include <inet/networklayer/common/L3AddressResolver.h>
#include <inet/networklayer/ipv4/Ipv4Header_m.h>
#include <inet/common/packet/printer/PacketPrinter.h>
#include <inet/common/socket/SocketTag_m.h>
#include <inet/linklayer/common/InterfaceTag_m.h>
// Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): for building/stripping
// the outer Ethernet header on the IP-less N3 leg. See doc/ethernet-pdu-session/31-macinmac-n3.md.
#include <inet/common/ProtocolTag_m.h>
#include <inet/linklayer/common/MacAddressTag_m.h>
#include <inet/linklayer/ethernet/common/EthernetMacHeader_m.h>
#include <inet/linklayer/ethernet/common/Ethernet.h>

Define_Module(GtpUser);

using namespace omnetpp;
using namespace inet;

void GtpUser::initialize(int stage)
{
    cSimpleModule::initialize(stage);

    // wait until all the IP addresses are configured
    if (stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;
    localPort_ = par("localPort");

    // get reference to the binder
    binder_ = getBinder();

    // transport layer access
    socket_.setOutputGate(gate("socketOut"));
    socket_.bind(localPort_);

    tunnelPeerPort_ = par("tunnelPeerPort");

    ownerType_ = selectOwnerType(getAncestorPar("nodeType"));

    // find the address of the core network gateway
    if (ownerType_ != PGW && ownerType_ != UPF)
    {
        // check if this is a gNB connected as secondary node
        bool connectedBS = isBaseStation(ownerType_) && getParentModule()->gate("ppp$o")->isConnected();

        if (connectedBS || ownerType_ == UPF_MEC)
        {
            std::string gateway = binder_->getNetworkName() + "." + getAncestorPar("gateway").stdstringValue();
            gwAddress_ = L3AddressResolver().resolve(gateway.c_str());
        }
    }

    if(isBaseStation(ownerType_))
        myMacNodeID = getParentModule()->par("macNodeId");
    else
        myMacNodeID = 0;

    ie_ = detectInterface();

    // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): read the N3
    // transport selector. "ethernet" makes the N3 leg IP-less (see GtpUser.ned
    // and doc/ethernet-pdu-session/31-macinmac-n3.md).
    n3Transport_ = par("n3Transport").stdstringValue();
}

NetworkInterface* GtpUser::detectInterface()
{
    IInterfaceTable *ift = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
    const char *interfaceName = par("ipOutInterface");
    NetworkInterface *ie = nullptr;

    if (strlen(interfaceName) > 0) {
        ie = ift->findInterfaceByName(interfaceName);
        if (ie == nullptr)
            throw cRuntimeError("Interface \"%s\" does not exist", interfaceName);
    }

    return ie;
}

CoreNodeType GtpUser::selectOwnerType(const char * type)
{
    EV << "GtpUser::selectOwnerType - setting owner type to " << type << endl;
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

    error("GtpUser::selectOwnerType - unknown owner type [%s]. Aborting...",type);

    // you should not be here
    return ENB;
}

void GtpUser::handleMessage(cMessage *msg)
{
    if (strcmp(msg->getArrivalGate()->getFullName(), "trafficFlowFilterGate") == 0)
    {
        EV << "GtpUser::handleMessage - message from trafficFlowFilter" << endl;

        // forward the encapsulated Ipv4 datagram
        handleFromTrafficFlowFilter(check_and_cast<Packet *>(msg));
    }
    else if(strcmp(msg->getArrivalGate()->getFullName(),"socketIn")==0)
    {
        EV << "GtpUser::handleMessage - message from udp layer" << endl;
        Packet *packet = check_and_cast<Packet *>(msg);
        PacketPrinter printer; // turns packets into human readable strings
        printer.printPacket(EV, packet); // print to standard output


        handleFromUdp(packet);
    }
    else if(strcmp(msg->getArrivalGate()->getFullName(),"n3EthIn")==0)
    {
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): a tunneled frame
        // arrived over the IP-less Ethernet N3 leg (gNB side).
        EV << "GtpUser::handleMessage - message from N3 Ethernet leg" << endl;
        handleFromN3Ethernet(check_and_cast<Packet *>(msg));
    }
}

void GtpUser::handleFromTrafficFlowFilter(Packet * datagram)
{
    /*
     * when we get here, it means that the packet is entering the core network and it may need to be tunneled to some destination,
     * based on the trafficFlowId found by the TrafficFlowFilter:
     * 1) tftId == -2, the destination does not belong to the simulation anymore
     *    --> delete the packet
     * 2) tftId -- 0, we are on a BS and the destination is a UE under the same gNB
     *    --> forward the packet to the local LTE/NR NIC
     * 3) tftId == -1, destination is outside the radio network
     *    --> tunnel the packet towards the CN gateway
     * 3) tftId == -3, destination is a MEC host
     *    3a) the MEC host is inside the same core network
     *        --> tunnel the packet towards the MEC host
     *    3b) the MEC host is inside another core network
     *        --> tunnel the packet towards the CN gateway
     * 4) otherwise, destination is a UE
     *    4a) the UE is inside the same network
     *        --> tunnel the packet towards its serving BS
     *    4b) the UE is inside another network
     *        --> tunnel the packet towards the CN gateway
     */

    auto tftInfo = datagram->removeTag<TftControlInfo>();
    TrafficFlowTemplateId flowId = tftInfo->getTft();

    EV << "GtpUser::handleFromTrafficFlowFilter - Received a tftMessage with flowId[" << flowId << "]" << endl;

    if(flowId == -2)
    {
        // the destination has been removed from the simulation. Delete datagram
        EV << "GtpUser::handleFromTrafficFlowFilter - Destination has been removed from the simulation. Delete packet." << endl;
        delete datagram;
        return;
    }

    // If we are on the eNB and the flowId represents the ID of this eNB, forward the packet locally
    if (flowId == 0)
    {
        // local delivery
        send(datagram,"pppGate");
    }
    else
    {
        // Real 3GPP Ethernet PDU Session work: an Ethernet-tagged datagram
        // (see TrafficFlowFilter::handleEthernetPacket) has no Ipv4Header to
        // peek -- destAddr is only needed below for the MEC/logging branches,
        // which this project's Ethernet PDU session scope never reaches
        // (handleEthernetPacket only ever returns a real destId or drops the
        // packet itself, never 0/-1/-3). See doc/ethernet-pdu-session/09-gtpuser-ethernet.md.
        auto protocolTag = datagram->findTag<PacketProtocolTag>();
        bool isEthernet = protocolTag != nullptr && protocolTag->getProtocol() == &Protocol::ethernetMac;
        Ipv4Address destAddr;
        if (!isEthernet)
        {
            const auto& hdr = datagram->peekAtFront<Ipv4Header>();
            destAddr = hdr->getDestAddress();
        }

        // create a new GtpUserMessage and encapsulate the datagram within the GtpUserMessage
        auto header = makeShared<GtpUserMsg>();
        // Real 3GPP Ethernet PDU Session work: teid identifies the
        // destination UE's session (its MacNodeId) -- this is what the
        // decapsulating end (below) uses to resolve Binder::getPduSessionType()
        // and decide how to handle the payload, rather than repeating a
        // per-packet type flag. flowId itself is NOT the UE's id for the
        // Ethernet branch (see TrafficFlowFilter::handleEthernetPacket's own
        // bugfix note): it is the serving base station's MacNodeId, needed
        // below to resolve the tunnel's IP peer. The UE's real id travels
        // separately via FlowControlInfo.destId.
        if (isEthernet) {
            MacNodeId ueDestId = datagram->getTag<FlowControlInfo>()->getDestId();
            header->setTeid(static_cast<unsigned int>(ueDestId));
        }
        else {
            header->setTeid(0);   // unchanged baseline behavior for ordinary IP traffic
        }
        auto gtpPacket = new Packet(datagram->getName());
        gtpPacket->insertAtBack(datagram->peekData());
        if (isEthernet)
        {
            // Real, minimal use of a real GTP-U extension header (TS 29.281
            // SS5.2.1 chaining, TS 38.415 PDU Session Container) -- carries a
            // genuine per-packet QFI. qfi=0/pduType=0 (DL) are this scope's
            // fixed defaults; per-flow QFI assignment is not implemented.
            header->setEFlag(true);
            header->setNextExtensionHeaderType(0x85);
            auto extHeader = makeShared<GtpUserExtHeaderPduSessionContainer>();
            extHeader->setPduType(0);
            extHeader->setQfi(0);
            extHeader->setNextExtensionHeaderType(0);
            gtpPacket->insertAtFront(extHeader);
        }
        gtpPacket->insertAtFront(header);

        delete datagram;

        L3Address tunnelPeerAddress;
        if (flowId == -1) // send to the gateway
        {
            EV << "GtpUser::handleFromTrafficFlowFilter - tunneling to " << gwAddress_.str() << endl;
            tunnelPeerAddress = gwAddress_;
        }
        else if(flowId == -3) // send to a MEC host
        {
            // check if the destination MEC host is within the same core network


            // retrieve the address of the UPF included within the MEC host
            EV << "GtpUser::handleFromTrafficFlowFilter - tunneling to " << destAddr.str() << endl;
            tunnelPeerAddress = binder_->getUpfFromMecHost(inet::L3Address(destAddr));
        }
        else  // send to a BS
        {
            // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): if this
            // instance's N3 leg is the IP-less Ethernet transport, wrap the
            // GtpUserMsg packet in an outer Ethernet header and send it out the
            // dedicated N3 Ethernet interface -- NO UDP, NO IP, no
            // getModuleNameByMacNodeId/L3AddressResolver lookup at all. The
            // teid (= UE MacNodeId, set above) still identifies the session,
            // now playing the role of the IEEE 802.1ah I-SID. See
            // doc/ethernet-pdu-session/31-macinmac-n3.md.
            if (n3Transport_ == "ethernet" && isEthernet)
            {
                EV_ERROR << "[ETH-PDU][N3-ETH][TX] MAC-in-MAC to gNB -- teid(UE destId, I-SID)="
                   << header->getTeid() << " (no IP)" << endl;
                sendOverN3Ethernet(gtpPacket);
                return;
            }

            // check if the destination is within the same core network


            // get the symbolic IP address of the tunnel destination ID
            // then obtain the address via IPvXAddressResolver
            const char* symbolicName = binder_->getModuleNameByMacNodeId(flowId);
            EV << "GtpUser::handleFromTrafficFlowFilter - tunneling to " << symbolicName << endl;
            tunnelPeerAddress = L3AddressResolver().resolve(symbolicName);
            // Real 3GPP Ethernet PDU Session work: EV_ERROR for visibility
            // only, see the identical note in
            // TrafficFlowFilter::handleEthernetPacket() and
            // doc/ethernet-pdu-session/27-mac-visibility-logging.md. teid
            // carries the destination UE's MacNodeId (set above); flowId is
            // the serving BS's MacNodeId (the GTP-U tunnel peer, "symbolicName").
            if (isEthernet)
                EV_ERROR << "[ETH-PDU][GTP-U][TX] tunneling teid(UE destId)=" << header->getTeid()
                   << " -> BS=" << symbolicName << " (" << tunnelPeerAddress.str() << ")" << endl;
        }
        socket_.sendTo(gtpPacket, tunnelPeerAddress, tunnelPeerPort_);
    }
}

void GtpUser::sendOverN3Ethernet(Packet * gtpPacket)
{
    // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): wraps the
    // already-built [GtpUserMsg][ext][inner tenant frame] packet in a genuine
    // OUTER inet::EthernetMacHeader (IEEE 802.1ah "backbone" header) + trailing
    // EthernetFcs, then sends it out n3EthOut toward the gNB. Built manually
    // (like TsnEtherApp) because n3EthOut is direct-wired to a basic
    // inet::EthernetMac, which requires the real header + a placeholder FCS to
    // already be present (see doc/ethernet-pdu-session/19-missing-fcs-placeholder.md).
    //
    // Addressing: point-to-point UPF<->gNB link. The outer destination is the
    // broadcast address and the gNB's N3 interface is promiscuous, so no
    // backbone-MAC resolution/config is needed (a multi-gNB backbone would
    // instead unicast to the serving gNB's backbone MAC via a backbone FDB --
    // out of scope here). The 802.1ah I-SID (which UE this frame is for) is
    // carried by the inner GtpUserMsg.teid, so the outer MACs are pure
    // transport. Outer EtherType 0x88B7 marks the N3 backbone service.
    auto fcs = makeShared<EthernetFcs>();
    gtpPacket->insertAtBack(fcs);

    auto outerEth = makeShared<EthernetMacHeader>();
    outerEth->setDest(MacAddress::BROADCAST_ADDRESS);
    outerEth->setSrc(MacAddress("0a-aa-00-00-00-fe"));   // fixed UPF backbone src (transport only)
    outerEth->setTypeOrLength(0x88B7);
    gtpPacket->insertAtFront(outerEth);

    gtpPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac);
    auto macReq = gtpPacket->addTagIfAbsent<MacAddressReq>();
    macReq->setDestAddress(MacAddress::BROADCAST_ADDRESS);
    macReq->setSrcAddress(MacAddress("0a-aa-00-00-00-fe"));

    send(gtpPacket, "n3EthOut");
}

void GtpUser::handleFromN3Ethernet(Packet * frame)
{
    // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3, gNB side): the mirror
    // of sendOverN3Ethernet(). The basic inet::EthernetMac passed the frame up
    // with its outer header intact (it does not decapsulate) and the trailing
    // FCS still present, so strip both here, leaving exactly the
    // [GtpUserMsg][ext][inner tenant frame] packet that decapAndDeliver()
    // expects -- identical to what handleFromUdp() would have after the UDP
    // socket delivered it. No IP was ever involved.
    frame->popAtFront<EthernetMacHeader>();
    frame->popAtBack<EthernetFcs>(inet::ETHER_FCS_BYTES);
    EV_ERROR << "[ETH-PDU][N3-ETH][RX] gnb received MAC-in-MAC frame (no IP), decapsulating" << endl;
    decapAndDeliver(frame);
}

void GtpUser::handleFromUdp(Packet * pkt)
{
    // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): the GTP-U/UDP/IP
    // path (standard N3). The UDP socket has already stripped UDP/IP, so what
    // arrives starts with the GtpUserMsg -- identical to what
    // handleFromN3Ethernet() produces after stripping the outer Ethernet
    // header. Both therefore share decapAndDeliver().
    decapAndDeliver(pkt);
}

void GtpUser::decapAndDeliver(Packet * pkt)
{
    /*
     * when we get here, it means that the packet reached the end of the tunnel and it needs to be decapsulated.
     * The following cases can occur:
     * 1) the packet has been received by a BS (this GtpUser module is inside a BS)
     *    --> the destination is for sure a UE served by this BS, hence we decapsulate the packet and deliver it locally
     * 2) the packet has been received by a MecHost (this GtpUser module is inside a MEC host's UPF)
     *    --> the destination is for sure the MEC host, hence we decapsulate the packet and deliver it locally
     * 3) the packet has been received by a "border" PGW/UPF (this GtpUser is inside a PGW/UPF)
     *    3a) the destination of the packet is NOT a UE
     *        --> the destination is for sure outside this network (e.g. a remote server or a node within another radio network),
     *            hence we decapsulate the packet and deliver it to the outbound interface
     *    3b) the destination of the packet is a UE
     *        3b1) the serving BS of the UE does NOT belong to the same radio network as the PGW/UPF
     *             --> we decapsulate the packet and deliver it to the outbound interface
     *        3b2) the serving BS of the UE belongs to the same radio network as the PGW/UPF
     *             --> we decapsulate the packet, re-encapsulate it and send it to the correct BS
     */


    EV << "GtpUser::handleFromUdp - Decapsulating and forwarding to the correct destination" << endl;

    auto gtpUserMsg = pkt->popAtFront<GtpUserMsg>();

    // Real 3GPP Ethernet PDU Session work: resolves the payload's real PDU
    // session type from the real teid this project now populates (see
    // handleFromTrafficFlowFilter above), instead of the payload always
    // being assumed IPv4 -- see doc/ethernet-pdu-session/09-gtpuser-ethernet.md.
    PduSessionType sessionType = binder_->getPduSessionType(gtpUserMsg->getTeid());
    if (gtpUserMsg->getEFlag() && gtpUserMsg->getNextExtensionHeaderType() == 0x85)
    {
        // Discards the real PDU Session Container extension header pushed on
        // encapsulation; this scope does not act on its QFI value.
        pkt->popAtFront<GtpUserExtHeaderPduSessionContainer>();
    }

    // re-create the original datagram and send it to the local network
    auto originalPacket = new Packet (pkt->getName());
    originalPacket->insertAtBack(pkt->peekData());
    if (sessionType == PduSessionType::ETHERNET)
        originalPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac);
    else
        originalPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ipv4);
    // remove any pending socket indications
    auto sockInd = pkt->removeTagIfPresent<SocketInd>();

    delete pkt;

    if (sessionType == PduSessionType::ETHERNET)
    {
        // Real 3GPP Ethernet PDU Session work: this scope only implements
        // downlink delivery to a directly-served UE (matching the reference
        // shim's own scope) -- no Ipv4Header exists to inspect, and the
        // MEC/re-tunneling logic below is IP-address-driven and does not
        // apply. Local delivery to the UE's radio stack still needs the same
        // InterfaceReq targeting the IP branch below uses.
        if (ie_ != nullptr)
            originalPacket->addTagIfAbsent<InterfaceReq>()->setInterfaceId(ie_->getInterfaceId());
        // Real 3GPP Ethernet PDU Session work: EV_ERROR for visibility only,
        // see doc/ethernet-pdu-session/27-mac-visibility-logging.md. "[N3]"
        // (not "[GTP-U]") because decapAndDeliver() is shared by both the GTP-U
        // and the MAC-in-MAC N3 transports (see doc/ethernet-pdu-session/31-macinmac-n3.md);
        // the transport-specific line was already logged upstream.
        EV_ERROR << "[ETH-PDU][N3][RX] gnb decapsulated -- teid(UE destId)=" << gtpUserMsg->getTeid()
           << " -> local delivery to radio stack" << endl;
        send(originalPacket, "pppGate");
        return;
    }

    const auto& hdr = originalPacket->peekAtFront<Ipv4Header>();
    const Ipv4Address& destAddr = hdr->getDestAddress();

    if (isBaseStation(ownerType_))
    {
        // add Interface-Request for cellular NIC
        if (ie_ != nullptr)
            originalPacket->addTagIfAbsent<InterfaceReq>()->setInterfaceId(ie_->getInterfaceId());

        EV << "GtpUser::handleFromUdp - Datagram local delivery to " << destAddr.str() << endl;
        // local delivery
        send(originalPacket,"pppGate");
    }
    else if(ownerType_== UPF_MEC )
    {
        // we are on the MEC, local delivery
        EV << "GtpUser::handleFromUdp - Datagram local delivery to " << destAddr.str() << endl;
        send(originalPacket,"pppGate");
    }
    else if (ownerType_== PGW || ownerType_ == UPF)
    {
        MacNodeId destId = binder_->getMacNodeId(destAddr);
        if (destId != 0)  // final destination is a UE
        {
            MacNodeId destMaster = binder_->getNextHop(destId);

            // check if the destination belongs to the same core network (for multi-operator scenarios)
            std::string gwFullPath = binder_->getNetworkName() + "." + binder_->getModuleByMacNodeId(destMaster)->par("gateway").stdstringValue();
            if (this->getParentModule()->getFullPath().compare(gwFullPath) == 0)
            {

                // the destination is a Base Station under the same core network as this PGW/UPF,
                // tunnel the packet toward that BS
                const char* symbolicName = binder_->getModuleNameByMacNodeId(destMaster);
                L3Address tunnelPeerAddress = L3AddressResolver().resolve(symbolicName);
                EV << "GtpUser::handleFromUdp - tunneling to BS " << symbolicName << endl;

                // send the message to the BS through GTP tunneling
                // * create a new GtpUserMessage
                // * encapsulate the datagram within the GtpUserMsg
                auto header = makeShared<GtpUserMsg>();
                header->setTeid(0);
                // Bug fix (found during review): this explicit override is a
                // leftover from the original 1-byte-declared/8-byte-actually-
                // written stub GtpUserMsg. The real header (see GtpUserMsg.msg)
                // now always declares and serializes a consistent 12 bytes;
                // forcing chunkLength back to 8 here would desync the chunk's
                // declared length from what GtpUserMsgSerializer actually
                // writes, which FieldsChunkSerializer treats as an error the
                // next time this packet is serialized (e.g. by the
                // PacketPrinter call in handleMessage()'s socketIn branch).
                auto gtpMsg = new Packet(originalPacket->getName());
                gtpMsg->insertAtFront(header);
                auto data = originalPacket->peekData();
                gtpMsg->insertAtBack(data);
                delete originalPacket;

                // create a new GtpUserMessage
                EV << "GtpUser::handleFromUdp - Tunneling datagram to " << tunnelPeerAddress.str() << ", final destination[" << destAddr.str() << "]" << endl;
                socket_.sendTo(gtpMsg, tunnelPeerAddress, tunnelPeerPort_);
                return;
            }
        }

        // destination is outside the radio network
        EV << "GtpUser::handleFromUdp - Sending datagram outside the radio network, destination[" << destAddr.str() << "]" << endl;
        send(originalPacket,"pppGate");
    }
}
