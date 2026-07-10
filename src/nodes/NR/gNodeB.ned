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


package simu5g.nodes.NR;

import inet.linklayer.ethernet.EthernetInterface;
import simu5g.nodes.eNodeB;

//
// gNodeB Module
//
module gNodeB extends eNodeB
{
    parameters:
        @networkNode();
        @display("i=device/antennatower;bgb=1260,600");
		StatsCollectorType = default("GNodeBStatsCollector");
        gateway = default("upf"); // change this path if the gateway node is inside a compound module
        nodeType = "GNODEB";        // DO NOT CHANGE
        nicType = default("NRNicEnb");
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): whether this gNB
        // receives N3 traffic over the IP-less Ethernet leg (dedicated n3Eth +
        // gtpUser.n3Transport="ethernet") instead of GTP-U/UDP/IP over pppIf.
        // Added here in the subclass so the shared eNodeB.ned is untouched.
        // See doc/ethernet-pdu-session/31-macinmac-n3.md.
        bool hasEthernetN3 = default(false);
    gates:
        // IP-less Ethernet N3 port from the UPF (present only when hasEthernetN3).
        inout n3Ethg @loose @labels(EtherFrame-conn);
    submodules:
        // Dedicated Ethernet interface for the IP-less N3 leg, direct-wired to
        // the inherited gtpUser (no "nl"/encap). promiscuous: the outer frame's
        // dest is broadcast (point-to-point backbone), so accept regardless.
        n3Eth: EthernetInterface if hasEthernetN3 {
            parameters:
                @display("p=100,500");
                interfaceTableModule = "^.interfaceTable";
                mac.promiscuous = true;
        }
    connections:
        // Received MAC-in-MAC frames go straight to gtpUser (inherited from
        // eNodeB), which strips the outer Ethernet header and decapsulates --
        // no IP stack traversed on ingress. gtpUser's decapsulated output still
        // uses its existing pppGate -> nl -> cellularNic path (unchanged).
        n3Eth.upperLayerOut --> gtpUser.n3EthIn if hasEthernetN3;
        n3Eth.phys <--> n3Ethg if hasEthernetN3;
}
