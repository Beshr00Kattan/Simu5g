//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / real 802.1Q VLAN tagging.
// See doc/ethernet-pdu-session/00-foundation-l2app.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#ifndef _TSNETHERAPP_H_
#define _TSNETHERAPP_H_

#include "apps/l2/L2App.h"

// L2App + a real, serialized IEEE 802.1Q VLAN tag. See TsnEtherApp.ned.
class TsnEtherApp : public L2App
{
  protected:
    int vid_ = 0;
    int pcp_ = 0;

    // Real 3GPP Ethernet PDU Session work: network-wide packet delay/loss
    // observer support (PacketDelayObserver / PacketLossObserver, see
    // src/common/packetDelayObserver, src/common/packetLossObserver, and
    // doc/ethernet-pdu-session/28-packet-observers.md). Emitted network-wide
    // (they bubble up to the system module, where the observers subscribe),
    // keyed by TsnEthFrameIdHeader.frameId -- real, serialized payload data,
    // not a tag -- so measurement survives the RLC round-trip regardless of
    // whether CreationTimeTag does.
    static omnetpp::simsignal_t packetDelaySentSignal_;
    static omnetpp::simsignal_t appPacketSentSignal_;
    static omnetpp::simsignal_t packetDelayReceivedSignal_;
    static omnetpp::simsignal_t appPacketReceivedSignal_;

  protected:
    virtual void initialize(int stage) override;
    virtual inet::Packet *buildFrame() override;
    virtual void handleFrame(inet::Packet *pkt) override;
};

#endif
