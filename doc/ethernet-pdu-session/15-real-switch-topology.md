# Documentation entry: Real switch topology (replaces the UDP/IP tunnel workaround)

## Why this supersedes entry 03

Entry 03 documented a deliberate scope compromise: since `SingleCell_Standalone.ned`'s
`router` is a plain `inet::Router` that can only forward IP traffic, the server-to-UPF
leg carried the Ethernet frame as a UDP/IP payload instead of natively. This was
flagged at the time as a compromise, not the ideal design. On review (prompted by the
user watching the Qtenv animation and correctly noting the server-to-UPF hop is not
real Ethernet), this entry replaces that compromise with a **real IEEE 802.1Q switch**
and **zero IP** on that leg, matching the original requirement precisely.

## New topology

```
Before (entry 03):  server --IP/UDP tunnel--> router --IP--> upf   (workaround)
Now (this entry):   server --real Ethernet--> tsnSwitch --real Ethernet--> upf
```

The existing `server--router--upf` IP path is **completely untouched** and still
serves `VoIP-DL`/`VoIP-UL` — this is a new, parallel, real-Ethernet-only path.

## File: `simulations/NR/networks/SingleCell_Standalone.ned`
**Modified:** `server: StandardHost` → `server: EthernetPduSessionHost` (new node
type, below). Added `tsnSwitch: EthernetSwitch` (real `inet.node.ethernet.EthernetSwitch`,
`numEthInterfaces = 2`). Added two new connections:
`server.ethg++ <--> Eth10G <--> tsnSwitch.ethg++;` and
`tsnSwitch.ethg++ <--> Eth10G <--> upf.ethg++;`. All four pre-existing connections
(`server.pppg++ <--> router...` etc.) are untouched.

## File: `src/nodes/EthernetPduSessionHost.ned` (new)
`module EthernetPduSessionHost extends StandardHost`: sets `numEthInterfaces =
default(1)` (StandardHost/`LinkLayerNodeBase` already support this generically — real
Ethernet interfaces are not new NED plumbing) and adds an `l2App[]` submodule vector
wired directly to `nl`, identical in structure to the slot already added to `Ue.ned`
(entry 01). This is what lets `TsnEtherApp` attach to the server the same way it
attaches to the UE.

## File: `src/nodes/Upf.ned`
New connection `nl.out++ --> trafficFlowFilter.ethernetFrameIn;` — real Ethernet frames
arriving via `eth[0]`/`tsnSwitch` travel `eth[0] → li → ethernet(encap, decapsulates
the real header) → bl → cb → nl → trafficFlowFilter.ethernetFrameIn`, entirely
separate from the existing `pppIf.upperLayerOut → trafficFlowFilter.internetFilterGateIn`
IP path (a plain, non-array gate cannot carry two connections, hence the new,
dedicated gate rather than reusing the old one).

