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
package simu5g.nodes;

import inet.common.MessageDispatcher;
import inet.linklayer.ppp.PppInterface;
import inet.linklayer.ethernet.EthernetInterface;
import inet.linklayer.ethernet.common.MacForwardingTable;
import inet.transportlayer.udp.Udp;
import inet.node.base.NetworkLayerNodeBase;
import simu5g.corenetwork.gtp.GtpUser;
import simu5g.corenetwork.trafficFlowFilter.TrafficFlowFilter;

//
// Implements a simple model of the 5GC UPF
//
// The core network can contain multiple UPFs: see example in "simulations/NR/standalone"
// UPF not connected to an external Data Network are intermediate UPFs. In the current version, intermediate UPFs just act
// like IP routers. 
// UPFs connected to an external Data Network take care of (de)tunneling packets according to GTP
//
module Upf extends NetworkLayerNodeBase
{
    parameters:
        @display("bgb=1348.9225,1103.51;i=abstract/switch");
        @figure[applicationLayer](type=rectangle; pos=250,6; size=1000,130; lineColor=#808080; cornerRadius=5; fillColor=#ffff00; fillOpacity=0.1);
        @figure[applicationLayer.title](type=text; pos=1245,11; anchor=ne; text="application layer");
        @figure[transportLayer](type=rectangle; pos=250,156; size=1000,130; fillColor=#ff0000; lineColor=#808080; cornerRadius=5; fillOpacity=0.1);
        @figure[transportLayer.title](type=text; pos=1245,161; anchor=ne; text="transport layer");
        @figure[submodules];

        string nodeType = default("UPF");   // do NOT change!
        forwarding = default(true);
        multicastForwarding = default(false);
        // Real 3GPP Ethernet PDU Session work: whether this UPF instance has a
        // real TSN Ethernet port (the dedicated tsnEth interface below). True
        // only on the real, DN-facing "upf" in SingleCell_Standalone.ned;
        // false (default) on "iUpf", a pure intermediate relay with nothing to
        // connect it to. See doc/ethernet-pdu-session/15-real-switch-topology.md.
        bool hasTsnEthPort = default(false);
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): whether this UPF
        // reaches the gNB over an IP-less Ethernet N3 leg (the dedicated n3Eth
        // interface below + gtp_user.n3Transport="ethernet") instead of the
        // standard GTP-U/UDP/IP over pppIf. See
        // doc/ethernet-pdu-session/31-macinmac-n3.md.
        bool hasEthernetN3 = default(false);

    gates:
        // this gate is used for the UPF connected to the external data network
        // it must be left unconnected for intermediate UPFs
        inout filterGate @loose @labels(PppFrame-conn);
        // Real 3GPP Ethernet PDU Session work: real, pure-Ethernet TSN port to
        // the tsnSwitch (no IP). Present only when hasTsnEthPort is true.
        inout tsnEthg @loose @labels(EtherFrame-conn);
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): the IP-less
        // Ethernet N3 port toward the gNB. Present only when hasEthernetN3.
        inout n3Ethg @loose @labels(EtherFrame-conn);

