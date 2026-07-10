# 20 — Reuse real EthernetEncapsulation, direct-wired (not "nl")

## Why

Entry 19's fix (`TsnEtherApp::buildFrame()` hand-inserting a placeholder
`EthernetFcs`) is, statement-for-statement, identical to what
`EthernetEncapsulation::processPacketFromHigherLayer()` itself does:

```cpp
ethHeader->setSrc(srcAddr);
ethHeader->setDest(macAddressReq->getDestAddress());
ethHeader->setTypeOrLength(typeOrLength);
packet->insertAtFront(ethHeader);
const auto& ethernetFcs = makeShared<EthernetFcs>();
ethernetFcs->setFcsMode(fcsMode);
packet->insertAtBack(ethernetFcs);
```

Yet after a confirmed **clean** rebuild, the exact same error persisted:

```
Cannot convert chunk from type inet::ByteCountChunk to type inet::EthernetFcs.
... in module (inet::EthernetMac) SingleCell_Standalone.server.tsnEth.mac
```

This ruled out both a stale-build explanation and a simple
construction-order mistake (the code matched INET's own reference
implementation). Rather than keep guessing at the exact interaction between
hand-built chunks and the `PriorityQueue`/`EthernetMac` pipeline, the fix is
to stop hand-building the frame and delegate to the real
`inet::EthernetEncapsulation` module — proven correct by virtue of being the
class every other INET Ethernet host already uses — while keeping the one
genuinely necessary property from entries 15/16: **no shared "nl"
MessageDispatcher**, since that was the original, independently-confirmed
source of two earlier "protocol is already registered" collisions.

## The fix: real encap, direct-wired

**File:** `src/nodes/EthernetPduSessionHost.ned`
- Added a `tsnEncap: EthernetEncapsulation` submodule (`registerProtocol = false`).
  It is deliberately **not** named `ethernet`: `StandardHost`/`LinkLayerNodeBase`
  already declares an inherited submodule of that exact name (conditional on
  `numEthInterfaces>0`, which we leave at 0), and re-declaring it with a
  concrete type is a NED name-collision error (the IDE flags it with a red X;
  it fails to load). `tsnEncap` is a fresh, dedicated name.
- Wired directly, module-to-module, with no dispatcher in between:
  `l2App[0].out --> tsnEncap.upperLayerIn`, `tsnEncap.upperLayerOut --> l2App[0].in`,
  `tsnEncap.lowerLayerOut --> tsnEth.upperLayerIn`, `tsnEth.upperLayerOut --> tsnEncap.lowerLayerIn`.
- Since there is only one possible destination for a packet at each of these
  gates, there is no dispatcher/registration decision to make — the
  "protocol is already registered" failure mode is structurally impossible
  here, independent of whatever the exact root cause of entry 19's bug
  turns out to be.

**File:** `src/apps/l2/TsnEtherApp.cc`
- `buildFrame()` reverted to building the *inner* frame only —
  `[EthernetMacAddressFields][Ieee8021qTagEpdHeader][payload]` — the same
  content `EthernetEncapsulation` will wrap in its own real outer
  `EthernetMacHeader` + `EthernetFcs`. `MacAddressReq`, `PacketProtocolTag`,
  and `PcpReq` tags are unchanged (encap reads `MacAddressReq` for the outer
  header's dest/src; the classifier still reads `PcpReq`, per entry 18).
- `initialize()`: re-added the `ProtocolGroup::getEthertypeProtocolGroup()`
  registration for `Protocol::ethernetMac` (previously in `L2App.cc`, removed
  in entry 16, now needed again because encap calls
  `getProtocolNumber(protocol)` and throws if it isn't registered).
- `handleFrame()`: reverted to popping `EthernetMacAddressFields` first (not
  a full `EthernetMacHeader`) — the outer header was already stripped by
  `TrafficFlowFilter` at the UPF, so what reaches the UE is exactly what
  `buildFrame()` built.

**File:** `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc`
- `handleEthernetPacket()`: reverted to popping the outer `EthernetMacHeader`
  + `EthernetFcs` (encap's own real frame, arriving intact since the UPF's
  `tsnEth` doesn't decapsulate), then peeking `EthernetMacAddressFields` for
  the destination MAC used in classification.

This is functionally back to entry 15/16's original three-chunk design on
the wire (`[outer EthernetMacHeader][EthernetMacAddressFields][Ieee8021qTagEpdHeader][payload][EthernetFcs]`),
with the two fixes from entries 15/16 and 18 kept: the UPF's dedicated
direct-wired `tsnEth` (still needed — the UPF side never had the FCS bug),
and PcpReq-tag-based classification (still needed — independent of frame
structure, and avoided a separate, still-unexplained chunk issue at the
classifier).
