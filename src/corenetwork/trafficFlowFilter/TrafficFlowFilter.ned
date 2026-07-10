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

package simu5g.corenetwork.trafficFlowFilter;

simple TrafficFlowFilter
{
    parameters:
        @display("i=block/filter");

        string ownerType; // must be one between ENODEB or GW
        bool fastForwarding = default(true);

        // Real 3GPP Ethernet PDU Session work: path to a real
        // inet::MacForwardingTable module used for Ethernet PDU session
        // MAC-address learning (TS 23.501 SS5.6.10.2). Only used on the
        // Ethernet classification branch; the pre-existing IP classification
        // branch below is completely unaffected. See
        // doc/ethernet-pdu-session/06-upf-mactable.md and 07-tff-ethernet-branch.md.
        string macForwardingTableModule = default("");
    gates:
        input internetFilterGateIn;
        // Real 3GPP Ethernet PDU Session work: dedicated gate pair for real,
        // pure-Ethernet frames arriving via the real tsnSwitch/eth[0] path
        // (no IP/UDP shell to strip -- contrast with internetFilterGateIn,
        // which only ever carries IP traffic). Wired to "nl" in Upf.ned,
        // registered for Protocol::ethernetMac in initialize(). See
        // doc/ethernet-pdu-session/15-real-switch-topology.md.
        //
        // Bug fix (user-reported): ethernetFrameIn was originally the only
        // gate connected to "nl" for this peer (nl.out++ only). inet's
        // MessageDispatcher keeps its in[]/out[] gate vectors as matched
        // pairs -- every other consumer (ipv4, cb, tn) connects both
        // directions together, so the two vectors stay the same size. A
        // one-directional connection grows out[] without growing in[],
        // desyncing the pairing and crashing registerProtocol()'s internal
        // bookkeeping with "Gate index 3 out of range ... size 3" the
        // moment it looked for the matching in[] slot. ethernetFrameOut is
        // functionally unused (TrafficFlowFilter never sends anything back
        // up through "nl") but must exist and be connected so both vectors
        // grow in lockstep.
        input ethernetFrameIn @loose;
        output ethernetFrameOut @loose;
        output gtpUserGateOut;
}