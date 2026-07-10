# Documentation entry: Post-implementation review bug fixes

These four defects were found during a line-by-line self-review after the user reported
(a) an initialization crash on the `EthernetPduSession` config, and (b) traffic
animating through the network but zero packets received at the UE. All four are fixed;
the codebase rebuilds cleanly (`libsimu5g_dbg.dll`, zero errors) after the fixes.

## Bug 1 — Empty `destMac` crash at initialization (BLOCKER, user-visible)

**Symptom (reported by user):**
`MacAddress: wrong address syntax '': 12 hex digits expected -- in module (L2App)
SingleCell_Standalone.ue[0].l2App[0] (id=297), during network initialization`

**File:** `src/apps/l2/L2App.cc`, `initialize()`.

**Cause:** the UE-side sink instance is configured with `destMac = ""` (empty) to mean
"receive only, never send" — the send-timer guard (`if (strlen(par("destMac")...) > 0)`)
correctly handled that intent, but the line `destMac_.setAddress(par("destMac").stringValue())`
ran *unconditionally* before that guard. `inet::MacAddress::setAddress()` throws on an
empty string.

**Fix:** parse `destMac` only when non-empty:
```cpp
if (strlen(par("destMac").stringValue()) > 0)
    destMac_.setAddress(par("destMac").stringValue());
```
This is purely init-time; a pure sink never sends, so `destMac_` staying default-
constructed (`00:00:00:00:00:00`) is never read.

## Bug 2 — GTP-U tunnel misrouted to the UE instead of the gNB (ROOT CAUSE of zero packets received)

**Symptom (reported by user):** animation shows traffic leaving the UPF, but the UE's
`TsnEtherApp` receives nothing.

**Files:** `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc` (`handleEthernetPacket`),
`src/corenetwork/gtp/GtpUser.cc` (`handleFromTrafficFlowFilter`).

**Cause:** `handleEthernetPacket()` originally set `TftControlInfo.tft = destId`, where
`destId` is the *UE's own* MacNodeId. But the pre-existing, unchanged `GtpUser` code
treats that `tft` value as the GTP-U tunnel *peer* and passes it to
`binder_->getModuleNameByMacNodeId(flowId)` → `L3AddressResolver().resolve(...)` →
`socket_.sendTo(...)`. Resolving the *UE's* module name yields the *UE's* IP address,
so the GTP-U packet was UDP-sent to the UE's own address rather than to `gnb`. Nothing
at the UE listens on the GTP-U port, so the packet was silently dropped by the UE's UDP
layer after the animation showed it leaving the UPF — exactly the "moves but never
arrives" symptom.

The pre-existing IP path never had this bug because its own `findTrafficFlow()` returns
`destMaster` — the serving *base station's* MacNodeId — as the `tft`, never the UE's.

**Fix:** mirror the IP path. `handleEthernetPacket()` now resolves the serving base
station and puts *that* in `TftControlInfo.tft`, while carrying the UE's own MacNodeId
separately in `FlowControlInfo.destId`:
```cpp
MacNodeId destBS = binder_->getNextHop(destId);
MacNodeId destMaster = binder_->getMasterNode(destBS);
pkt->addTagIfAbsent<FlowControlInfo>()->setDestId(destId);   // UE id, read at the gNB
tftInfo->setTft(destMaster);                                 // BS id, used for tunnel routing
```
`GtpUser::handleFromTrafficFlowFilter()` correspondingly now reads the UE id from
`FlowControlInfo.destId` (not from `flowId`) when populating the real GTP-U `teid`:
```cpp
if (isEthernet) {
    MacNodeId ueDestId = datagram->getTag<FlowControlInfo>()->getDestId();
    header->setTeid(static_cast<unsigned int>(ueDestId));
}
```
With this fix the GTP-U tunnel peer is `gnb`, the frame reaches the gNB's `GtpUser`,
is decapsulated, and continues down the real NR stack to the UE.

## Bug 3 — Stale `setChunkLength(B(8))` in the IP re-tunnel branch (latent serialization error)

**File:** `src/corenetwork/gtp/GtpUser.cc`, `handleFromUdp()` UPF re-tunnel branch.

