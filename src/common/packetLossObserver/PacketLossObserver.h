//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: see PacketLossObserver.ned and
// doc/ethernet-pdu-session/28-packet-observers.md.
//

#ifndef COMMON_PACKETLOSSOBSERVER_PACKETLOSSOBSERVER_H_
#define COMMON_PACKETLOSSOBSERVER_PACKETLOSSOBSERVER_H_

#include <omnetpp.h>

using namespace omnetpp;

class PacketLossObserver : public cSimpleModule, public cListener {
  private:
    long totalSent;
    long totalReceived;

    cMessage* cleanupTimer;

    simsignal_t totalLossSignal;
    simsignal_t lossPercentageSignal;

  public:
    virtual ~PacketLossObserver();
    virtual void initialize() override;
    virtual void handleMessage(cMessage* msg) override;
    virtual void finish() override;
    virtual void receiveSignal(cComponent* source, simsignal_t signalID, long value, cObject* details) override;
};

#endif /* COMMON_PACKETLOSSOBSERVER_PACKETLOSSOBSERVER_H_ */