    submodules:
        pppIf: PppInterface {
            parameters:
                @display("p=1150,525");
        }
        udp: Udp {
            parameters:
                @display("p=374.0425,201.4075");
        }
        trafficFlowFilter: TrafficFlowFilter {
            parameters:
                @display("p=881.7925,79.5475");
                ownerType = parent.nodeType;
                // Real 3GPP Ethernet PDU Session work: points TrafficFlowFilter at
                // the macTable submodule below, for real MAC-address learning of
                // Ethernet PDU session traffic (TS 23.501 SS5.6.10.2). See
                // doc/ethernet-pdu-session/06-upf-mactable.md.
                macForwardingTableModule = "^.macTable";
        }
        // Real 3GPP Ethernet PDU Session work: reused, real INET learning/aging
        // MAC forwarding table (inet::MacForwardingTable), the same one
        // inet::EthernetSwitch's bridging submodule uses -- see
        // doc/ethernet-pdu-session/06-upf-mactable.md for why the UPF, not a
        // separate switch node, is the correct place for this per TS 23.501.
        macTable: MacForwardingTable {
            parameters:
                @display("p=881.7925,180");
                interfaceTableModule = "^.interfaceTable";
        }
        // Real 3GPP Ethernet PDU Session work: a real, DEDICATED Ethernet
        // interface for the TSN/N6 leg, wired DIRECTLY to trafficFlowFilter --
        // deliberately NOT the standard eth[]/EthernetEncapsulation/nl chain.
        // Rationale (see doc/ethernet-pdu-session/15-real-switch-topology.md):
        // the standard chain makes inet::EthernetEncapsulation register
        // Protocol::ethernetMac on the shared "nl" MessageDispatcher, and a
        // MessageDispatcher permits only ONE consumer per {protocol,
        // servicePrimitive} -- so trafficFlowFilter registering ethernetMac
        // there too collides ("protocol is already registered"). A dedicated,
        // direct-wired interface avoids "nl" and encap entirely: no protocol
        // registration, no collision. mac.promiscuous=true because the UPF is
        // a bridge/gateway port -- it must accept frames whose dest MAC is a
        // UE's tsnMac (behind the radio stack), not the port's own address.
        tsnEth: EthernetInterface if hasTsnEthPort {
            parameters:
                @display("p=1150,340");
                interfaceTableModule = "^.interfaceTable";
                mac.promiscuous = true;
        }
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): a real, dedicated
        // Ethernet interface for the IP-less N3 leg toward the gNB, direct-wired
        // to gtp_user (no "nl"/encap, exactly like tsnEth). gtp_user builds the
        // outer EthernetMacHeader itself; this interface just transmits it.
        // See doc/ethernet-pdu-session/31-macinmac-n3.md.
        n3Eth: EthernetInterface if hasEthernetN3 {
            parameters:
                @display("p=1150,420");
                interfaceTableModule = "^.interfaceTable";
                mac.promiscuous = true;
        }
        gtp_user: GtpUser {
            parameters:
                @display("p=525,80");
        }
        tn: MessageDispatcher {
            parameters:
                @display("p=750,296;b=1000,5,,,,1");
        }
        at: MessageDispatcher {
            parameters:
                @display("p=750,146;b=1000,5,,,,1");
        }

    connections:

        gtp_user.socketOut --> at.in++;
        gtp_user.socketIn <-- at.out++;

        pppIf.upperLayerOut --> trafficFlowFilter.internetFilterGateIn;
        pppIf.upperLayerIn <-- gtp_user.pppGate;

        // Real 3GPP Ethernet PDU Session work: real Ethernet frames arriving
        // via the dedicated tsnEth interface reach trafficFlowFilter through a
        // DIRECT wire -- no "nl", no EthernetEncapsulation, no protocol
        // registration (and therefore no dispatcher collision). tsnEth's MAC
        // passes the received frame up WITH its outer EthernetMacHeader intact
        // (basic EthernetMac does not decapsulate -- that's encap's job, which
        // this path deliberately skips); trafficFlowFilter pops that outer
        // header itself. See doc/ethernet-pdu-session/15-real-switch-topology.md.
        tsnEth.upperLayerOut --> trafficFlowFilter.ethernetFrameIn if hasTsnEthPort;
        trafficFlowFilter.ethernetFrameOut --> tsnEth.upperLayerIn if hasTsnEthPort;
        tsnEth.phys <--> tsnEthg if hasTsnEthPort;

        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): gtp_user emits the
        // outer-Ethernet-wrapped tunnel frame straight onto n3Eth (downlink),
        // no IP stack involved. See doc/ethernet-pdu-session/31-macinmac-n3.md.
        gtp_user.n3EthOut --> n3Eth.upperLayerIn if hasEthernetN3;
        n3Eth.phys <--> n3Ethg if hasEthernetN3;

        trafficFlowFilter.gtpUserGateOut --> gtp_user.trafficFlowFilterGate;

        // at dispatcher to transport layer connections
        at.out++ --> udp.appIn;
        at.in++ <-- udp.appOut;

        // transport layer to tn dispatcher connections
        udp.ipOut --> tn.in++;
        udp.ipIn <-- tn.out++;

        // tn dispatcher to network layer connections
        tn.out++ --> ipv4.transportIn if hasIpv4;
        tn.in++ <-- ipv4.transportOut if hasIpv4;

        tn.out++ --> ipv6.transportIn if hasIpv6;
        tn.in++ <-- ipv6.transportOut if hasIpv6;

        tn.out++ --> generic.transportIn if hasGn;
        tn.in++ <-- generic.transportOut if hasGn;

        tn.out++ --> nl.in++;
        tn.in++ <-- nl.out++;

        pppIf.phys <--> filterGate;
}