**Cause:** a leftover `header->setChunkLength(B(8))` from the original 1-byte-declared/
8-byte-written stub `GtpUserMsg`. The real header now declares and serializes a
consistent 12 bytes; forcing 8 here desyncs the chunk's declared length from what the
serializer writes, which `FieldsChunkSerializer` treats as an error the next time the
packet is serialized. This path is only reached in multi-UPF re-tunneling (not the
single-gNB downlink of this scenario), so it was latent — fixed by removing the
override so the `.msg`-declared 12 B stands.

## Bug 4 — `getTag<PacketProtocolTag>()` could throw after RLC round-trip (defensive hardening)

**Files:** `LtePdcpRrc.cc` (headerCompress/headerDecompress/toDataPort), `NRPdcpRrcEnb.cc`
(fromDataPort), `IP2Nic.cc` (fromIpBs/toIpUe), `GtpUser.cc` (handleFromTrafficFlowFilter).

**Cause / concern:** `getTag<T>()` throws if the tag is absent. Whether the original
`PacketProtocolTag` survives RLC segmentation/reassembly (`UmTxEntity::rlcPduMake()`
creates *new* `Packet` objects for PDU fragments) was not verified by test execution.
If it does not survive on some path, a `getTag()` call on the RX side would crash the
simulation rather than fall back gracefully.

**Fix:** all protocol-tag checks changed from `getTag<PacketProtocolTag>()` to
`findTag<PacketProtocolTag>()` (returns `nullptr` instead of throwing), treating an
absent tag as "not Ethernet" — the historical default behavior for every pre-existing
IP packet. This is a strict safety improvement regardless of whether the tag actually
survives: correct behavior if it does, no crash if it doesn't.

## Bug 5 — Wrong egress interface name + needless lookup on the sink (BLOCKER, user-visible)

**Symptom (reported by user):**
`L2App: interface 'cellularNic' not found in InterfaceTable -- in module (L2App)
SingleCell_Standalone.ue[0].l2App[0] (id=297), during network initialization`
(this appears *after* Bug 1 is fixed — i.e. the sim now gets further into init.)

**File:** `src/apps/l2/L2App.{ned,cc}`, `initialize()`.

**Two causes, both fixed:**
1. **Wrong name.** `L2App`'s `interfaceName` defaulted to `"cellularNic"` — but that is
   the NED *submodule* name. The actual registered `NetworkInterface` name is set by
   `IP2Nic::registerInterface()` from `IP2Nic`'s own `interfaceName` parameter, whose
   default is **`"cellular"`** (confirmed at `IP2Nic.ned:28`). Fixed the `.ned` default
   to `"cellular"`.
2. **Needless lookup on a sink.** The UE-side `l2App[0]` is a pure *sink* (`destMac=""`,
   never sends), so it never sets `InterfaceReq` and never needs an egress interface at
   all. The lookup was run unconditionally in `initialize()`, so it threw on the sink
   even though the value was never used. Fixed by moving the interface resolution
   *inside* the `if (strlen(destMac) > 0)` sender-only block — a sink now skips it
   entirely.

**Why it was latent until now:** Bug 1 (empty-destMac crash) aborted init before this
line was reached; fixing Bug 1 let init proceed to this next unconditional lookup.

**Dispatch path confirmed sound (no bug):** while fixing Bug 5, the UE-side delivery
path was verified against INET's real `MessageDispatcher.cc`: `L2App::initialize()`
calls `registerProtocol(Protocol::ethernetMac, gate("out"), gate("in"))` — the exact
2-gate form `inet::Ipv4` uses (`registerProtocol(Protocol::ipv4, queueOut, queueIn)`),
registering the app's `in` gate for `ethernetMac`+`SP_INDICATION`. `IP2Nic::toIpUe()`
sets both `DispatchProtocolReq` and `PacketProtocolTag` to `ethernetMac`, so the
dispatcher's `-1` service-primitive KLUDGE resolves them to `SP_INDICATION` and routes
the frame up to `l2App`. This mirrors the proven IP delivery path exactly.

## Verification status after fixes

Rebuilt successfully (`make MODE=debug`, `libsimu5g_dbg.dll`, zero errors, only
pre-existing unrelated warnings). Runtime execution from this environment remains
blocked by the pre-existing local OMNeT++ Qt/ICU DLL mismatch (see entry 99, §12) —
these fixes are verified at the compile/static-review level; the fix for Bug 2 in
particular should be confirmed at runtime by checking that
`ue[0].l2App[0]` reports nonzero `framesReceived`.
