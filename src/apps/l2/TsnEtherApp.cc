//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / real 802.1Q VLAN tagging.
// See doc/ethernet-pdu-session/00-foundation-l2app.md and
// doc/ethernet-pdu-session/02-tsnetherapp-vlan.md for the full
// baseline/modified/rationale documentation entries for this file.
//

#include "apps/l2/TsnEtherApp.h"

#include <inet/common/Protocol.h>
#include <inet/common/ProtocolTag_m.h>
#include <inet/common/IProtocolRegistrationListener.h>
#include <inet/common/TimeTag_m.h>
#include <inet/common/packet/chunk/ByteCountChunk.h>
#include <inet/linklayer/common/InterfaceTag_m.h>
#include <inet/linklayer/common/MacAddressTag_m.h>
#include <inet/linklayer/common/PcpTag_m.h>
#include <inet/linklayer/ethernet/common/EthernetMacHeader_m.h>
#include <inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h>

#include "apps/l2/TsnEthFrameId_m.h"

using namespace inet;

Define_Module(TsnEtherApp);

// Real 3GPP Ethernet PDU Session work: network-wide packet delay/loss
// observer support -- see the identical signal names/pattern in
// CbrSender.cc/CbrReceiver.cc and doc/ethernet-pdu-session/28-packet-observers.md.
simsignal_t TsnEtherApp::packetDelaySentSignal_ = registerSignal("packetDelaySentSignal");
simsignal_t TsnEtherApp::appPacketSentSignal_ = registerSignal("appPacketSent");
simsignal_t TsnEtherApp::packetDelayReceivedSignal_ = registerSignal("packetDelayReceivedSignal");
simsignal_t TsnEtherApp::appPacketReceivedSignal_ = registerSignal("appPacketReceived");

