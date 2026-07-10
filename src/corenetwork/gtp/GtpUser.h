//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

#ifndef __GTP_USER_H_
#define __GTP_USER_H_

#include <map>
#include <omnetpp.h>
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "corenetwork/gtp/GtpUserMsg_m.h"
#include <inet/common/ModuleAccess.h>
#include <inet/networklayer/common/NetworkInterface.h>
#include <inet/networklayer/common/L3AddressResolver.h>
#include "common/binder/Binder.h"
#include <inet/linklayer/common/InterfaceTag_m.h>

/**
 * GtpUser is used for building data tunnels between GTP peers.
 * GtpUser can receive two kind of packets:
 * a) IP datagram from a trafficFilter. Those packets are labeled with a tftId
 * b) GtpUserMsg from Udp-IP layers.
 *
 */
class GtpUser : public omnetpp::cSimpleModule
{
    inet::UdpSocket socket_;
    int localPort_;

    // reference to the LTE Binder module
    Binder* binder_;

    // the GTP protocol Port
    unsigned int tunnelPeerPort_;

    // IP address of the gateway to the Internet
    inet::L3Address gwAddress_;

    // specifies the type of the node that contains this filter (it can be ENB or PGW)
    CoreNodeType ownerType_;

    CoreNodeType selectOwnerType(const char * type);

    // if this module is on BS, this variable includes the ID of the BS
    MacNodeId myMacNodeID;

    inet::NetworkInterface* ie_;

    // Real 3GPP Ethernet PDU Session work (experimental N3 redesign): "gtp"
    // (standard GTP-U/UDP/IP) or "ethernet" (IP-less MAC-in-MAC on N3). See
    // GtpUser.ned and doc/ethernet-pdu-session/31-macinmac-n3.md.
    std::string n3Transport_;

  protected:

    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *msg) override;

    // receive and IP Datagram from the traffic filter, encapsulates it in a GTP-U packet than forwards it to the proper next hop
    void handleFromTrafficFlowFilter(inet::Packet * datagram);

    // receive a GTP-U packet from Udp, reads the TEID and decides whether performing label switching or removal
    void handleFromUdp(inet::Packet * gtpMsg);

    // Real 3GPP Ethernet PDU Session work: MAC-in-MAC N3 counterparts of the
    // two methods above. sendOverN3Ethernet() wraps the already-built GtpUserMsg
    // packet in an outer EthernetMacHeader and sends it out n3EthOut (UPF side).
    // handleFromN3Ethernet() strips that outer header on the gNB side, then
    // hands the inner GtpUserMsg packet to decapAndDeliver().
    void sendOverN3Ethernet(inet::Packet * gtpPacket);
    void handleFromN3Ethernet(inet::Packet * frame);
    // Shared decapsulation used by BOTH handleFromUdp (GTP-U mode) and
    // handleFromN3Ethernet (Ethernet mode): pops the GtpUserMsg (+ optional
    // PDU-session-container ext header), resolves the PDU session type from
    // the teid, and locally delivers the reconstructed inner frame.
    void decapAndDeliver(inet::Packet * innerWithGtpHeader);

    // detect outgoing interface name (CellularNic)
    inet::NetworkInterface *detectInterface();
};

#endif
