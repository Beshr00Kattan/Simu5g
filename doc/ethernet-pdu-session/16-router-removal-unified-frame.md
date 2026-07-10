# 16 — Topology consolidation: router removed, unified single-header frame, server gets a dedicated direct-wired interface

## Why this entry exists

Entry 15 replaced the UDP/IP tunnel workaround with a real `tsnSwitch` and gave
the UPF a dedicated, direct-wired `EthernetInterface` (`tsnEth`) to avoid a
`MessageDispatcher` protocol-registration collision on `upf.nl`. That fixed
the UPF side, but the **server** side was still attached the "standard" way
(`numEthInterfaces=1` on `EthernetPduSessionHost`, relying on
`inet::EthernetEncapsulation` + the `nl` dispatcher to build the real outer
`EthernetMacHeader`). Live-testing then hit:

```
Cannot convert chunk from type inet::ByteCountChunk to type inet::Ieee8021qTagEpdHeader.
... module (TsnPcpClassifier) SingleCell_Standalone.server.eth[0].queue.classifier
```

together with an explicit instruction from the user: *"remove the router
completely, and have the switch in that path from server to UPF instead."*

## Root cause of the classifier error

`TsnPcpClassifier::classifyPacket()` assumed the frame reaching
`server.eth[0].queue` was `[real EthernetMacHeader (built by
EthernetEncapsulation)][our EthernetMacAddressFields][our
Ieee8021qTagEpdHeader][payload]` — i.e. it assumed encap had already wrapped
`TsnEtherApp`'s own two-chunk frame in a second, real header.

Tracing `inet::registerService()`/`registerProtocol()` and
`MessageDispatcher::handleRegisterProtocol()` (`IProtocolRegistrationListener.cc`,
`MessageDispatcher.cc`) shows that only `registerProtocol()` (not
`registerService()`) forwards a registration transitively across the whole
connected dispatcher chain via `forwardProtocolRegistration`. Encap's own
*upper-side* registration is a `registerService()` call, which does not
propagate the same way. Whether `DispatchProtocolReq(ethernetMac, SP_REQUEST)`
correctly reaches `EthernetEncapsulation` therefore depends on exactly which
dispatcher registered what — a genuinely fragile, hard-to-predict outcome in
this specific host configuration. In practice, the packet reached `eth[0]`
**without** ever passing through `EthernetEncapsulation`: no real outer
header was ever inserted, so `TsnEtherApp`'s own two-chunk frame
(`EthernetMacAddressFields` + `Ieee8021qTagEpdHeader` + payload, 16 bytes of
real header) sat directly at the front. The classifier's offset arithmetic
(peek 14B as if it were `EthernetMacHeader`, then peek another 12B as
`EthernetMacAddressFields`, then peek 4B as `Ieee8021qTagEpdHeader`) walked 26
bytes into a 16-byte header, landing inside the `ByteCountChunk` payload —
exactly the reported error.

Reading `EthernetMac::processReceivedDataFrame`/`handleUpperPacket`
(`EthernetMac.cc`) confirms a real (basic, non-CSMA) `inet::EthernetMac`
unconditionally does `packet->peekAtFront<EthernetMacHeader>()` and throws on
any other chunk type — so *any* module handing a packet straight to a raw
`EthernetInterface` (bypassing `EthernetEncapsulation`) must itself build one
genuine, full `EthernetMacHeader` at the front. This is the same
"only-one-consumer-per-{protocol,servicePrimitive}" dispatcher fragility that
required the UPF's dedicated-interface redesign in entry 15 — it now gets
applied symmetrically to the server, and the frame format is simplified to
not depend on encap being present at all.

## Decision: consolidate, don't keep patching

Rather than chase a third dispatcher-registration subtlety, the design is
consolidated around the pattern that already worked cleanly for the UPF:

