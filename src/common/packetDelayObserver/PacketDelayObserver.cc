//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: see PacketDelayObserver.ned and
// doc/ethernet-pdu-session/28-packet-observers.md.
//

#include "PacketDelayObserver.h"

Define_Module(PacketDelayObserver);

PacketDelayObserver::~PacketDelayObserver()
{
    cancelAndDelete(cleanupTimer);
}

void PacketDelayObserver::initialize()
{
    delaySignal = registerSignal("packetDelayByObserver");
    packetsSentByObserverSignal = registerSignal("packetsSentByObserver");
    packetsReceivedByObserverSignal = registerSignal("packetsReceivedByObserver");

    // Real 3GPP Ethernet PDU Session work: subscribing at the SYSTEM MODULE
    // (not a parent/sibling module) is what lets this one instance observe
    // every app in the network -- TsnEtherApp on the server/UE and
    // CbrSender/CbrReceiver alike -- without any NED-level wiring to them.
    // OMNeT++ signals emitted by any module bubble up through its ancestor
    // chain to the system module unless explicitly blocked, so subscribing
    // there is equivalent to subscribing to "the whole simulation".
    cModule* systemModule = getSystemModule();
    systemModule->subscribe("packetDelaySentSignal", this);
    systemModule->subscribe("packetDelayReceivedSignal", this);

    cleanupTimer = new cMessage("cleanupTimer");
    scheduleAt(simTime() + 5.0, cleanupTimer);
}

void PacketDelayObserver::handleMessage(cMessage* msg)
{
    if (msg == cleanupTimer) {
        simtime_t now = simTime();
        for (auto it = sentTimes.begin(); it != sentTimes.end(); ) {
            if (now - it->second > 10.0) {
                EV_WARN << "PacketDelayObserver: cleaning up stale, never-received frameId=" << it->first << endl;
                it = sentTimes.erase(it);
            }
            else {
                ++it;
            }
        }
        scheduleAt(simTime() + 5.0, cleanupTimer);
    }
}

void PacketDelayObserver::receiveSignal(cComponent* source, simsignal_t signalID, long value, cObject* details)
{
    static simsignal_t sentSig = registerSignal("packetDelaySentSignal");
    static simsignal_t recvSig = registerSignal("packetDelayReceivedSignal");

    if (signalID == sentSig) {
        sentTimes[value] = simTime();
        numPacketsSent++;
        emit(packetsSentByObserverSignal, numPacketsSent);
        EV_INFO << "PacketDelayObserver: sent recorded, frameId=" << value << endl;
    }
    else if (signalID == recvSig) {
        auto it = sentTimes.find(value);
        if (it != sentTimes.end()) {
            simtime_t delay = simTime() - it->second;
            emit(delaySignal, delay);
            numPacketsReceived++;
            emit(packetsReceivedByObserverSignal, numPacketsReceived);
            EV_INFO << "PacketDelayObserver: received frameId=" << value << ", delay=" << delay << "s" << endl;
            sentTimes.erase(it);
        }
        else {
            EV_WARN << "PacketDelayObserver: received untracked frameId=" << value
                     << " (sent before this observer's cleanup window, or from a source not emitting packetDelaySentSignal)" << endl;
        }
    }
}

void PacketDelayObserver::finish()
{
    emit(packetsSentByObserverSignal, numPacketsSent);
    emit(packetsReceivedByObserverSignal, numPacketsReceived);
}
