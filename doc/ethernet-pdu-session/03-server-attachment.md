# Documentation entry: Ue.ned l2App slot (UE side) + TsnEtherSourceApp (server side)

This entry supersedes an earlier draft of itself: the first design attempted to give
`server` (StandardHost) the same direct-to-`nl` attachment used for `ue[]`, via a new
`EthernetPduSessionHost` wrapper NED type. That was reverted after checking
`SingleCell_Standalone.ned`'s actual topology: `server` only reaches `upf` via
`server--router--upf.filterGate`, and `router` is a plain `inet::Router` that forwards
strictly based on its Ipv4 routing table -- it has no registered handler for a
non-IP-tagged packet and would drop or crash on one. Rather than modify `router`
(another shared, generic INET module) or add a network node, the AF-to-UPF (N6) leg
carries the Ethernet frame as a UDP payload instead -- see `TsnEtherSourceApp.ned`'s
own header comment for the full rationale (in short: 3GPP does not standardize N6's
transport encapsulation, and carrying an L2 payload inside UDP/IP for one hop is the
same idea GTP-U itself uses one hop later). `EthernetPduSessionHost.ned` was deleted;
it is not part of the final design.

## File: `src/nodes/Ue.ned`
(unchanged from the original entry) Adds `numL2Apps`/`pduSessionType` parameters and
an `l2App[numL2Apps]: <> like IL2App` slot wired directly to `nl`, exactly parallel to
`ipv4`/`ipv6`. This is necessary and correct for the UE side specifically because,
once a frame reaches the UE via PDCP/RLC/MAC/PHY, there genuinely is no IP layer
involved at all (per Layer A's PDCP changes) -- there is no socket to attach a
transport-layer app to. See entry `01-l2app-attachment.md` for the full
statement-by-statement explanation of this file's baseline/modified code (that part
of the design did not change).

## File: `src/apps/l2/TsnEtherSourceApp.{ned,h,cc}`
**Baseline:** No baseline -- new file, modeled directly on
`simu5g.apps.cbr.CbrSender` (confirmed by reading `CbrSender.cc`/`.h`/`.ned`): same
`like IApp`, `socketOut`/`socketIn` gates, `inet::UdpSocket`, `L3AddressResolver`
destination lookup, and self-message send-timer idiom.

**New code / statement-by-statement (the parts that differ from CbrSender):**
- `sendFrame()` builds the same real `EthernetMacAddressFields` +
  `Ieee8021qTagEpdHeader` + `ByteCountChunk` stack `TsnEtherApp::buildFrame()` builds
  (see its own doc entry for the field-level rationale) -- this `Packet` is the
  genuine Ethernet frame the Ethernet PDU session is meant to carry.
- `socket_.sendTo(pkt, destAddress_, destPort_)`: hands that Ethernet-framed packet to
  an ordinary `UdpSocket`, which wraps it in real UDP+IP headers for transport across
  the `server--router--upf` segment -- i.e. the wire layout for this one hop is
  `[IP][UDP][EthernetMacAddressFields][Ieee8021qTagEpdHeader][payload]`. This is not a
  metadata substitute for the Ethernet frame (the frame is fully byte-present as the
  UDP payload); it is an outer transport shell around it, analogous to how GTP-U
  itself is a UDP/IP-encapsulated tunnel one hop later.
- No `PacketProtocolTag`/`DispatchProtocolReq`/`InterfaceReq` handling is needed here
  at all (unlike `L2App`): since this module is a normal socket app in the `app[]`
  vector, the existing UDP/IP dispatch path (`at`/`tn`/`nl` → `ipv4` → `router`)
  already knows how to route it -- no new dispatcher registration required for the
  server side.

**Why (architectural justification):** `TrafficFlowFilter` (see its own doc entry)
unwraps the outer IP/UDP headers upon arrival at the UPF and continues with the
genuine inner Ethernet frame from that point on. Everything from the UPF onward --
GTP-U, gNB, PDCP/RLC/MAC/PHY, UE -- carries the real frame bytes with no IP/UDP
involved, which is the segment Layer A's "no metadata substitute, PDCP/RLC/MAC/PHY
must carry the real Ethernet frame bytes end-to-end" requirement is actually about.

**Replaces:** the reference shim's `TSNApp` embedded in `ServerMacTsn` (Section 9.3/
9.7 of the reference report), which sent through a brand-new `tsnOut` gate wired to a
brand-new `TsnMacForwarder` relay chain, bypassing the existing IP/UDP infrastructure
entirely rather than reusing it for this one transport hop.
