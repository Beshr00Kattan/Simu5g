# Documentation entry: TrafficFlowFilter Ethernet branch (+ Binder MAC table, + tsnMac registration)

## Files: `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.{h,cc}`
**Baseline (confirmed by direct read):** `handleMessage()` unconditionally
`peekAtFront<Ipv4Header>()`s the incoming packet, extracts src/dst IP, calls
`findTrafficFlow()` (Binder IP→MacNodeId lookups only), tags `TftControlInfo`, and
sends to `gtpUserGateOut`. No non-IP branch existed.

**Modified/new code:**
- `#define ETHERNET_PDU_SESSION_N6_PORT 3000` (header) — the well-known UDP port
  `TsnEtherSourceApp` tunnels Ethernet frames to across the N6 IP segment (see entry
  03). Matches `TsnEtherSourceApp.ned`'s `destPort` default.
- `MacForwardingTable* macForwardingTable_` member, resolved in `initialize()` only if
  `par("macForwardingTableModule")` is non-empty (true only for the UPF's own
  `trafficFlowFilter` instance, per `Upf.ned`).
- `handleMessage()`: before the existing IP classification code runs, peeks (does
  **not** pop) the `Ipv4Header`, and if its protocol is UDP and the UDP destination
  port is `ETHERNET_PDU_SESSION_N6_PORT`, calls the new `handleEthernetPacket(pkt)`
  and returns — the entire pre-existing IP branch below is untouched and still runs,
  byte-for-byte as before, for every packet that isn't this specific tunnel traffic
  (this is the regression-safety guarantee for VoIP-DL/VoIP-UL).
- `handleEthernetPacket(Packet*)` (new method):
  1. `pkt->popAtFront<Ipv4Header>(); pkt->popAtFront<UdpHeader>();` — *now*
     destructively strips the outer N6 transport shell, safe because the port check
     already confirmed this packet is genuinely the tunneled Ethernet frame. From
     this point on the packet contains only the real frame
     (`EthernetMacAddressFields` + `Ieee8021qTagEpdHeader` + payload).
  2. `pkt->peekAtFront<EthernetMacAddressFields>()->getDest()`: reads the real
     destination MAC directly off the wire bytes.
  3. `pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac)`:
     tags the now-IP/UDP-free packet with its real protocol, for every downstream
     module (GtpUser, PDCP) to branch on.
  4. `macForwardingTable_->getUnicastAddressForwardingInterface(destMac)`: the real,
     reused `inet::MacForwardingTable` lookup (see Upf.ned's doc entry for why this
     table's `interfaceId` slot holds a `MacNodeId` here instead of a real interface
     id). A hit means a MAC this table already learned/knows about.
  5. On a **miss**, falls back to `binder_->getNodeIdForMacAddress(destMac)` — the
     UE's MAC registered by `LteMacUe.cc` at attach time (see below) — and, if found,
     calls `macForwardingTable_->learnUnicastAddressForwardingInterface(destId,
     destMac)` to seed the real table, so subsequent lookups for the same
     destination hit the table directly (exercising its real aging/lookup logic from
     that point on).
  6. If Binder doesn't know the MAC either, the packet is dropped (with an `EV` log
     line) rather than flooded to every Ethernet-session UE.

**Why the Binder fallback exists (honest scope note):** TS 23.501 §5.6.10.2's real
mechanism is the UPF learning UE-side MAC addresses **from observed uplink frames**.
This project's scope (matching the reference shim's own scope, and confirmed
practical given the remaining time budget) implements the **downlink** direction only
— `TsnEtherApp` on the UE side is configured as a sink (no destMac configured, so it
never sends). With no uplink Ethernet traffic ever flowing through this UPF, the real
`MacForwardingTable` would never learn anything on its own, and the FDB would stay
permanently empty regardless of how many UEs exist — defeating its purpose. The
Binder-backed fallback (itself populated from each UE's own declared `tsnMac`
parameter, registered the same "configure once, register at attach" way
`pduSessionType` is) supplies the missing bootstrap data, and the real table is still
the thing that answers every lookup after the first. **Uplink Ethernet PDU session
traffic (UE→AF) is explicitly out of scope for this project** (alongside 802.1Qbv/TAS
and DS-TT/NW-TT) — see the final report's scope section.

**Why drop-on-unknown instead of flood:** implementing genuine flood would require
`GtpUser`/`TftControlInfo` to support fan-out to multiple destinations (currently one
`tft` id per packet); given this scope's UE population is always known via `tsnMac`
registration, an unknown destination MAC is only reachable by misconfiguration, so
dropping (with a clear log line) is a proportionate, documented simplification rather
than an unhandled gap.

**Replaces:** the reference shim's `TsnRadioIngress::getDestinationNodeId()` linear
scan over a static `ue[i].tsnMac` parameter array with string comparison (Section 4/
9.10 of the reference report) and `TsnMacForwarder`'s single-string `allowedDst`
(Section 9.6) — both replaced by a real, keyed, aging-capable lookup table supporting
an arbitrary number of UEs with no special-casing.

## File: `src/common/binder/Binder.h` (amendment)
**Modified/new code:** `std::map<inet::MacAddress, MacNodeId> ueMacAddressToNodeId_;`
+ `setUeMacAddress(MacAddress, MacNodeId)` / `getNodeIdForMacAddress(MacAddress) ->
MacNodeId` (0 = not found, matching `getMacNodeId()`'s existing convention).
**Why:** the authoritative, per-UE registered-at-attach MAC table described above.

## File: `src/nodes/Ue.ned` (amendment)
**Modified/new code:** `string tsnMac = default("");` parameter, next to
`pduSessionType`.

## File: `src/stack/mac/layer/LteMacUe.cc` (amendment)
**Modified/new code:** immediately after the `setPduSessionType()` call (see entry
05), if the session type is `ETHERNET` and `tsnMac` is non-empty:
```cpp
binder_->setUeMacAddress(MacAddress(tsnMacStr), nodeId_);
```
**Why:** registers the UE's MAC at the exact same lifecycle point (and using the same
`getAncestorPar()` idiom) as `pduSessionType` and the pre-existing IP↔MacNodeId
registration — one consistent "attach" moment for every per-UE Ethernet PDU session
attribute this project introduces.
