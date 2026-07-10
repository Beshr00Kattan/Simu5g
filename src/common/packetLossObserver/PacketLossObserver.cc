//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: see PacketLossObserver.ned and
// doc/ethernet-pdu-session/28-packet-observers.md.
//

#include "PacketLossObserver.h"

Define_Module(PacketLossObserver);

PacketLossObserver::~PacketLossObserver()
{
    cancelAndDelete(cleanupTimer);
}

void PacketLossObserver::initialize()
{
    totalSent = 0;
    totalReceived = 0;

    totalLossSignal = registerSignal("totalPacketLoss");
    lossPercentageSignal = registerSignal("packetLossPercentage");

    // Real 3GPP Ethernet PDU Session work: system-module-level subscription
    // -- see the identical rationale in PacketDelayObserver.cc.
    getSystemModule()->subscribe("appPacketSent", this);
    getSystemModule()->subscribe("appPacketReceived", this);

    cleanupTimer = new cMessage("cleanupTimer");
    scheduleAt(simTime() + 5.0, cleanupTimer);
}

void PacketLossObserver::handleMessage(cMessage* msg)
{
    if (msg == cleanupTimer) {
        EV_INFO << "PacketLossObserver: running total sent=" << totalSent
                 << " received=" << totalReceived
                 << " loss=" << (totalSent - totalReceived) << endl;
        scheduleAt(simTime() + 5.0, cleanupTimer);
    }
}

void PacketLossObserver::finish()
{
    long loss = totalSent - totalReceived;
    double lossPercentage = (totalSent == 0) ? 0.0 : (100.0 * loss / totalSent);

    emit(totalLossSignal, loss);
    emit(lossPercentageSignal, lossPercentage);

    recordScalar("Total Packet Loss", loss);
    recordScalar("Packet Loss Percentage", lossPercentage);
}

void PacketLossObserver::receiveSignal(cComponent* source, simsignal_t signalID, long value, cObject* details)
{
    const char* signalName = getSignalName(signalID);

    if (strcmp(signalName, "appPacketSent") == 0) {
        totalSent += 1;
    }
    else if (strcmp(signalName, "appPacketReceived") == 0) {
        totalReceived += 1;
    }
}