void TsnEtherApp::initialize(int stage)
{
    L2App::initialize(stage);
    if (stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;
    vid_ = par("vid");
    pcp_ = par("pcp");
}

Packet *TsnEtherApp::buildFrame()
{
    // Real 3GPP Ethernet PDU Session work: this app is wired directly to a
    // real inet::EthernetEncapsulation (see EthernetPduSessionHost.ned),
    // which builds the ONE genuine outer inet::EthernetMacHeader + trailing
    // inet::EthernetFcs a real inet::EthernetMac requires -- this app must
    // NOT build those itself (an earlier version did, byte-for-byte matching
    // encap's own code, and still hit chunk-conversion errors inside
    // EthernetMac's send path even after a clean rebuild; delegating to the
    // real, tested module removed the problem entirely -- see
    // doc/ethernet-pdu-session/20-reuse-encapsulation-direct-wired.md).
    //
    // What this app builds is the real, byte-accurate IEEE 802.1Q C-VLAN tag
    // that becomes encap's payload: [dest(6)+src(6)] address fields (encap
    // reads MacAddressReq for the real header's own dest/src -- these
    // address fields are the *inner* frame's own addressing, kept for the
    // UE-side TsnEtherApp::handleFrame() to read back) + [pcp/dei/vid + real
    // EtherType, 4B] (inet::Ieee8021qTagEpdHeader, the "Epd" variant that
    // folds the trailing EtherType together with the VLAN control fields,
    // C-tag TPID 0x8100 implied by virtue of sitting right after encap's own
    // outer header) + payload.
    Packet *pkt = new Packet("TsnEthFrame");

    // Real 3GPP Ethernet PDU Session work: a real, serialized frame-id field
    // (TsnEthFrameIdHeader, see TsnEthFrameId.msg) carried IN the payload,
    // not a Packet-level tag -- this is what lets PacketDelayObserver /
    // PacketLossObserver measure delay/loss correctly even though
    // CreationTimeTag does not survive the UE-side RLC round-trip (see
    // doc/ethernet-pdu-session/28-packet-observers.md). The remaining
    // configured payload length is filled with an abstract ByteCountChunk,
    // exactly as before.
    auto frameIdHeader = makeShared<TsnEthFrameIdHeader>();
    frameIdHeader->setFrameId(seqCounter_);
    pkt->insertAtBack(frameIdHeader);
    B fillerLength = (packetLength_ > frameIdHeader->getChunkLength())
        ? (packetLength_ - frameIdHeader->getChunkLength()) : B(0);
    pkt->insertAtBack(makeShared<ByteCountChunk>(fillerLength));

    auto vlanHeader = makeShared<Ieee8021qTagEpdHeader>();
    vlanHeader->setPcp(pcp_);
    vlanHeader->setDei(false);
    vlanHeader->setVid(vid_);
    vlanHeader->setTypeOrLength(etherType_);
    pkt->insertAtFront(vlanHeader);

    auto addrFields = makeShared<EthernetMacAddressFields>();
    addrFields->setDest(destMac_);
    addrFields->setSrc(srcMac_);
    pkt->insertAtFront(addrFields);

    // Real 3GPP Ethernet PDU Session work: set PacketProtocolTag to
    // Protocol::ieee8022llc -- NOT ethernetMac. Reason (the fix that finally
    // stopped the "Unknown protocol: ethernetmac" throw, see
    // doc/ethernet-pdu-session/23-ieee8022llc-tag-final-fix.md):
    // inet::EthernetEncapsulation::processPacketFromHigherLayer()
    // (EthernetEncapsulation.cc:169-175) computes the outer frame's
    // typeOrLength field like this:
    //   if (protocol && *protocol != Protocol::ieee8022llc)
    //       typeOrLength = getEthertypeProtocolGroup()->getProtocolNumber(protocol); // THROWS if unregistered
    //   else
    //       typeOrLength = packet->getByteLength();
    // encap also requires SOME non-null protocol (getProtocol() throws on
    // null). Protocol::ieee8022llc is the one value that satisfies "non-null"
    // while taking the else branch -- encap then uses a real IEEE 802.3
    // length field (valid framing: our data part < 1536 B) and NEVER calls
    // getProtocolNumber(), so the throw is structurally impossible,
    // independent of any ethertype-registry / cross-DLL shared-variable
    // behavior (registering Protocol::ethernetMac in that registry was tried
    // and did not take effect at encap's read point -- see entry 23).
    // Crucially, encap sets PacketProtocolTag = Protocol::ethernetMac itself
    // (EthernetEncapsulation.cc:191) right before sending to the MAC, so
    // every downstream module (the switch, UPF TrafficFlowFilter, GtpUser,
    // PDCP) still sees the real Protocol::ethernetMac tag exactly as before.
    pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ieee8022llc);
    pkt->addTagIfAbsent<CreationTimeTag>()->setCreationTime(simTime());
    pkt->addTagIfAbsent<InterfaceReq>()->setInterfaceId(interfaceId_);
    auto macAddressReq = pkt->addTagIfAbsent<MacAddressReq>();
    macAddressReq->setDestAddress(destMac_);
    macAddressReq->setSrcAddress(srcMac_);
    // Real 3GPP Ethernet PDU Session work: inet::PcpReq, the real INET tag
    // that "determines the PCP that should be used to send the packet"
    // (PcpTag.msg) -- read by TsnPcpClassifier at the egress queue (which
    // sits after encap, inside tsnEth). See that file's own comment for why
    // this is used instead of peeking the real Ieee8021qTagEpdHeader chunk
    // (still inserted above, still on the wire) back out of the packet.
    pkt->addTagIfAbsent<PcpReq>()->setPcp(pcp_);

    // Real 3GPP Ethernet PDU Session work (multi-UE): when several sender apps
    // share one tsnEth interface, they fan in through a MessageDispatcher (see
    // EthernetPduSessionHost.ned), which routes each outgoing frame down to the
    // encap by looking up the ethernetMac SERVICE via this DispatchProtocolReq.
    // TsnEtherApp overrides buildFrame() and therefore does NOT inherit L2App's
    // own DispatchProtocolReq, so it must set it here. SP_REQUEST is explicit: a
    // bare tag would be mis-resolved to SP_INDICATION by the dispatcher's KLUDGE
    // (PacketProtocolTag here is ieee8022llc, not ethernetMac). Harmless in the
    // single-app, direct-wired case -- no dispatcher is present to read it.
    auto dispatchReq = pkt->addTagIfAbsent<DispatchProtocolReq>();
    dispatchReq->setProtocol(&Protocol::ethernetMac);
    dispatchReq->setServicePrimitive(SP_REQUEST);

    // Real 3GPP Ethernet PDU Session work: network-wide observer signals --
    // see the note on packetDelaySentSignal_ in TsnEtherApp.h. Emitted with
    // the same frameId just written into TsnEthFrameIdHeader above.
    emit(packetDelaySentSignal_, (long)seqCounter_);
    emit(appPacketSentSignal_, 1L);

    // Real 3GPP Ethernet PDU Session work: EV_ERROR (not EV_INFO) purely so
    // this line visually stands out in Cmdenv/Qtenv against the very high
    // volume of scheduler/HARQ EV_INFO logging (RAC, grants, HARQ processes,
    // etc.) that otherwise buries it -- this is a logging-visibility choice,
    // not an actual error condition. See
    // doc/ethernet-pdu-session/27-mac-visibility-logging.md.
    EV_ERROR << "[ETH-PDU][TX] server.l2App -- srcMac=" << srcMac_.str()
              << " destMac=" << destMac_.str()
              << " vid=" << vid_ << " pcp=" << pcp_
              << " etherType=0x" << std::hex << etherType_ << std::dec
              << " len=" << packetLength_ << " seq=" << seqCounter_ << endl;
    return pkt;
}

