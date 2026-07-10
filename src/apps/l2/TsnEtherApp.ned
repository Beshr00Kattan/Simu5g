//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / real 802.1Q VLAN tagging.
// See doc/ethernet-pdu-session/00-foundation-l2app.md for the full
// baseline/modified/rationale documentation entry for this file.
//

package simu5g.apps.l2;

//
// L2App specialization that additionally pushes a real, serialized IEEE
// 802.1Q VLAN tag (inet::Ieee8021qTagTpidHeader) in front of the Ethernet
// header, carrying a genuine VLAN ID and PCP (priority code point) value.
// This is the traffic source used by the Ethernet PDU Session scenario
// ([Config EthernetPduSession] in simulations/NR/standalone/omnetpp.ini).
//
simple TsnEtherApp extends L2App
{
    parameters:
        // Real 3GPP Ethernet PDU Session work: REQUIRED. Without an explicit
        // @class, a "simple X extends Y" module INHERITS Y's @class -- so the
        // NED type TsnEtherApp would be backed by the C++ class L2App (the
        // base), and TsnEtherApp::buildFrame()/initialize() would NEVER run.
        // That was the real, long-hidden root cause of the persistent
        // "Unknown protocol: ethernetmac" error: l2App[0] was silently a plain
        // L2App object building a base "EthFrame" (tagged ethernetMac), while
        // every fix lived in the never-instantiated TsnEtherApp class. The
        // OMNeT++ event banner showing "(TsnEtherApp)" is the NED type name,
        // which masked the wrong C++ class. See
        // doc/ethernet-pdu-session/24-missing-class-property-rootcause.md.
        @class(TsnEtherApp);
        int vid = default(1);   // 12-bit VLAN identifier
        int pcp = default(0);   // 3-bit priority code point (0..7)
        @display("i=block/app2");

        // Real 3GPP Ethernet PDU Session work: network-wide packet
        // delay/loss observer support -- see TsnEtherApp.h and
        // doc/ethernet-pdu-session/28-packet-observers.md. OMNeT++ requires
        // every signal a module emits to be declared in its NED type (or an
        // ancestor's); these are emitted directly by TsnEtherApp.cc, not by
        // L2App, so they are declared here rather than in L2App.ned.
        @signal[packetDelaySentSignal](type=long);
        @signal[appPacketSent](type=long);
        @signal[packetDelayReceivedSignal](type=long);
        @signal[appPacketReceived](type=long);
}
