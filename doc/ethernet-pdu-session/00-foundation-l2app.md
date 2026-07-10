# Documentation entry: L2App foundation (Layer A/B prerequisite)

Per the mandatory documentation requirement, this entry is written at the time these
files were created, before any Layer A/B UPF/PDCP work begins (L2App is the packet
source/sink both layers depend on).

## File: `src/apps/l2/L2App.ned`
**Baseline:** No baseline — new file. No prior L2/Ethernet-only application type existed
in Simu5G; all existing apps under `src/apps/*` (cbr, voip, alert, burst, vod) are
UDP/TCP-socket apps that go through the transport-layer dispatcher.

**New code:** `simple L2App` module, gates `in`/`out`, parameters `srcMac`, `destMac`,
`sendInterval`, `packetLength`, `etherType`, `startTime`, `stopTime`, plus four signals
(`framesSent`, `framesReceived`, `framesDropped`, `frameDelay`).

**Explanation / why:**
- `in`/`out` gates (not `socketIn`/`socketOut` as `IApp` implementations normally have):
  a real Ethernet PDU session app has no transport-layer socket to bind to — it must
  sit directly on top of the network-layer dispatcher, one layer below where UDP/TCP
  apps sit. This is why `L2App` is not declared `like IApp`: `IApp`'s gate contract
  presumes exactly the transport-layer attachment this design must avoid.
- `etherType` defaults to `0x88B5` (IEEE 802 "Local Experimental Ethertype 1"), a real
  reserved-for-experimental EtherType value, rather than an invented magic number.
- The four signals mirror, in spirit, the reference shim's `TsnApp`/`L2App`
  sent/received/dropped/delay signals — but here they are emitted around a module that
  builds genuine wire bytes, not metadata.

**Replaces:** the shim's `TSNApp`/`L2App` pair (Section 9.3/9.5 of the reference
report), which built a `Packet` carrying only a `TsnEtherTag` metadata tag and a
`ByteCountChunk` with no real Ethernet header at all.

## File: `src/apps/l2/L2App.h` / `src/apps/l2/L2App.cc`
**Baseline:** No baseline — new file.

**New code — statement-by-statement (the non-trivial parts of `L2App.cc`):**
- `srcMac_.setAddress(par("srcMac").stringValue())` / `destMac_...`: parses the
  NED string parameters into real `inet::MacAddress` objects once, at init, rather
  than re-parsing a string on every packet (as the shim's `TsnMacForwarder::normalizeMac`
  had to do repeatedly because it only ever carried MACs as `string` fields in a tag).
- `if (strlen(par("destMac")...) > 0) { selfSender_ = new cMessage(...); scheduleAt(...) }`:
  only the sending side of a flow (the side actually configured with a destination)
  starts the periodic send timer; a pure sink instance (destMac left empty in the
  `.ini`) never schedules a send, so the same module type serves as both source and
  sink depending on configuration — avoiding a separate sink module for the base class.
- `buildFrame()`:
  - `pkt->insertAtBack(makeShared<ByteCountChunk>(packetLength_))`: the frame's data
    payload, represented compactly (simulation does not need real application bytes,
    only their count/length, which is the standard INET idiom for payload placeholders).
  - `auto ethHeader = makeShared<EthernetMacHeader>(); ethHeader->setDest(...);
    ethHeader->setSrc(...); ethHeader->setTypeOrLength(etherType_);
    pkt->insertAtFront(ethHeader)`: this is the real, spec-defined Ethernet II header
    (`inet::EthernetMacHeader`, confirmed present at
    `inet4.5/src/inet/linklayer/ethernet/common/EthernetMacHeader.msg`), inserted as
    genuine framed bytes at the front of the packet — this is the literal fix for the
    reference shim's Section 8 "Not Modeled: Serialized Ethernet headers on the INET
    Ethernet stack" row.
  - `pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac)`:
    tags the packet with INET's own pre-existing `Protocol::ethernetMac` constant
    (confirmed present in `inet/common/Protocol.cc:101` — no new protocol invented).
    Every downstream module in this task's plan (`TrafficFlowFilter`, `GtpUser`,
    `IP2Nic`, `LtePdcpRrcBase`) branches on this real tag instead of the shim's
    `findTag<TsnEtherTag>() != nullptr` ad hoc check.
  - `pkt->addTagIfAbsent<CreationTimeTag>()->setCreationTime(simTime())`: reuses INET's
    own `CreationTimeTag` (already used elsewhere in Simu5G, e.g. `CbrSender.cc:132`)
    for delay measurement, instead of the shim's bespoke `TsnEtherTag.radioIngressTime`
    field — one fewer custom field to maintain, one more reused mechanism.
- `handleFrame()`:
  - `auto ethHeader = pkt->popAtFront<EthernetMacHeader>()`: pops the real header
    off the real bytes (as opposed to the shim's `L2App::handleMessage`, which did
    `pkt->findTag<TsnEtherTag>()` — a tag lookup, not a header pop, because there
    was no header to pop).
  - Null check + `framesDroppedSignal_`: a frame arriving without a valid Ethernet
    header is genuinely malformed and is dropped, not silently treated as absent-IP.
  - Delay computed from the real `CreationTimeTag`, emitted on `frameDelaySignal_`.

**Why this design (spec/architectural justification):** TS 23.501 §5.6.10.2 (Ethernet
PDU Session type) requires the UE-facing and AF-facing traffic to be genuine Ethernet
frames; the reference shim modeled this with a tag instead of bytes. Using INET's own
`EthernetMacHeader`/`Protocol::ethernetMac` is the minimal, already-existing mechanism
that satisfies this without inventing new wire formats, per the task's "prefer
extending existing Simu5G/INET mechanisms" instruction.

**Open item carried into the next entry:** how `L2App`/`TsnEtherApp` attach to `server`
(StandardHost) and `ue[]` (NRUe) — resolved in `01-l2app-attachment.md`.