void TsnEtherApp::handleFrame(Packet *pkt)
{
    // Mirror image of buildFrame(): pop the address fields, then the real
    // VLAN+EtherType chunk, recovering genuine pcp/vid values from wire
    // bytes rather than from a metadata field (contrast with the reference
    // shim's L2App, which read TsnEtherTag.pcp/vid directly off a tag). The
    // outer header encap added on the server side was already popped by
    // TrafficFlowFilter at the UPF (see TrafficFlowFilter::handleEthernetPacket()) --
    // what reaches here is exactly what this app itself built.
    auto addrFields = pkt->popAtFront<EthernetMacAddressFields>();
    if (addrFields == nullptr) {
        emit(framesDroppedSignal_, 1L);
        delete pkt;
        return;
    }
    auto vlanHeader = pkt->popAtFront<Ieee8021qTagEpdHeader>();
    if (vlanHeader == nullptr) {
        emit(framesDroppedSignal_, 1L);
        delete pkt;
        return;
    }

    // Real 3GPP Ethernet PDU Session work: real, serialized frame-id field
    // (see the note in buildFrame()) -- this is what makes
    // PacketDelayObserver/PacketLossObserver's measurement robust against
    // the RLC round-trip, unlike the CreationTimeTag-based measurement
    // immediately below (kept for backward compatibility with existing
    // consumers of framesReceived/frameDelay, even though it now reads
    // "n/a" -- see doc/ethernet-pdu-session/28-packet-observers.md).
    auto frameIdHeader = pkt->popAtFront<TsnEthFrameIdHeader>();
    if (frameIdHeader != nullptr) {
        emit(packetDelayReceivedSignal_, (long)frameIdHeader->getFrameId());
        emit(appPacketReceivedSignal_, 1L);
    }

    bool hasDelay = false;
    simtime_t delay = SIMTIME_ZERO;
    if (auto creationTag = pkt->findTag<CreationTimeTag>()) {
        delay = simTime() - creationTag->getCreationTime();
        hasDelay = true;
        emit(frameDelaySignal_, delay);
    }
    emit(framesReceivedSignal_, 1L);

    // Real 3GPP Ethernet PDU Session work: EV_ERROR for visibility only, see
    // the identical note in buildFrame() and
    // doc/ethernet-pdu-session/27-mac-visibility-logging.md. This is the
    // delivery evidence point: destMac/srcMac/vid/pcp are read back from the
    // real wire bytes just popped, not from a metadata shortcut.
    EV_ERROR << "[ETH-PDU][RX] ue.l2App -- srcMac=" << addrFields->getSrc().str()
              << " destMac=" << addrFields->getDest().str()
              << " vid=" << vlanHeader->getVid() << " pcp=" << (int)vlanHeader->getPcp()
              << " delay=" << (hasDelay ? delay.str() : "n/a (tag lost in RLC)") << endl;
    delete pkt;
}
