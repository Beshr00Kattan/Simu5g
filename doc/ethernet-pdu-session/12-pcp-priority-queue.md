# Documentation entry: TsnPcpClassifier + PriorityQueue wiring (Layer B PCP priority)

## Files: `src/apps/l2/TsnPcpClassifier.{ned,h,cc}`
**Baseline:** No baseline — new file.

**New code / statement-by-statement:**
- `TsnPcpClassifier.ned`: `simple TsnPcpClassifier extends PacketClassifierBase like
  IPacketClassifier` — extends the real INET base class every classifier (including
  INET's own `PcpClassifier`/`PcpTrafficClassClassifier`) extends, inheriting its
  `in`/`out[]` gate contract and statistics signals rather than declaring new ones.
- `classifyPacket(Packet *packet)`:
  - `packet->peekAt<Ipv4Header>(offset)` then advances `offset` by its real
    `getChunkLength()`, then the same for `UdpHeader`, then
    `EthernetMacAddressFields`, arriving at the real `Ieee8021qTagEpdHeader` chunk —
    every step uses **non-destructive** `peekAt()`, so the packet leaving this
    classifier (queued, then sent to `PppInterface`) is byte-for-byte identical to
    what arrived; nothing is popped or reordered.
  - `return (vlanHeader->getPcp() >= highPriorityMinPcp_) ? 0 : 1;` — real,
    still-on-the-wire PCP value drives the classification decision.

**Why this is a new class instead of reusing INET's own `PcpClassifier`:** confirmed
by reading `PcpTrafficClassClassifier.cc` during planning that INET's classifier
reads an out-of-band `PcpInd`/`PcpReq` **tag**, populated only after a
`Ieee8021qTagEpdHeaderChecker` has already popped the real header off the packet.
This project deliberately never pops the VLAN header at any intermediate hop — it
must still be on the wire when it reaches the UE (see `TsnEtherApp`'s doc entry) — so
no such tag is ever created, and INET's classifier would only ever see its
`defaultGateIndex`, i.e. no real differentiation at all. `TsnPcpClassifier` instead
reads the real header directly, peeking through this project's own known tunnel
encapsulation (documented in `TsnEtherSourceApp`'s header comment) — a deliberately
project-specific classifier, since no generic one exists that both (a) knows this
project's exact wire layout and (b) doesn't consume the tag.

## Config: `simulations/NR/standalone/omnetpp.ini`
**New lines** (in `[Config EthernetPduSession]`, see its own doc entry):
```ini
*.server.ppp[0].queue.typename = "inet.queueing.queue.PriorityQueue"
*.server.ppp[0].queue.numQueues = 2
*.server.ppp[0].queue.classifier.typename = "simu5g.apps.l2.TsnPcpClassifier"
```
**Why no NED change was needed:** `inet::PppInterface.ned`'s `queue` submodule is
already declared as a polymorphic slot (`queue: <default("DropTailQueue")> like
IPacketQueue`), and `inet::queueing::queue::PriorityQueue.ned`'s own `classifier`
submodule is likewise polymorphic (`classifier: <default("PacketClassifier")> like
IPacketClassifier`) — both are retypeable purely from `.ini`, so real multi-queue
strict-priority scheduling (`PriorityQueue`'s own `scheduler: <default("PriorityScheduler")>`,
unchanged, real INET code) is achieved by configuration alone, reusing three real
INET modules (`PriorityQueue`, `PriorityScheduler`, and our classifier) with zero
NED/topology changes to `PppInterface` or `StandardHost` themselves.

**Scope note (documented, not a gap):** this wiring is applied at `server.ppp[0]`
(the N6 leg, server→router→UPF) — the first, and in this scope's downlink-only flow
the only, real multi-flow contention point where the VLAN tag sits at a fixed, known
depth in the chunk stack. Applying the equivalent classification after GTP-U
encapsulation (the UPF→gNB leg) would need a second, near-identical classifier
peeking one layer deeper (past `GtpUserMsg` + the optional `GtpUserExtHeaderPduSessionContainer`)
and is straightforward given `TsnPcpClassifier`'s pattern, but is not wired in this
pass — noted alongside the uplink and handover scope boundaries already documented,
not silently omitted.

**Explicitly out of scope (per the task's own instruction), noted here at the exact
relevant point:** IEEE 802.1Qbv gate-control-list / time-synchronized transmission
windows. `PriorityQueue`'s `PriorityScheduler` is a real strict-priority scheduler —
it always drains the higher-priority queue first — but it has no time reference, no
gate-control list, and no synchronized schedule; both queues are always open. Adding
TAS would require gPTP time synchronization, a GCL scheduling engine, and
residence-time modeling — a separate, substantially larger undertaking than this
thesis scope covers (see the final report's dedicated scope section).
