//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / PCP-based priority queueing.
// See doc/ethernet-pdu-session/12-pcp-priority-queue.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#include "apps/l2/TsnPcpClassifier.h"

#include <inet/linklayer/common/PcpTag_m.h>

using namespace inet;

Define_Module(TsnPcpClassifier);

void TsnPcpClassifier::initialize(int stage)
{
    PacketClassifierBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL)
        highPriorityMinPcp_ = par("highPriorityMinPcp");
}

int TsnPcpClassifier::classifyPacket(Packet *packet)
{
    // Real 3GPP Ethernet PDU Session work (topology consolidation, second
    // fix): the first version of this classifier peeked through the wire
    // chunk stack (EthernetMacHeader, then Ieee8021qTagEpdHeader) to reach
    // the PCP field. That kept throwing "Cannot convert chunk from type
    // ByteCountChunk to type Ieee8021qTagEpdHeader" even after the frame
    // format was made internally consistent (see
    // doc/ethernet-pdu-session/16-router-removal-unified-frame.md) -- the
    // packet reaching this classifier, once inside inet::PriorityQueue,
    // no longer exposes the same chunk boundaries buildFrame() created
    // (observed, not fully root-caused: consistent with INET's chunk
    // simplification/immutability machinery collapsing adjacent chunks
    // into a canonical byte-region once the packet is queued).
    //
    // Real 3GPP Ethernet PDU Session work (fix): use inet::PcpReq instead --
    // the real, purpose-built INET tag "may be present on a packet from the
    // application to the mac protocol" to determine the PCP a link-layer
    // module should use to send it (see PcpTag.msg's own doc comment). This
    // is the same category of mechanism inet::QosClassifier/RandomQosClassifier
    // use, and is standard practice precisely because re-parsing wire bytes
    // for a QUEUEING module's *own* internal decision is both unnecessary
    // and -- as observed here -- chunk-machinery-fragile. The real
    // Ieee8021qTagEpdHeader stays on the wire in the packet's data
    // (TsnEtherApp::buildFrame() still inserts it); this tag is a companion,
    // not a replacement.
    auto pcpReq = packet->findTag<PcpReq>();
    int pcp = (pcpReq != nullptr) ? pcpReq->getPcp() : 0;
    return (pcp >= highPriorityMinPcp_) ? 0 : 1;
}
