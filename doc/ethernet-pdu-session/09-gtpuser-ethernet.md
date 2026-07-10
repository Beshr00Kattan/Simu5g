# Documentation entry: GtpUser Ethernet payload branch

## File: `src/corenetwork/gtp/GtpUser.cc`
**Baseline (confirmed by direct read):** `handleFromTrafficFlowFilter()`
unconditionally peeked `Ipv4Header` for `destAddr` (used only by the `-1`/`-3`
branches), always built `GtpUserMsg` with `teid=0` and a hardcoded `chunkLength=B(8)`,
and always wrapped `datagram->peekData()` (the whole IP datagram) as the GTP-U
payload. `handleFromUdp()` symmetrically always re-tagged the decapsulated packet
`Protocol::ipv4` and always peeked `Ipv4Header` for `destAddr`.

**Modified/new code — statement-by-statement:**
- `handleFromTrafficFlowFilter()`:
  - `bool isEthernet = datagram->getTag<PacketProtocolTag>()->getProtocol() ==
    &Protocol::ethernetMac`: reads the real protocol tag `TrafficFlowFilter` already
    set (either `ipv4`, unchanged, or `ethernetMac`, new — see its doc entry) to
    decide which branch to take, instead of an absent-tag heuristic.
  - The `Ipv4Header` peek for `destAddr` is skipped entirely when `isEthernet` — there
    is no IP header on this packet at all at this point (`TrafficFlowFilter` already
    stripped the outer IP/UDP N6 transport shell). `destAddr` stays default-constructed
    and is only read further down inside the `-1`/`-3` branches, which this project's
    Ethernet PDU session flow never reaches (`handleEthernetPacket` only ever returns
    a real `destId` or drops the packet itself).
  - `header->setTeid(flowId > 0 ? static_cast<unsigned int>(flowId) : 0)`: **the
    single field that carries session identity through the tunnel** — replaces the
    baseline's hardcoded `0` for both the pre-existing IP branch and the new Ethernet
    branch alike, giving `teid` its real, spec-intended meaning for the first time in
    this codebase.
  - `gtpPacket->insertAtBack(datagram->peekData())` then, only if `isEthernet`,
    `gtpPacket->insertAtFront(extHeader)` before `gtpPacket->insertAtFront(header)`:
    builds the chunk stack `[GtpUserMsg][GtpUserExtHeaderPduSessionContainer][real
    Ethernet frame bytes]` — the extension header is only present for the Ethernet
    branch (`header->setEFlag(true); header->setNextExtensionHeaderType(0x85)`),
    matching TS 29.281's real extension-header chaining semantics (absent unless
    explicitly signaled).
- `handleFromUdp()`:
  - `PduSessionType sessionType = binder_->getPduSessionType(gtpUserMsg->getTeid())`:
    **the decapsulating side's entire dispatch decision**, resolved from the real
    TEID this project now writes, not from inspecting the payload's bytes (which
    would require already knowing the payload's type to safely peek it — circular)
    and not from a repeated per-packet type flag (see `GtpUserMsg`'s doc entry for why
    that would contradict the "session type is per-session" modeling decision).
  - `if (gtpUserMsg->getEFlag() && ... == 0x85) pkt->popAtFront<GtpUserExtHeaderPduSessionContainer>()`:
    removes the extension header chunk (if present) from the chunk stack before
    reconstructing the inner packet, so `pkt->peekData()` afterward yields exactly
    the real Ethernet frame bytes with nothing extraneous prepended.
  - `originalPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(...)`: now
    branches on `sessionType` instead of unconditionally writing `ipv4` — this is the
    exact fix for the reference report's own observation (Section 9.16 of the
    reference report, about the *shim's* PDCP code) that a payload's true protocol
    should govern behavior; here it is fixed one layer earlier, at the point the
    protocol tag is actually (re)established after detunneling.
  - The new `if (sessionType == PduSessionType::ETHERNET) { ...; send(originalPacket,
    "pppGate"); return; }` early-return branch handles Ethernet PDU session local
    delivery to the UE's radio stack (still tagging `InterfaceReq` for the cellular
    NIC, identically to the IP branch) and exits before reaching the
    `peekAtFront<Ipv4Header>()` line and the MEC/re-tunneling logic below it, both of
    which are IP-address-driven and do not apply to a session with no IP address.

**Why (architectural justification):** this is the direct implementation of Layer A's
"GtpUser: real byte-serialized Ethernet payload" and "gNB/UE: replace the old
TsnRadioIngress metadata mapping with the real mechanism" requirements — the
destination is resolved once, at the UPF (`TrafficFlowFilter`), carried through the
tunnel via a real field (`teid`) with genuine spec meaning, and the gNB-side
`GtpUser` needs no MAC-address knowledge or lookup of its own at all: it only asks
`Binder::getPduSessionType()`, then hands the still-fully-intact real Ethernet frame
to the radio stack.

**Explicit scope boundary carried over from `TrafficFlowFilter`'s doc entry:** only
the downlink direction (server → UPF → gNB → UE) is implemented. Uplink Ethernet PDU
session traffic would require `GtpUser`'s uplink path (currently IP-routing-only, via
`ipv4`/`at`/`tn`) to also recognize and re-tunnel Ethernet-tagged post-decapsulation
packets — not implemented in this scope, alongside 802.1Qbv/TAS and DS-TT/NW-TT (see
the final report).

**Replaces:** the reference shim's `TsnRadioIngress` module (Section 9.10/10.5 of the
reference report), which performed the MAC→`nrMacNodeId` mapping and
`FlowControlInfo` construction itself, entirely outside the GTP-U/UPF path. Here, the
mapping already happened at the UPF (`TrafficFlowFilter`), and `GtpUser`/PDCP (next
doc entry) only need to carry that already-resolved identity (`teid`) through the
tunnel and radio stack.