**Bug found and fixed (user-reported):** `numEthInterfaces = default(1)` was
initially added at the `Upf` module-*type* level. `Upf` is instantiated **twice** in
`SingleCell_Standalone.ned` -- as `upf` (the real, DN-facing UPF, which needs
`eth[0]`) and as `iUpf` (a pure intermediate relay). Setting the default at the type
level gave `iUpf` a real `eth[0]`/`ethg[0]` too, but nothing in the topology connects
it, which OMNeT++'s network-completeness check correctly rejected at startup:
`Gate 'SingleCell_Standalone.iUpf.ethg$i[0]' is not connected to sibling or parent
module`. **Fix:** removed the type-level default from `Upf.ned` entirely (reverted to
`LinkLayerNodeBase`'s own default of 0) and instead set `numEthInterfaces = 1` only on
the `upf:` submodule instance in `SingleCell_Standalone.ned` -- `iUpf` is unaffected
and keeps 0 real Ethernet interfaces, exactly as before.

## Major redesign (user-reported, #3): dedicated direct-wired UPF interface

The first switch-topology design routed the UPF's incoming frames through the
standard `eth[]` → `li` → `EthernetEncapsulation` → `bl` → `cb` → `nl` chain, with
`TrafficFlowFilter` registering `Protocol::ethernetMac` on `nl` to consume them. Two
successive user-reported startup errors proved this fights INET's shared-dispatcher
model:

1. **Server side** (`handleRegisterProtocol(): protocol is already registered ...
   server.nl`): the server's own `EthernetEncapsulation` already registers
   `ethernetMac` on `server.nl` (its registration propagates transitively up the
   dispatcher chain). The `l2App` registering it again collided. Fixed by adding a
   `registerOwnProtocol` parameter to `L2App` (default `true`; set `false` on the
   server, which defers to encap's registration).
2. **UPF side** (`handleRegisterProtocol(): protocol is already registered ...
   upf.nl`): the same collision, but now unavoidable — the UPF genuinely needs *both*
   `EthernetEncapsulation` (from its `eth[]` interface) *and* `TrafficFlowFilter` to
   handle `ethernetMac` at `nl`, and a `MessageDispatcher` permits only **one**
   consumer per `{protocol, servicePrimitive}` (verified by reading
   `MessageDispatcher::handleRegisterProtocol()` — it throws on a second registration
   at a different gate index).

**Resolution — bypass `nl`/encap entirely for the UPF ingress.** The UPF now has a
**dedicated `EthernetInterface` submodule** (`tsnEth`, present only when
`hasTsnEthPort=true`), wired **directly** to `TrafficFlowFilter`:
```ned
tsnEth: EthernetInterface if hasTsnEthPort { mac.promiscuous = true; }
...
tsnEth.upperLayerOut --> trafficFlowFilter.ethernetFrameIn if hasTsnEthPort;
trafficFlowFilter.ethernetFrameOut --> tsnEth.upperLayerIn if hasTsnEthPort;
tsnEth.phys <--> tsnEthg if hasTsnEthPort;
```
No `nl`, no `EthernetEncapsulation`, no protocol registration on the UPF ingress → no
collision possible. `TrafficFlowFilter::initialize()` no longer calls
`registerProtocol()` at all. `mac.promiscuous = true` because the UPF is a
bridge/gateway port: it must accept frames whose destination MAC is a UE's `tsnMac`
(behind the radio stack), not the port's own address — confirmed against
`EthernetMacBase::dropFrameNotForUs()`, which only accepts non-matching unicast in
promiscuous mode.

**Frame layout consequence.** The server's real `EthernetEncapsulation` (still used on
the server egress) wraps our inner frame in a real outer `EthernetMacHeader` + `Fcs`.
So what arrives at the UPF is:
`[outer EthernetMacHeader][our EthernetMacAddressFields + Ieee8021qTagEpdHeader +
payload][EthernetFcs]`. The basic `EthernetMac` on `tsnEth` passes this up intact
(it does not decapsulate — that's encap's job, deliberately skipped here), so
`TrafficFlowFilter::handleEthernetPacket()` now pops the outer header and the trailing
FCS before reading our inner `EthernetMacAddressFields`. The outer header is the
"physical" L2 frame the switch forwarded by; our inner frame is its tenant payload —
a clean, realistic two-level model.

**Server-egress consequence.** For the server's frame to pass *through*
`EthernetEncapsulation` (so it gets the outer header) rather than being routed straight
to the interface, `L2App`/`TsnEtherApp::buildFrame()` now tag it with
`DispatchProtocolReq(ethernetMac, SP_REQUEST)`. The explicit `SP_REQUEST` is essential:
`MessageDispatcher`'s service-primitive KLUDGE (read from its source) would otherwise
see `PacketProtocolTag == DispatchProtocolReq` protocol and mis-resolve a bare tag to
`SP_INDICATION` (an upward path), sending the frame the wrong direction.

## Bug found and fixed (user-reported, #2): unpaired dispatcher gate

**Symptom:** `Gate index 3 out of range when accessing vector gate 'in[]' with
size 3 -- in module (inet::MessageDispatcher) SingleCell_Standalone.upf.nl`.

**Cause:** `inet::MessageDispatcher` keeps its `in[]`/`out[]` gate vectors as
matched pairs -- every existing consumer (`ipv4`, `cb`, `tn`) connects both
directions together, so the two vectors grow in lockstep and the same index
always refers to the same peer. The original `ethernetFrameIn` wiring
(`nl.out++ --> trafficFlowFilter.ethernetFrameIn;`) was one-directional: it grew
`nl.out[]` to size 4 while `nl.in[]` stayed at size 3. `registerProtocol()`'s
internal bookkeeping (called from `TrafficFlowFilter::initialize()`) assumes the
pairing holds and tried to reach the now-missing `nl.in[3]`.

**Fix:** `TrafficFlowFilter.ned` gained a second gate, `output ethernetFrameOut
@loose;` -- functionally unused (`TrafficFlowFilter` never sends anything back
up through `nl`) but present specifically so its connection keeps both vectors
the same size. `Upf.ned` connects both directions together:
```ned
nl.out++ --> trafficFlowFilter.ethernetFrameIn;
nl.in++ <-- trafficFlowFilter.ethernetFrameOut;
```
**Why the pre-existing `internetFilterGateIn` never had this problem:** it is
fed by a *direct* connection from `pppIf` (`pppIf.upperLayerOut -->
trafficFlowFilter.internetFilterGateIn;`), not through any `MessageDispatcher` --
a leaf-module connection has no pairing requirement. The pairing constraint only
applies to gates connected to a dispatcher, which `ethernetFrameIn` newly is.

## Files: `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.{ned,h,cc}`
**Removed:** the `ETHERNET_PDU_SESSION_N6_PORT` UDP-port-sniffing logic entirely — no
longer needed, since real frames now arrive on their own gate.
**Added:** `input ethernetFrameIn @loose;` gate. In `initialize()`, when this instance
has a `macForwardingTableModule` (i.e., it's the UPF's own instance), calls
`registerProtocol(Protocol::ethernetMac, gate("ethernetFrameIn"), SP_INDICATION)` —
the identical single-gate + `ServicePrimitive` overload the pre-existing code already
uses two lines above for `LteProtocol::ipv4uu`, so `nl` knows to deliver ethernetMac
indications here.
**`handleMessage()`:** now dispatches purely on **arrival gate**
(`msg->getArrivalGate() == gate("ethernetFrameIn")`) rather than sniffing packet
content — cleaner and removes the "magic port number" that entry 07 documented as a
necessary evil of the tunnel workaround.
**`handleEthernetPacket()`:** no longer pops `Ipv4Header`/`UdpHeader` (there is none)
— peeks `EthernetMacAddressFields` directly at the front. The MAC-resolution/serving-BS
logic (the Bug 2 fix from entry 14) is unchanged.

## Files: `src/apps/l2/TsnEtherSourceApp.*` — retired
No longer used by `omnetpp.ini`. `TsnEtherApp` (already built to work as either
source or sink depending on whether `destMac` is configured) is now reused directly
on **both** ends — the source files remain in the tree but are dead code; removing
them is a trivial follow-up, deferred to avoid an unnecessary extra Makefile
regeneration cycle while iterating live with the user.

## Files: `src/apps/l2/L2App.cc`, `TsnEtherApp.cc` — real `EthernetEncapsulation` compatibility
Making `server.l2App[0]` (a `TsnEtherApp` instance) send through a **real**
`inet::EthernetEncapsulation` (via `EthernetPduSessionHost`'s inherited `ethernet:`
submodule, active by default since `LinkLayerNodeBase` sets
`ethernet.registerProtocol = default(true)`) surfaced two real requirements,
confirmed by reading `EthernetEncapsulation::processPacketFromHigherLayer()`:

1. **`MacAddressReq` is mandatory.** `processPacketFromHigherLayer()` calls
   `packet->getTag<MacAddressReq>()` unconditionally (throws if absent) to learn the
   real frame's destination (and optionally source) MAC. Both `L2App::buildFrame()`
   and `TsnEtherApp::buildFrame()` now set `pkt->addTagIfAbsent<MacAddressReq>()`
   with `destMac_`/`srcMac_` — harmless and unread on the UE path (`cellularNic` has
   no `EthernetEncapsulation` at all), required and load-bearing on the server path.
2. **The payload's `PacketProtocolTag` must resolve to a real EtherType.**
   `processPacketFromHigherLayer()` computes the real frame's `typeOrLength` field via
   `ProtocolGroup::getEthertypeProtocolGroup()->getProtocolNumber(protocol)`, which
   **throws** (`cRuntimeError`, confirmed by reading `ProtocolGroup::getProtocolNumber()`
   — no fallback) if the packet's `PacketProtocolTag` protocol isn't registered in that
   group. `Protocol::ethernetMac` (used throughout this project as the dispatch/content
   marker) is a meta-protocol, not a registered payload EtherType, and is absent from
   INET's static `ethertypeProtocols` table (confirmed by reading `ProtocolGroup.cc`
   — only real protocols like `ipv4`/`arp`/... are listed).
   **Fix:** `L2App::initialize()` registers it once, dynamically, via
   `ProtocolGroup::getEthertypeProtocolGroup()->addProtocol(etherType_, &Protocol::ethernetMac)`
   (guarded by `findProtocolNumber() == -1` so it only registers once even though
   both the server's and the UE's `l2App` instances run this code) — using the
   project's own configured `etherType` value (default `0x88B5`, IEEE 802 "Local
   Experimental Ethertype 1") as the real, on-the-wire EtherType. This is a dynamic,
   additive registration via a real, public INET API (`ProtocolGroup::addProtocol`),
   not a modification to any INET file.

