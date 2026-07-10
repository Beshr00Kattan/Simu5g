//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: see PacketDelayObserver.ned and
// doc/ethernet-pdu-session/28-packet-observers.md.
//

#ifndef COMMON_PACKETDELAYOBSERVER_PACKETDELAYOBSERVER_H_
#define COMMON_PACKETDELAYOBSERVER_PACKETDELAYOBSERVER_H_

#include <omnetpp.h>
#include <map>

using namespace omnetpp;

class PacketDelayObserver : public cSimpleModule, public cListener
{
  private:
    std::map<long, simtime_t> sentTimes;
    cMessage* cleanupTimer = nullptr;

    simsignal_t delaySignal;
    simsignal_t packetsSentByObserverSignal;
    simsignal_t packetsReceivedByObserverSignal;

    long numPacketsSent = 0;
    long numPacketsReceived = 0;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage* msg) override;
    virtual void receiveSignal(cComponent* source, simsignal_t signalID, long value, cObject* details) override;
    virtual void finish() override;
    virtual ~PacketDelayObserver() override;
};

#endif
