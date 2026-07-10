# Documentation entry: LtePdcpRrcBase protocol-tag guards

## File: `src/stack/pdcp_rrc/layer/LtePdcpRrc.cc`
**Baseline (confirmed by direct read):** `headerCompress()` unconditionally (when
`isCompressionEnabled()`) called `pkt->removeAtFront<Ipv4Header>()`; `headerDecompress()`
symmetrically always expected `LteRohcPdu` + `Ipv4Header` at the front; `toDataPort()`
unconditionally executed `pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ipv4)`.
None of the three had any branch on packet content/type at all.

**Modified/new code (three guards, one per method):**
```cpp
if (pkt->getTag<PacketProtocolTag>()->getProtocol() == &Protocol::ethernetMac)
    return; // headerCompress() / headerDecompress()

if (pkt->getTag<PacketProtocolTag>()->getProtocol() != &Protocol::ethernetMac)
    pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ipv4); // toDataPort()
```

**Why (statement-by-statement, spec/architectural justification):**
- `headerCompress()`/`headerDecompress()` early-return for `ethernetMac`: ROHC (RFC
  3095, as implemented here in simplified form) is meaningless for a non-IP payload —
  there is no IP/TCP/UDP header to compress, and running `removeAtFront<Ipv4Header>()`
  on a packet that actually starts with `EthernetMacAddressFields` would misparse the
  real Ethernet frame bytes as an IP header (silent corruption, not a clean failure).
  This is the direct fix for the reference report's own Section 8 wish-list item
  ("skipping IP-specific compression/decompression") — but done by checking the
  packet's *real, already-existing* protocol tag, not the reference shim's
  `findTag<TsnEtherTag>() != nullptr` check (Section 9.16 of the reference report),
  which worked by the *absence* of an ad hoc metadata tag rather than by the packet's
  actual declared protocol.
- `toDataPort()`'s guard: only forces the `ipv4` tag when the packet is not already
  `ethernetMac` — an Ethernet PDU session packet must keep the tag set upstream (by
  `GtpUser`/`IP2Nic`, see their doc entries) all the way to `IP2Nic::toIpUe()` at the
  UE, which needs to read it to decide whether to deliver to `ipv4` or to the UE's
  `l2App[]` slot. Forcing `ipv4` here unconditionally (the baseline behavior) would
  have silently mis-tagged every Ethernet PDU session packet as IP right before it
  left PDCP, breaking that downstream branch.

**Why this is the correct place for the guard (not somewhere else):** these three
methods are the *only* places in the pre-existing PDCP code that make IP-specific
assumptions about packet content; everything else in `LtePdcpRrcBase`/`NRPdcpRrcEnb`
(LCID/CID allocation, `ConnectionsTable`, `getTxEntity`/`getRxEntity`) is generic and
untouched, since none of it inspects packet content — it only reads `FlowControlInfo`
fields, which this project already populates correctly for both session types (see
`IP2Nic`'s doc entry).

**Replaces:** the reference shim's `LtePdcpRrcBase::handleMessage`/`toDataPort`
modifications (Section 9.16 of the reference report), which added an `#include
"Level2App/TsnEtherTag_m.h"` and branched on tag presence to route packets to a
brand-new `tsnDataIn`/`tsnDataOut` gate pair added to `LtePdcpRrc.ned` (Section 9.14).
Here, no new gates are added to PDCP at all — the existing `DataPort` gate is reused
for both session types, since the actual difference between them (ROHC on/off,
protocol tag value) is fully expressed by the guards above.
