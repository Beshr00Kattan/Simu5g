//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: AF-side (N6) host with a real,
// pure-Ethernet attachment to the TSN switch -- no IP/UDP tunneling.
// See doc/ethernet-pdu-session/20-reuse-encapsulation-direct-wired.md for the
// full baseline/modified/rationale documentation entry for this file.
//

package simu5g.nodes;

import inet.common.MessageDispatcher;
import inet.linklayer.ethernet.EthernetInterface;
import inet.linklayer.ethernet.basic.EthernetEncapsulation;
import inet.node.inet.StandardHost;
import simu5g.apps.l2.IL2App;

//
// A plain inet::StandardHost extended with:
//  (a) a real, DEDICATED Ethernet interface (tsnEth) plus a real
//      inet::EthernetEncapsulation instance, wired DIRECTLY together and to
//      l2App[0] -- deliberately NOT StandardHost's own numEthInterfaces /
//      eth[] / "nl" chain, and
//  (b) an l2App[] slot.
//
// Why a real EthernetEncapsulation, but direct-wired (see
// doc/ethernet-pdu-session/20-reuse-encapsulation-direct-wired.md):
// hand-building the outer inet::EthernetMacHeader + placeholder
// inet::EthernetFcs directly in TsnEtherApp (an earlier version of this
// design) reproduced the exact same chunk sequence
// EthernetEncapsulation::processPacketFromHigherLayer() itself builds, yet
// still hit "Cannot convert chunk from type ByteCountChunk to type
// EthernetFcs" inside inet::EthernetMac's send path, even after a clean
// rebuild -- pointing at an interaction with the queueing pipeline rather
// than a construction-order bug. Reusing the real, heavily-exercised
// EthernetEncapsulation module removes that whole class of risk. It is
// wired directly to tsnEth and to l2App[0] (module-to-module, no "nl"
// MessageDispatcher in between) specifically to avoid the *other* failure
// mode this project already hit twice: a MessageDispatcher permits only one
// consumer per {protocol, servicePrimitive}, and inet::EthernetEncapsulation
// registering itself on a shared "nl" collided with other registrations
// there ("protocol is already registered"). Direct wiring sidesteps that
// structurally: there is only one possible destination for a packet leaving
// l2App or arriving from tsnEth, so no dispatcher/registration decision is
// ever made.
//
module EthernetPduSessionHost extends StandardHost
{
    parameters:
        int numL2Apps = default(0);
    gates:
        // StandardHost's own "ethg" is sized by numEthInterfaces, which this
        // module leaves at its base default of 0 (no eth[]/"nl"-attached
        // encap submodules are created at all -- see the module comment
        // above). tsnEthg is this module's own, separate gate for the
        // dedicated tsnEth port, mirroring Upf.ned's tsnEthg exactly.
        inout tsnEthg @loose @labels(EtherFrame-conn);
    submodules:
        // Real 3GPP Ethernet PDU Session work: dedicated, direct-wired TSN
        // Ethernet port -- the server-side mirror of Upf.ned's tsnEth.
        tsnEth: EthernetInterface {
            parameters:
                @display("p=375,225");
                interfaceTableModule = "^.interfaceTable";
        }
        // Real 3GPP Ethernet PDU Session work: the real INET module that
        // builds/strips the outer EthernetMacHeader + EthernetFcs -- reused
        // here directly (not via "nl") for its tested correctness. See the
        // module comment above for why. NOTE: it is deliberately NOT named
        // "ethernet" -- StandardHost/LinkLayerNodeBase already declares an
        // inherited submodule with that exact name (conditional on
        // numEthInterfaces>0, which we leave at 0), and re-declaring it with
        // a concrete type here is a NED name collision. "tsnEncap" is a
        // fresh, dedicated name with no such conflict.
        tsnEncap: EthernetEncapsulation {
            parameters:
                @display("p=375,150");
                interfaceTableModule = "^.interfaceTable";
                // Real 3GPP Ethernet PDU Session work (multi-UE): with more than
                // one sender app sharing this single tsnEth interface, the apps
                // fan in through l2Dispatcher (below), which routes each app's
                // outgoing SP_REQUEST frame down to encap by looking up the
                // ethernetMac SERVICE -- so encap must register that service
                // here (registerProtocol=true calls registerService(ethernetMac,
                // ...), see EthernetEncapsulation.cc). With a single app the
                // wiring is direct (no dispatcher to receive the registration),
                // so it stays false, exactly as before -- no behavior change to
                // the single-UE EthernetPduSession config.
                registerProtocol = parent.numL2Apps > 1;
        }
        // Real 3GPP Ethernet PDU Session work (multi-UE): EthernetEncapsulation
        // has a SINGLE upperLayerIn/Out gate pair, so N>1 sender apps cannot
        // wire to it directly. This dispatcher multiplexes them onto the one
        // encap/tsnEth -- the same MessageDispatcher idiom Ue.ned uses for "nl".
        // Present only when there is more than one app; the single-app case
        // keeps its original, proven direct wiring untouched.
        l2Dispatcher: MessageDispatcher if numL2Apps > 1 {
            @display("p=250,110;b=250,5");
        }
        l2App[numL2Apps]: <> like IL2App {
            @display("p=375,50,row,140");
        }
    connections allowunconnected:
        tsnEth.phys <--> tsnEthg;
        tsnEncap.lowerLayerOut --> tsnEth.upperLayerIn;
        tsnEncap.lowerLayerIn <-- tsnEth.upperLayerOut;

        // Single sender app (single-UE): the original, proven DIRECT wiring.
        l2App[0].out --> tsnEncap.upperLayerIn if numL2Apps == 1;
        l2App[0].in <-- tsnEncap.upperLayerOut if numL2Apps == 1;

        // Multiple sender apps (multi-UE): fan the apps into the dispatcher, and
        // the dispatcher into the single encap. The apps must be configured with
        // registerOwnProtocol=false (see omnetpp.ini) so they do not each try to
        // register ethernetMac on the dispatcher (which permits only one
        // provider per protocol); encap registers it instead (above).
        for i=0..numL2Apps-1 {
            l2App[i].out --> l2Dispatcher.in++ if numL2Apps > 1;
            l2App[i].in <-- l2Dispatcher.out++ if numL2Apps > 1;
        }
        l2Dispatcher.out++ --> tsnEncap.upperLayerIn if numL2Apps > 1;
        l2Dispatcher.in++ <-- tsnEncap.upperLayerOut if numL2Apps > 1;
}
