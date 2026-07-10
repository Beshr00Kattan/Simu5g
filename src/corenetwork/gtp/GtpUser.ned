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

package simu5g.corenetwork.gtp;

//
// Implements a simplified version of the GTP-U protocol
//
simple GtpUser
{
    parameters:
        string interfaceTableModule;            // path to the InterfaceTable module
        string ipOutInterface = default("");    // optional: manual specification of outgoing interface for received IP
        int localPort = default(31);
        int tunnelPeerPort = default(31);
        bool filter = default(true);

        // Real 3GPP Ethernet PDU Session work (experimental N3 redesign):
        // selects how the N3 (UPF<->gNB) leg carries the tunneled frame.
        //   "gtp"      -- the standard, 3GPP-compliant GTP-U/UDP/IP tunnel
        //                 (unchanged baseline; the revert-checkpoint behavior).
        //   "ethernet" -- IP-LESS MAC-in-MAC (IEEE 802.1ah-style): the
        //                 GtpUserMsg (its teid = UE MacNodeId acting as the
        //                 802.1ah I-SID) is wrapped in an OUTER EthernetMacHeader
        //                 and sent over a real Ethernet link via n3EthOut/n3EthIn,
        //                 with NO UDP/IP anywhere. See
        //                 doc/ethernet-pdu-session/31-macinmac-n3.md.
        // Deliberately non-3GPP (N3 is defined as IP transport) -- an
        // experimental research architecture for "whole Ethernet, no IP".
        string n3Transport @enum("gtp","ethernet") = default("gtp");

        @display("i=block/tunnel");

    gates:
        output socketOut;
        input socketIn;

        input trafficFlowFilterGate;
        output pppGate;

        // Real 3GPP Ethernet PDU Session work: the IP-less N3 Ethernet leg
        // (used only when n3Transport=="ethernet"). n3EthOut: UPF egress toward
        // the gNB (downlink); n3EthIn: gNB ingress from the UPF. @loose so the
        // standard GTP-U mode leaves them unconnected without error.
        output n3EthOut @loose;
        input n3EthIn @loose;
}

//
// Implements a simplified version of the GTP-U protocol for tunneling packets over X2 (for handover)
//
simple GtpUserX2
{
    parameters:
        int localPort = default(32);
        int tunnelPeerPort = default(32);
        
        @display("i=block/tunnel");

    gates:
        output socketOut;
        input socketIn;

        output lteStackOut;
        input lteStackIn;
}
