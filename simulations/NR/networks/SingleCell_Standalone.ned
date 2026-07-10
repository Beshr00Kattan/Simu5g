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
package simu5g.simulations.NR.networks;

import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.networklayer.ipv4.RoutingTableRecorder;
import inet.node.ethernet.Eth10G;
import inet.node.ethernet.EthernetSwitch;
import simu5g.common.binder.Binder;
import simu5g.common.carrierAggregation.CarrierAggregation;
import simu5g.common.packetDelayObserver.PacketDelayObserver;
import simu5g.common.packetLossObserver.PacketLossObserver;
import simu5g.nodes.EthernetPduSessionHost;
import simu5g.nodes.Upf;
import simu5g.nodes.NR.gNodeB;
import simu5g.nodes.NR.NRUe;
import simu5g.nodes.backgroundCell.BackgroundCell;
import simu5g.world.radio.LteChannelControl;

// SingleCell_standalone
//
// This network represents the deployment option 3 in 3GPP 38.801
//
network SingleCell_Standalone
{
    parameters:
        int numUe = default(1);
        int numBgCells = default(0);
        @display("i=block/network2;bgb=1000,1000;bgi=background/pisa");
    submodules:
        channelControl: LteChannelControl {
            @display("p=50,25;is=s");
        }
        routingRecorder: RoutingTableRecorder {
            @display("p=50,75;is=s");
        }
        configurator: Ipv4NetworkConfigurator {
            @display("p=50,125");
        }
        binder: Binder {
            @display("p=50,175;is=s");
        }
        carrierAggregation: CarrierAggregation {
            @display("p=50.993748,258.7;is=s");
        }
        // Real 3GPP Ethernet PDU Session work: network-wide, tag-independent
        // delay/loss measurement -- subscribe at the system-module level (see
        // each module's own .ned comment), so a single instance observes
        // every app in the network without any wiring. See
        // doc/ethernet-pdu-session/28-packet-observers.md.
        packetDelayObserver: PacketDelayObserver {
            @display("p=50,320;is=s");
        }
        packetLossObserver: PacketLossObserver {
            @display("p=50,370;is=s");
        }
        server: EthernetPduSessionHost {
            // EthernetPduSessionHost extends StandardHost 1:1 (plus a real,
            // dedicated tsnEth interface + an l2App[] slot, both inert unless
            // used) -- see doc/ethernet-pdu-session/16-router-removal-unified-frame.md.
            @display("p=212,118;is=n;i=device/server");
        }
        // Real 3GPP Ethernet PDU Session work: "router" (a plain inet::Router
        // hop between server and upf) has been REMOVED per explicit user
        // instruction. There is also no direct server<->upf link of any
        // kind any more (also per explicit user instruction: a direct link
        // "is not logical" alongside a switch) -- tsnSwitch below is the
        // ONLY connection between server and upf, for both the real Ethernet
        // PDU session traffic and (as a consequence) for anything else. See
        // doc/ethernet-pdu-session/17-switch-only-server-upf.md.
        //
        // Real 3GPP Ethernet PDU Session work: the actual IEEE 802.1Q switch
        // for the Ethernet PDU session's N6 leg (server<->upf) -- the sole
        // path between the two nodes. Real INET learning bridge.
        tsnSwitch: EthernetSwitch {
            parameters:
                numEthInterfaces = 2;
            @display("p=290,200;i=device/switch");
        }
        upf: Upf {
            // hasTsnEthPort = true only on THIS instance (the real, DN-facing
            // UPF that connects to tsnSwitch). This enables the UPF's
            // dedicated tsnEth interface, wired straight into
            // trafficFlowFilter (bypassing nl/encap). See
            // doc/ethernet-pdu-session/15-real-switch-topology.md.
            //
            // Real 3GPP Ethernet PDU Session work: "iUpf" (a pure IP-relay
            // intermediate UPF, previously the only path from upf to gnb) has
            // been REMOVED per explicit user instruction -- it played no role
            // in the Ethernet PDU session logic (no TrafficFlowFilter
            // classification, no hasTsnEthPort), it only forwarded the N3
            // GTP-U tunnel's outer IP packets as a plain router. upf now
            // connects directly to gnb. See
            // doc/ethernet-pdu-session/29-remove-iupf.md.
            hasTsnEthPort = true;
            // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): the N3 leg
            // to the gNB is the IP-less Ethernet transport, not GTP-U/UDP/IP.
            // See doc/ethernet-pdu-session/31-macinmac-n3.md.
            hasEthernetN3 = true;
            @display("p=527.35,116.9125");
        }
        gnb: gNodeB {
            // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): receives N3
            // over the IP-less Ethernet leg. See doc/ethernet-pdu-session/31-macinmac-n3.md.
            hasEthernetN3 = true;
            @display("p=726.35,277.35626;is=vl");
        }
        bgCell[numBgCells]: BackgroundCell {
            @display("p=416,277;is=vl");
        }
        ue[numUe]: NRUe {
            @display("p=628.09375,411.68124");
        }
    connections:
        // Real 3GPP Ethernet PDU Session work: no direct server<->upf link of
        // any kind. tsnSwitch is the ONLY path between server and upf -- see
        // the "tsnSwitch:" submodule comment above and
        // doc/ethernet-pdu-session/17-switch-only-server-upf.md. upf.filterGate
        // is intentionally left unconnected: VoIP-DL/VoIP-UL relied on the
        // removed server-router-upf PPP path and no longer have an IP route
        // to/from the server in this topology -- documented as a consequence
        // of this instruction, not an oversight.
        //
        // Real 3GPP Ethernet PDU Session work (MAC-in-MAC N3): the N3 leg is now
        // a REAL ETHERNET link (no IP) -- upf.n3Eth <--> gnb.n3Eth -- carrying
        // the tenant frame inside an outer 802.1ah-style Ethernet header. This
        // replaces the former GTP-U/UDP/IP-over-PPP link (upf.pppg <--> gnb.ppp),
        // making the ENTIRE server->UE path IP-free. iUpf was already removed
        // (entry 29). See doc/ethernet-pdu-session/31-macinmac-n3.md.
        upf.n3Ethg <--> Eth10G <--> gnb.n3Ethg;

        // Real 3GPP Ethernet PDU Session work: the real, pure-Ethernet N6
        // path (no IP anywhere) -- server.tsnEth -- tsnSwitch -- upf.tsnEth.
        // Parallel to, and independent of, the IP backbone above.
        server.tsnEthg <--> Eth10G <--> tsnSwitch.ethg++;
        tsnSwitch.ethg++ <--> Eth10G <--> upf.tsnEthg;
}
