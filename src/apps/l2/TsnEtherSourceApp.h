//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: AF-side (N6) source application.
// See doc/ethernet-pdu-session/03-server-attachment.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#ifndef _TSNETHERSOURCEAPP_H_
#define _TSNETHERSOURCEAPP_H_

#include <omnetpp.h>
#include <inet/common/INETDefs.h>
#include <inet/transportlayer/contract/udp/UdpSocket.h>
#include <inet/networklayer/common/L3Address.h>
#include <inet/linklayer/common/MacAddress.h>

// See TsnEtherSourceApp.ned for why this exists alongside TsnEtherApp.
class TsnEtherSourceApp : public omnetpp::cSimpleModule
{
    inet::UdpSocket socket_;
    omnetpp::cMessage *selfSender_ = nullptr;

    inet::MacAddress srcMac_;
    inet::MacAddress destMac_;
    int vid_ = 0;
    int pcp_ = 0;
    int etherType_ = 0;
    inet::B packetLength_;
    omnetpp::simtime_t sendInterval_;
    omnetpp::simtime_t startTime_;
    omnetpp::simtime_t stopTime_;

    int localPort_ = -1;
    int destPort_ = -1;
    inet::L3Address destAddress_;

    static omnetpp::simsignal_t framesSentSignal_;

    void sendFrame();
    void scheduleNextSend();

  public:
    ~TsnEtherSourceApp();

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *msg) override;
};

#endif
