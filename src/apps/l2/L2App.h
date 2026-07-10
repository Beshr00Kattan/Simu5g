//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer A / L2App foundation module.
// See doc/ethernet-pdu-session/00-foundation-l2app.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#ifndef _L2APP_H_
#define _L2APP_H_

#include <omnetpp.h>
#include <inet/common/INETDefs.h>
#include <inet/common/packet/Packet.h>
#include <inet/linklayer/common/MacAddress.h>
#include <inet/networklayer/common/InterfaceTable.h>

// Generic Ethernet-frame-level application (no IP/UDP). See L2App.ned.
class L2App : public omnetpp::cSimpleModule
{
  protected:
    inet::IInterfaceTable *ift_ = nullptr;
    int interfaceId_ = -1; // resolved once, from the "interfaceName" parameter

    inet::MacAddress srcMac_;
    inet::MacAddress destMac_;
    omnetpp::simtime_t sendInterval_;
    inet::B packetLength_;
    int etherType_ = 0;
    omnetpp::simtime_t startTime_;
    omnetpp::simtime_t stopTime_;

    omnetpp::cMessage *selfSender_ = nullptr;
    uint32_t seqCounter_ = 0;

    static omnetpp::simsignal_t framesSentSignal_;
    static omnetpp::simsignal_t framesReceivedSignal_;
    static omnetpp::simsignal_t framesDroppedSignal_;
    static omnetpp::simsignal_t frameDelaySignal_;

  public:
    L2App();
    ~L2App();

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *msg) override;
    virtual void scheduleNextSend();

    // Builds one outgoing Ethernet frame. Subclasses (e.g. TsnEtherApp)
    // override this to push additional real wire headers (VLAN tag, ...)
    // in front of the EthernetMacHeader built here.
    virtual inet::Packet *buildFrame();

    // Validates/consumes one incoming Ethernet frame. Subclasses override
    // this to pop the additional headers they pushed in buildFrame().
    virtual void handleFrame(inet::Packet *pkt);
};

#endif