With both fixes, the real frame `EthernetEncapsulation` builds on send is:
`[real EthernetMacHeader(dest,src,typeOrLength=0x88B5)][our EthernetMacAddressFields(dest,src)][our Ieee8021qTagEpdHeader][payload][EthernetFcs]`
— i.e. our own logical Ethernet+VLAN header (carrying the UE's `tsnMac`, the actual
TSN-layer destination) travels as the **payload** of a real, physically-addressed
Ethernet II frame, exactly mirroring how a real device's NIC driver would frame
whatever an application hands it. `inet::EthernetEncapsulation::processPacketFromMac()`
strips exactly this outer header + FCS on the receive side, leaving our own content
intact for `TrafficFlowFilter`/`TsnEtherApp` to read, unchanged from before.

## Why flooding still delivers the frame correctly

`tsnSwitch` is a genuine `inet::EthernetSwitch` with only two ports in this topology
(server, upf). Its `MacForwardingTable` has never observed a frame whose source MAC
is `ue[0]`'s `tsnMac` (that UE is unreachable via any physical switch port — it sits
behind the entire NR radio stack), so a lookup for that destination MAC always misses,
and `Ieee8021dRelay`'s real, unmodified flood-on-unknown-unicast behavior sends the
frame out every other port — which, with exactly one other port (`upf`), delivers it
correctly. No special-casing was needed for the "logical" (`tsnMac`) vs. "physical"
(a real device's own MAC) address distinction that a larger, multi-port topology would
require.

## File: `src/apps/l2/TsnPcpClassifier.cc`
Updated to peek through the **real** `EthernetMacHeader` (now present, added by
`EthernetEncapsulation`) before reaching our own `EthernetMacAddressFields`/VLAN
header, instead of the retired IPv4/UDP shell. Wired in `omnetpp.ini` on
`server.eth[0].queue` (was `server.ppp[0].queue`).
