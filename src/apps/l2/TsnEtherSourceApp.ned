//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: AF-side (N6) source application.
// See doc/ethernet-pdu-session/03-server-attachment.md for the full
// baseline/modified/rationale documentation entry for this file.
//

package simu5g.apps.l2;

import inet.applications.contract.IApp;

//
// AF-side source for an Ethernet PDU session: builds the same real,
// byte-serialized 802.1Q-tagged Ethernet frame TsnEtherApp builds (a real
// inet::EthernetMacHeader/Ieee8021qTagEpdHeader, no metadata tag standing in
// for either), but sends it as the *payload* of an ordinary UDP/IP packet
// over a ~inet::UdpSocket (the same "IApp over UdpSocket" pattern
// simu5g.apps.cbr.CbrSender already uses), instead of attaching directly to
// a network-layer dispatcher the way TsnEtherApp does on the UE side.
//
// Why the difference: SingleCell_Standalone.ned's "server" reaches the UPF
// only via server--router--upf.filterGate, and "router" is a plain
// inet::Router that forwards based on its Ipv4 routing table -- it has no
// registered handler for a non-IP protocol and would drop (or crash on) a
// bare Ethernet-tagged packet. Carrying the Ethernet frame as a UDP payload
// across this existing IP-routed segment is a real, standard technique (the
// same idea GTP-U itself uses one hop later: an L2/tunnel payload inside
// UDP/IP), and 3GPP does not standardize N6's own transport encapsulation,
// so this is a legitimate, minimal way to reach the UPF over the fixed
// existing topology without modifying "router" or adding a network node.
// The UPF's TrafficFlowFilter unwraps this outer UDP/IP shell and works with
// the genuine inner Ethernet frame from that point on -- everything from the
// UPF onward (GTP-U, gNB, PDCP/RLC/MAC/PHY, UE) carries the real frame with
// no IP/UDP involved, which is the part Layer A's compliance requirement
// actually targets.
//
simple TsnEtherSourceApp like IApp
{
    parameters:
        int localPort = default(-1);
        int destPort = default(3000);
        string destAddress; // the UPF's DN-facing address, e.g. "upf"

        string srcMac;   // e.g. "00:00:00:00:00:01"
        string destMac;  // e.g. "00:00:00:00:00:02"
        int vid = default(1);
        int pcp = default(0);
        int etherType = default(0x88B5);
        int packetLength @unit("B") = default(300B);
        double sendInterval @unit("s") = default(0.01s);
        double startTime @unit("s") = default(0s);
        double stopTime @unit("s") = default(0s); // 0s = run for the whole simulation

        @signal[framesSent];
        @statistic[framesSent](title="Ethernet frames sent"; source="framesSent"; record=count,vector);

        @display("i=block/source");
    gates:
        output socketOut;
        input socketIn;
}
