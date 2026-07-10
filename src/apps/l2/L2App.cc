//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer A / L2App foundation module.
// See doc/ethernet-pdu-session/00-foundation-l2app.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#include "apps/l2/L2App.h"

#include <inet/common/IProtocolRegistrationListener.h>
#include <inet/common/ModuleAccess.h>
#include <inet/common/ProtocolTag_m.h>
#include <inet/common/TimeTag_m.h>
#include <inet/common/packet/chunk/ByteCountChunk.h>
#include <inet/linklayer/common/InterfaceTag_m.h>
#include <inet/linklayer/common/MacAddressTag_m.h>
#include <inet/linklayer/ethernet/common/EthernetMacHeader_m.h>

using namespace inet;

simsignal_t L2App::framesSentSignal_ = registerSignal("framesSent");
simsignal_t L2App::framesReceivedSignal_ = registerSignal("framesReceived");
simsignal_t L2App::framesDroppedSignal_ = registerSignal("framesDropped");
simsignal_t L2App::frameDelaySignal_ = registerSignal("frameDelay");

Define_Module(L2App);

L2App::L2App() {}

L2App::~L2App()
{
    cancelAndDelete(selfSender_);
}

void L2App::initialize(int stage)
{
    cSimpleModule::initialize(stage);

    if (stage == inet::INITSTAGE_NETWORK_LAYER) {
        // Registers this module as the real, dispatcher-routable provider of
        // Protocol::ethernetMac on gates "out"/"in" -- the exact same
        // registerProtocol() mechanism inet::Ipv4NetworkLayer uses
        // (registerProtocol(Protocol::ipv4, gate("queueOut"),
        // gate("queueIn")), confirmed at Ipv4.cc:111) and inet::Udp uses on
        // its network-facing side (Udp.cc:119). Needed for the UE (wired to
        // "nl" -> cellularNic, see Ue.ned): without it "nl" has no route for
        // DispatchProtocolReq packets tagged ethernetMac and throws "Unknown
        // message". "registerOwnProtocol" (default true) is a harmless no-op
        // on any host wired directly to a leaf module instead of a
        // MessageDispatcher (e.g. the server's TsnEtherApp, direct-wired to
        // tsnEth -- see EthernetPduSessionHost.ned): registerProtocol()'s
        // findConnectedGate() walk finds no IProtocolRegistrationListener and
        // simply does nothing (confirmed in IProtocolRegistrationListener.cc).
        if (par("registerOwnProtocol").boolValue())
            inet::registerProtocol(inet::Protocol::ethernetMac, gate("out"), gate("in"));
        return;
    }
    if (stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;

    srcMac_.setAddress(par("srcMac").stringValue());
    // A pure sink (the UE side of this scenario) configures destMac = "" to
    // signal "don't send" (see the strlen() check below) -- MacAddress::setAddress()
    // throws on an empty string, so only parse it when non-empty. Fixes:
    // "MacAddress: wrong address syntax '': 12 hex digits expected..." at init.
    if (strlen(par("destMac").stringValue()) > 0)
        destMac_.setAddress(par("destMac").stringValue());
    sendInterval_ = par("sendInterval");
    packetLength_ = B(par("packetLength").intValue());
    etherType_ = par("etherType");
    startTime_ = par("startTime");
    stopTime_ = par("stopTime");

    // Only the sending side (destMac configured) needs an egress interface to
    // tag outgoing frames for (InterfaceReq). A pure sink (destMac == "")
    // receives via the network-layer dispatcher and never sets InterfaceReq,
    // so it must NOT do this lookup -- doing it unconditionally throws
    // "interface 'cellular' not found" on a sink and is simply unnecessary.
    // The interface is registered by IP2Nic::registerInterface() under the
    // name in IP2Nic's own "interfaceName" parameter (default "cellular"),
    // which is why L2App's interfaceName default is "cellular" (the registered
    // NetworkInterface name), not "cellularNic" (the NED submodule name).
    if (strlen(par("destMac").stringValue()) > 0) {
        ift_ = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        NetworkInterface *ie = ift_->findInterfaceByName(par("interfaceName").stringValue());
        if (ie == nullptr)
            throw cRuntimeError("L2App: egress interface '%s' not found in InterfaceTable", par("interfaceName").stringValue());
        interfaceId_ = ie->getInterfaceId();

        // Only nodes configured with a real destination (the source side of
        // the flow) run the send timer.
        selfSender_ = new cMessage("l2AppSendTimer");
        scheduleAt(std::max(startTime_, simTime()), selfSender_);
    }
}

void L2App::scheduleNextSend()
{
    simtime_t next = simTime() + sendInterval_;
    if (stopTime_ > SIMTIME_ZERO && next > stopTime_)
        return;
    scheduleAt(next, selfSender_);
}

Packet *L2App::buildFrame()
{
    // Builds one real, byte-serialized Ethernet II frame: a genuine
    // inet::EthernetMacHeader is inserted at the front of the packet (not a
    // metadata tag standing in for it, unlike the reference TSN shim's
    // TsnEtherTag), followed by a ByteCountChunk payload of the configured
    // length. PacketProtocolTag is set to Protocol::ethernetMac so every
    // downstream module (TrafficFlowFilter, GtpUser, PDCP, IP2Nic) can
    // branch on the packet's *real* protocol type instead of the shim's
    // "packet has no IP tag" heuristic.
    Packet *pkt = new Packet("EthFrame");
    pkt->insertAtBack(makeShared<ByteCountChunk>(packetLength_));

    auto ethHeader = makeShared<EthernetMacHeader>();
    ethHeader->setDest(destMac_);
    ethHeader->setSrc(srcMac_);
    ethHeader->setTypeOrLength(etherType_);
    pkt->insertAtFront(ethHeader);

    pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac);
    pkt->addTagIfAbsent<CreationTimeTag>()->setCreationTime(simTime());
    // Names the target egress interface (cellularNic) so the "nl"
    // MessageDispatcher this module is wired to can route the packet to
    // it, exactly as inet::Ipv4 tags outgoing datagrams with InterfaceReq
    // before handing them to the same kind of dispatcher.
    pkt->addTagIfAbsent<InterfaceReq>()->setInterfaceId(interfaceId_);
    // Real 3GPP Ethernet PDU Session work: required (not optional) whenever
    // this app sits behind a real inet::EthernetEncapsulation -- it reads
    // MacAddressReq unconditionally (getTag, throws if absent) to build the
    // real frame's real dest/src MAC. Harmless, unread tag on the UE path
    // (cellularNic has no EthernetEncapsulation). See
    // doc/ethernet-pdu-session/15-real-switch-topology.md.
    auto macAddressReq = pkt->addTagIfAbsent<MacAddressReq>();
    macAddressReq->setDestAddress(destMac_);
    macAddressReq->setSrcAddress(srcMac_);
    // Real 3GPP Ethernet PDU Session work: routes this outgoing frame through
    // the host's "nl" dispatcher to inet::EthernetEncapsulation (which adds
    // the real outer EthernetMacHeader + FCS) rather than straight to the
    // interface. Must be SP_REQUEST explicitly: with a bare tag the
    // dispatcher's KLUDGE would see PacketProtocolTag == DispatchProtocolReq
    // protocol and mis-resolve it to SP_INDICATION (an upward path), sending
    // the frame the wrong way. See doc/ethernet-pdu-session/15-real-switch-topology.md.
    auto dispatchReq = pkt->addTagIfAbsent<DispatchProtocolReq>();
    dispatchReq->setProtocol(&Protocol::ethernetMac);
    dispatchReq->setServicePrimitive(SP_REQUEST);
    return pkt;
}

void L2App::handleFrame(Packet *pkt)
{
    // Validates that a real EthernetMacHeader is present (a malformed or
    // foreign packet reaching this gate is dropped, not silently misread as
    // IP the way the unmodified PDCP header-compression path would).
    auto ethHeader = pkt->popAtFront<EthernetMacHeader>();
    if (ethHeader == nullptr) {
        emit(framesDroppedSignal_, 1L);
        delete pkt;
        return;
    }

    if (auto creationTag = pkt->findTag<CreationTimeTag>()) {
        simtime_t delay = simTime() - creationTag->getCreationTime();
        emit(frameDelaySignal_, delay);
    }
    emit(framesReceivedSignal_, 1L);
    delete pkt;
}

void L2App::handleMessage(cMessage *msg)
{
    if (msg == selfSender_) {
        Packet *pkt = buildFrame();
        send(pkt, "out");
        emit(framesSentSignal_, 1L);
        seqCounter_++;
        scheduleNextSend();
        return;
    }

    // Anything arriving on gate "in" is an inbound Ethernet frame.
    handleFrame(check_and_cast<Packet *>(msg));
}
