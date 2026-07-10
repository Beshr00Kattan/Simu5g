//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: AF-side (N6) source application.
// See doc/ethernet-pdu-session/03-server-attachment.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#include "apps/l2/TsnEtherSourceApp.h"

#include <inet/common/TimeTag_m.h>
#include <inet/common/packet/Packet.h>
#include <inet/common/packet/chunk/ByteCountChunk.h>
#include <inet/linklayer/ethernet/common/EthernetMacHeader_m.h>
#include <inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h>
#include <inet/networklayer/common/L3AddressResolver.h>

using namespace inet;

simsignal_t TsnEtherSourceApp::framesSentSignal_ = registerSignal("framesSent");

Define_Module(TsnEtherSourceApp);

TsnEtherSourceApp::~TsnEtherSourceApp()
{
    cancelAndDelete(selfSender_);
}

void TsnEtherSourceApp::initialize(int stage)
{
    cSimpleModule::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        srcMac_.setAddress(par("srcMac").stringValue());
        destMac_.setAddress(par("destMac").stringValue());
        vid_ = par("vid");
        pcp_ = par("pcp");
        etherType_ = par("etherType");
        packetLength_ = B(par("packetLength").intValue());
        sendInterval_ = par("sendInterval");
        startTime_ = par("startTime");
        stopTime_ = par("stopTime");
        localPort_ = par("localPort");
        destPort_ = par("destPort");
    }
    else if (stage == INITSTAGE_APPLICATION_LAYER) {
        // Same resolve-by-module-name idiom simu5g.apps.cbr.CbrSender already
        // uses (CbrSender.cc:105) to reach a destination host by its network
        // name (e.g. "upf") rather than a literal, fragile IP address.
        destAddress_ = L3AddressResolver().resolve(par("destAddress").stringValue());
        socket_.setOutputGate(gate("socketOut"));
        socket_.bind(localPort_ >= 0 ? localPort_ : 0);

        selfSender_ = new cMessage("tsnEtherSourceSendTimer");
        scheduleAt(std::max(startTime_, simTime()), selfSender_);
    }
}

void TsnEtherSourceApp::scheduleNextSend()
{
    simtime_t next = simTime() + sendInterval_;
    if (stopTime_ > SIMTIME_ZERO && next > stopTime_)
        return;
    scheduleAt(next, selfSender_);
}

void TsnEtherSourceApp::sendFrame()
{
    // Builds the identical real, byte-accurate 802.1Q-tagged Ethernet II
    // frame TsnEtherApp::buildFrame() builds (see that file's documentation
    // entry for the field-by-field rationale) -- the only difference here is
    // that this Packet becomes the *payload* of a UDP datagram instead of
    // being handed directly to a network-layer dispatcher, per this file's
    // .ned-header rationale for why the AF-to-UPF (N6) leg needs an IP/UDP
    // shell in this fixed topology while the 5G-internal legs do not.
    Packet *pkt = new Packet("TsnEthFrame");
    pkt->insertAtBack(makeShared<ByteCountChunk>(packetLength_));

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

    pkt->addTagIfAbsent<CreationTimeTag>()->setCreationTime(simTime());

    socket_.sendTo(pkt, destAddress_, destPort_);
    emit(framesSentSignal_, 1L);
}

void TsnEtherSourceApp::handleMessage(cMessage *msg)
{
    if (msg == selfSender_) {
        sendFrame();
        scheduleNextSend();
        return;
    }
    // Nothing is expected on socketIn for a pure source; discard defensively.
    delete msg;
}
