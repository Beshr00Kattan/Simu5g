//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / PCP-based priority queueing.
// See doc/ethernet-pdu-session/12-pcp-priority-queue.md for the full
// baseline/modified/rationale documentation entry for this file.
//

package simu5g.apps.l2;

import inet.queueing.base.PacketClassifierBase;
import inet.queueing.contract.IPacketClassifier;

//
// Classifies a TsnEtherSourceApp-tunneled packet ([Ipv4][Udp][EthernetMacAddressFields]
// [Ieee8021qTagEpdHeader][payload]) into an output gate by its real,
// still-on-the-wire 802.1Q PCP value: PCP >= highPriorityMinPcp goes to gate 0
// (the high-priority queue), everything else to gate 1. Unlike INET's own
// inet.queueing.classifier.PcpClassifier (which reads an out-of-band PcpInd/PcpReq
// tag, only ever populated after a Ieee8021qTagEpdHeaderChecker has already popped
// the real header off the packet -- something this project deliberately never
// does, since the VLAN tag must still be on the wire when it reaches the UE), this
// classifier peeks past this project's own known IP/UDP tunnel header to read the
// real PCP field without disturbing it.
//
// Deliberately out of scope: IEEE 802.1Qbv gate-control-list / time-synchronized
// scheduling. This classifier only feeds a strict-priority scheduler (two static
// queues) -- there is no time-gating of either queue. See the final report's
// dedicated scope section for the complexity-vs-thesis-scope rationale.
//
simple TsnPcpClassifier extends PacketClassifierBase like IPacketClassifier
{
    parameters:
        int highPriorityMinPcp = default(4); // PCP values >= this go to gate 0 (real IEEE 802.1Q priority differentiation, no time gating)
        @class(TsnPcpClassifier);
}
