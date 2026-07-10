# Documentation entry: IP2Nic Ethernet destId resolution + delivery (gNB downlink-in, UE downlink-out)

## Files: `src/stack/ip2nic/IP2Nic.{h,cc}`
**Baseline (confirmed by direct read):** `fromIpBs()` (gNB/eNB side, packet arriving
from the IP layer bound for a UE) unconditionally peeks `Ipv4Header`, checks
IP-address-keyed handover-forwarding/holding maps, then calls `toStackBs()`, which
peeks `Ipv4Header` again, extracts ToS/header-size, and populates `FlowControlInfo`
(`srcAddr`/`dstAddr`/`typeOfService`/`headerSize`) — but **not** `destId`; that is
resolved again, redundantly, inside PDCP. `toIpUe()` (UE side, packet arriving from
the radio stack bound for the IP layer) unconditionally peeks `Ipv4Header` and tags
`Protocol::ipv4` via `prepareForIpv4()`. `markPacket()`'s bearer-selection policy
(LTE/NR/split) is entirely driven by `FlowControlInfo::getDstAddr()`/ToS thresholds.

**Modified/new code:**
- New method `toStackBsEthernet(Packet*)` (declared in `IP2Nic.h`, defined in
  `IP2Nic.cc`), called from `fromIpBs()` when `PacketProtocolTag == ethernetMac`,
  **before** the `Ipv4Header` peek (so that peek is never reached for this session
  type at all).
- `toIpUe()` gains an early branch for the same tag, before its own `Ipv4Header` peek.

**Statement-by-statement / why:**
- `fromIpBs()`: `if (pkt->getTag<PacketProtocolTag>()->getProtocol() ==
  &Protocol::ethernetMac) { toStackBsEthernet(pkt); return; }` — skips the
  IP-address-keyed handover-forwarding/holding logic entirely (handover is not
  implemented for Ethernet PDU sessions in this scope, alongside uplink — see the
  final report) and routes straight to the Ethernet-specific path.
- `toStackBsEthernet()`:
  - `pkt->peekAtFront<EthernetMacAddressFields>()->getDest()`: reads the real
    destination MAC directly off the still-fully-intact frame (nothing has popped it
    yet at this point in the pipeline).
  - `binder_->getNodeIdForMacAddress(destMac)`: **the single destId resolution point
    for the entire downlink Ethernet PDU session path** — the same Binder-backed
    lookup `TrafficFlowFilter`'s Ethernet branch already uses (see its doc entry),
    reused here rather than re-implemented.
  - `lteInfo->setDestId(destId)`: stores the already-resolved destId directly on the
    `FlowControlInfo` tag, so `NRPdcpRrcEnb::fromDataPort()` (see below) can read it
    back instead of re-deriving it from an IP address that does not exist for this
    packet.
  - `lteInfo->setUseNR(true)`: this scope's topology is single-cell, pure-NR (no dual
    connectivity, no LTE fallback), so the existing IP-address-driven `markPacket()`
    threshold policy (SS5.6.10.2's real behavior would need a per-session bearer
    policy anyway) does not apply here and is bypassed rather than generalized —
    a deliberate, documented scope simplification, not an oversight.
  - `vlanHeader->getPcp()` written into `lteInfo->setTypeOfService(...)`: reads the
    real, still-present 802.1Q PCP value (peeked, not popped, at the correct byte
    offset past the address fields) and carries it forward as this packet's ToS-like
    priority value — available for future MAC-scheduler QoS integration (see the
    final report's scope section for why that integration itself is not implemented).
- `toIpUe()`: `if (... == ethernetMac) { prepareForIpv4(pkt, &Protocol::ethernetMac);
  send(pkt, ipGateOut_); return; }` — reuses `prepareForIpv4()` completely unchanged
  (it was already parameterized by `const Protocol* protocol`, just always called
  with the default `&Protocol::ipv4` before) to tag `DispatchProtocolReq`/
  `PacketProtocolTag`/`InterfaceInd` for `ethernetMac` instead, and sends out the
  identical `ipGateOut_` gate. Because `L2App` registered itself for
  `Protocol::ethernetMac` on the same `nl` dispatcher `ipv4` is registered on (see
  entry 01), this one small change is sufficient for the packet to reach the UE's
  `l2App[0]` (the `TsnEtherApp` sink) instead of `ipv4` — no new dispatch mechanism,
  just the existing one used for a second, real protocol.

**Why (architectural justification):** this is the direct implementation of Layer A's
requirement that "the actual UE resolution occurs at the gNB ingress adapter" and that
PDCP/RLC/MAC/PHY carry the real frame with FlowControlInfo populated from a real
lookup — except here the "ingress adapter" role the reference shim gave to a bespoke
`TsnRadioIngress` module is filled by `IP2Nic`, a module Simu5G already has at exactly
that position in the pipeline (between the tunnel-facing side and PDCP), reusing its
existing `FlowControlInfo`-population responsibility instead of inserting a new module.

## File: `src/stack/pdcp_rrc/layer/NRPdcpRrcEnb.cc`
**Baseline:** `fromDataPort()` unconditionally computed `srcId`/`destId` via
`binder_->getNrMacNodeId(Ipv4Address(...))`/`getMacNodeId(...)`, and unconditionally
ran the D2D-capability check based on those IDs.

**Modified code:** wrapped the entire block in `if (isEthernet) { destId =
lteInfo->getDestId(); srcId = nodeId_; ...D2D fields zeroed... } else { ...original
code, verbatim... }`, where `isEthernet` is the same `PacketProtocolTag ==
ethernetMac` check used throughout this project.

**Statement-by-statement / why:**
- `destId = lteInfo->getDestId()`: reads the value `IP2Nic::toStackBsEthernet()`
  already resolved and stored — no second Binder lookup, and critically, no attempt
  to construct an `Ipv4Address` from a `srcAddr`/`dstAddr` that are both `0` for this
  session type (which would otherwise silently resolve to "not found" / node 0).
- `srcId = nodeId_`: this gNB's own MacNodeId — reasonable for a downlink-only flow
  where the gNB is always the PDCP-layer source of the frame (the AF is upstream of
  the whole 5G segment).
- D2D fields zeroed unconditionally: D2D delivery is not implemented for Ethernet PDU
  sessions in this scope (an orthogonal simplification to the uplink/handover ones
  already noted).
- The `else` branch is the **original code, unchanged, character-for-character** —
  the regression-safety guarantee for every existing IP scenario.

**Replaces:** the reference shim's `TsnRadioIngress::getDestinationNodeId()` (Section
9.10 of the reference report), which performed this exact "resolve destination
identity, write it into FlowControlInfo" job itself, in a bespoke module inserted
before the cellular NIC. Here the same job is done by `IP2Nic` (destId resolution)
and consumed by the same `NRPdcpRrcEnb::fromDataPort()` every other flow already goes
through — no parallel ingress adapter.