1. **The server gets its own dedicated, direct-wired `tsnEth` interface**
   (mirroring `Upf.ned`'s `tsnEth` exactly), bypassing
   `EthernetEncapsulation`/`nl` entirely. There is now only one possible
   destination for a packet leaving `l2App`, so no dispatcher/registration
   decision is ever made on that path.
2. **The wire frame becomes a single, unified structure** that does not
   depend on `EthernetEncapsulation` being present anywhere:
   `[real EthernetMacHeader(dest,src,typeOrLength=0x8100)][real
   Ieee8021qTagEpdHeader(pcp,dei,vid,realEtherType)][payload]` — this is the
   genuine, byte-accurate 18-byte 802.1Q tagged-frame header layout (TPID in
   the outer header, PCP/VID/real-EtherType in the "Epd" chunk that follows
   it without repeating the TPID), built once by `TsnEtherApp::buildFrame()`
   and never rewrapped at any hop.
3. **`router` is removed from the topology.** It only ever carried the one
   PPP hop between `server` and `upf`; `server.pppg` now connects directly to
   `upf.filterGate`, so ordinary IP traffic (`VoIP-DL`/`VoIP-UL`) keeps
   working unchanged (one fewer hop, not a functional change for those
   configs), while the Ethernet PDU session traffic uses `tsnSwitch`
   exclusively and never touches `router`/`pppg` at all.

## File-by-file changes

### `src/apps/l2/TsnEtherApp.cc`
- `buildFrame()`: replaced the `EthernetMacAddressFields` chunk with a full
  `EthernetMacHeader` (`dest`, `src`, `typeOrLength=0x8100`, the standard
  802.1Q TPID). Removed the `DispatchProtocolReq(ethernetMac, SP_REQUEST)`
  tagging added in entry 15 (no longer meaningful — there is no dispatcher on
  this path to route with it).
- `handleFrame()`: pops `EthernetMacHeader` (was `EthernetMacAddressFields`)
  first, then `Ieee8021qTagEpdHeader` — mirrors the new build order exactly.

### `src/nodes/EthernetPduSessionHost.ned`
- Removed `numEthInterfaces = default(1)` (no more `eth[]`/`ethernet`
  submodules at all).
- Added a dedicated `tsnEth: EthernetInterface` submodule + `tsnEthg` gate,
  and direct connections `l2App[i].out --> tsnEth.upperLayerIn` /
  `l2App[i].in <-- tsnEth.upperLayerOut` — the server-side mirror of
  `Upf.ned`'s `tsnEth`.

### `src/apps/l2/TsnPcpClassifier.cc`
- `classifyPacket()`: removed the middle `EthernetMacAddressFields` peek —
  the frame is now exactly `[EthernetMacHeader][Ieee8021qTagEpdHeader][payload]`,
  so only two peeks are needed.

### `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc`
- `handleEthernetPacket()`: no longer pops the `EthernetMacHeader` — it is
  peeked only (for the destination MAC used in classification) and left
  intact, since it is now the *same* header the UE's `TsnEtherApp::handleFrame()`
  expects to pop at the far end of the GTP-U tunnel (no more "outer vs.
  inner header" distinction, because there is no more encapsulating module
  adding a second header). Only the trailing `EthernetFcs` is still popped
  and discarded — it is a physical-layer artifact of the one
  `tsnEth<->tsnSwitch<->tsnEth` wire hop, not part of the logical frame being
  tunneled to the UE.

### `src/apps/l2/L2App.cc`
- Removed stale comments describing the now-abandoned
  `EthernetEncapsulation`-dependent design; `registerOwnProtocol` is kept
  (still needed for the UE's `nl`-attached path) with an updated comment
  explaining it is a harmless no-op on any host wired directly to a leaf
  module (confirmed via `IProtocolRegistrationListener.cc`:
  `registerProtocol()`'s `findConnectedGate()` walk returns `nullptr` when no
  `MessageDispatcher`-like module is reachable, so the call simply does
  nothing).
- Removed the `ProtocolGroup`-based ether-type registration block from
  `initialize()` (INITSTAGE_APPLICATION_LAYER) — it existed solely to satisfy
  `EthernetEncapsulation::getProtocolNumber()`, which is no longer on this
  path for either the server or the UE.

### `simulations/NR/networks/SingleCell_Standalone.ned`
- Removed the `router: Router` submodule and its two `pppg` connections.
- `server.pppg++ <--> Eth10G <--> upf.filterGate` directly (was
  `server -- router -- upf`).
- `server.tsnEthg <--> Eth10G <--> tsnSwitch.ethg++` (was
  `server.ethg++`, since `EthernetPduSessionHost` no longer has a
  `numEthInterfaces`-sized `ethg`).
- Removed the now-unused `inet.node.inet.Router` and `inet.node.inet.StandardHost`
  imports.

### `simulations/NR/standalone/omnetpp.ini`, `[Config EthernetPduSession]`
- `*.server.l2App[0].interfaceName = "tsnEth"` (was `"eth0"` — the registered
  `NetworkInterface` name is now the dedicated submodule's own name).
- Removed `*.server.l2App[0].registerOwnProtocol = false` (no longer
  necessary — there is nothing to collide with on this path; left at its
  default `true`, which is a harmless no-op here).
- `*.server.tsnEth.queue.*` (was `*.server.eth[0].queue.*`) for the
  `TsnPcpClassifier`/`PriorityQueue` wiring.

## What this replaces / supersedes

This entry supersedes entry 15's server-side wiring (the `numEthInterfaces`
+ `EthernetEncapsulation` + `nl` + `DispatchProtocolReq`/`MacAddressReq`
routing-through-encap design) while keeping entry 15's UPF-side
dedicated-interface design unchanged (it already matches this pattern). The
GTP-U tunneling, `TrafficFlowFilter` MAC-based classification/learning, and
`Binder`-based UE MAC/PDU-session-type registration from Layer A are all
unaffected by this change — only the wire frame's exact chunk layout and the
physical attachment mechanism on the server side changed.
