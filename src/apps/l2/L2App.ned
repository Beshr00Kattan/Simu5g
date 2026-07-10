//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer A / L2App foundation module.
// See doc/ethernet-pdu-session/00-foundation-l2app.md for the full
// baseline/modified/rationale documentation entry for this file.
//

package simu5g.apps.l2;

//
// Minimal module interface satisfied by L2App and its subclasses (e.g.
// TsnEtherApp), used by the l2App[] slot added to Ue.ned and to
// EthernetPduSessionHost.ned so those hosts can instantiate any L2App
// variant without depending on the concrete class name.
//
moduleinterface IL2App
{
    gates:
        input in;
        output out;
}

//
// Generic Ethernet-frame-level application. Unlike the transport-layer
// apps under simu5g.apps (cbr, voip, ...), L2App never touches UDP/TCP/IP:
// it builds and consumes real inet::EthernetMacHeader-framed packets and
// is wired directly to a network-layer dispatcher ("nl" in Ue.ned /
// EthernetPduSessionHost.ned), exactly where inet::Ipv4NetworkLayer is
// wired -- not to the app[] vector that feeds the transport-layer
// dispatcher, since there is no transport layer under an Ethernet PDU
// session's traffic. L2App registers itself for Protocol::ethernetMac
// with that dispatcher (see L2App.cc), the same registerProtocol()
// mechanism inet::Ipv4NetworkLayer/Udp already use for their own
// protocols -- no new dispatch mechanism is introduced.
//
simple L2App like IL2App
{
    parameters:
        string interfaceTableModule = default("^.interfaceTable"); // path to this host's InterfaceTable
        string interfaceName = default("cellular"); // registered NetworkInterface name of the cellular NIC (set by IP2Nic, default "cellular"); outgoing frames are tagged for it via InterfaceReq. NOTE: this is the interface NAME, not the "cellularNic" submodule name.
        // Real 3GPP Ethernet PDU Session work: whether THIS instance should
        // register Protocol::ethernetMac on "nl" itself. Must be true on the
        // UE (cellularNic has no inet::EthernetEncapsulation to do it) and
        // false on the server (EthernetPduSessionHost's real "ethernet:"
        // submodule already registers ethernetMac on "nl" transitively via
        // its own registerService() call -- a second registration from here
        // collides: "protocol is already registered"). See
        // doc/ethernet-pdu-session/15-real-switch-topology.md.
        bool registerOwnProtocol = default(true);
        string srcMac;                     // source MAC address, e.g. "00:00:00:00:00:01"
        string destMac;                    // destination MAC address, e.g. "00:00:00:00:00:02"
        double sendInterval @unit("s") = default(0.01s);
        int packetLength @unit("B") = default(300B); // length of the frame payload (excluding headers)
        int etherType = default(0x88B5);   // IEEE 802 "experimental" EtherType, used when no VLAN tag is pushed
        double startTime @unit("s") = default(0s);
        double stopTime @unit("s") = default(0s); // 0s = run for the whole simulation

        @signal[framesSent];
        @signal[framesReceived];
        @signal[framesDropped];
        @signal[frameDelay];
        @statistic[framesSent](title="Ethernet frames sent"; source="framesSent"; record=count,vector);
        @statistic[framesReceived](title="Ethernet frames received"; source="framesReceived"; record=count,vector);
        @statistic[framesDropped](title="Ethernet frames dropped"; source="framesDropped"; record=count,vector);
        @statistic[frameDelay](title="End-to-end frame delay"; unit="s"; source="frameDelay"; record=mean,vector);

        @display("i=block/app");
    gates:
        input in;
        output out;
}
