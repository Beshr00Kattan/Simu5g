> **SUPERSEDED / WRONG — see entry 22.** The conclusion below (remove the
> PacketProtocolTag and rely on an encap fallback) is incorrect:
> `EthernetEncapsulation` calls `PacketProtocolTag::getProtocol()`, which
> *throws* on a null protocol, so the tag is mandatory. The error also never
> actually changed because the simulation was running a day-stale **debug**
> library while only the **release** build was being rebuilt. Entry 22 has the
> correct fix and the full diagnosis. This file is kept only for history.

# 21 — Drop the ProtocolGroup ethertype-registration workaround entirely

## Symptom

After fixing the NED name collision (entry 20's `ethernet` -> `tsnEncap`
rename), on a confirmed fresh rebuild:

```
Unknown protocol: id = 24, name = ethernetmac -- in module
(inet::EthernetEncapsulation) SingleCell_Standalone.server.tsnEncap
(id=34), at t=0.01s, event #51
```

## Root cause

Re-reading `EthernetEncapsulation::processPacketFromHigherLayer()`
end-to-end (not just the fragment relevant to header/FCS construction, as in
entry 19/20):

```cpp
const auto& protocolTag = packet->addTagIfAbsent<PacketProtocolTag>();
const Protocol *protocol = protocolTag->getProtocol();
if (protocol && *protocol != Protocol::ieee8022llc)
    typeOrLength = ProtocolGroup::getEthertypeProtocolGroup()->getProtocolNumber(protocol);
else
    typeOrLength = packet->getByteLength();
...
packet->insertAtFront(ethHeader);
...
protocolTag->setProtocol(&Protocol::ethernetMac);   // encap sets this itself, on the way OUT
```

`TsnEtherApp::buildFrame()` was setting `PacketProtocolTag = Protocol::ethernetMac`
**before** handing the packet to encap. That forced encap into the
`getProtocolNumber(protocol)` branch, looking up "what real EtherType number
represents the protocol `ethernetMac`" — a lookup that only succeeds if
something has registered `Protocol::ethernetMac` in the global ethertype
registry, which is what entries 16/20 tried to do by calling
`ProtocolGroup::addProtocol()` at module init. That registration is
mechanically correct in isolation, but `Protocol::ethernetMac` is Simu5G's
own internal dispatch/meta marker (used throughout `TrafficFlowFilter`,
`GtpUser`, PDCP to mean "this is real Ethernet traffic") — it was never a
sensible entry for "real payload protocol -> real wire EtherType number" in
the first place, and depending on a global, order-sensitive registration
happening at the right init stage is inherently more fragile than avoiding
the lookup altogether.

The key realization: encap does not need to be told the protocol going in —
**it sets `PacketProtocolTag = Protocol::ethernetMac` itself**, as its very
last step before sending to the lower layer. Anything downstream
(`TrafficFlowFilter`, `GtpUser`, PDCP) only ever inspects the packet *after*
it has passed through encap, so it makes no difference whether the tag was
set by `TsnEtherApp` beforehand or by encap itself afterward.

## Fix

**File:** `src/apps/l2/TsnEtherApp.cc`
- `buildFrame()`: removed the `pkt->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac)`
  call entirely.
- `initialize()`: removed the `ProtocolGroup::getEthertypeProtocolGroup()->addProtocol(...)`
  registration block (no longer needed — the lookup it was working around is
  never triggered now).

With no `PacketProtocolTag` set going in, `protocolTag->getProtocol()`
returns `nullptr`, so encap takes its own built-in fallback
(`typeOrLength = packet->getByteLength()`, a valid, real IEEE 802.3
length-field encoding) — no global registry involved, no dependency on
initialization order across modules, nothing that can throw regardless of
build/init state. This is a strictly more robust fix than entry 20's
registration approach, not just a different way of reaching the same
outcome.
