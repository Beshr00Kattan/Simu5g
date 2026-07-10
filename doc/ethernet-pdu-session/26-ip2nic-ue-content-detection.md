# 26 — UE IP2Nic: detect the Ethernet frame by content, not the (lost) tag

## Symptom (next error after entry 25's fix)

The frame traversed the entire radio stack and reached the UE's IP2Nic, which threw:

```
Cannot convert chunk from type inet::EthernetMacAddressFields to type
inet::Ipv4Header ... in module (IP2Nic)
SingleCell_Standalone.ue[0].cellularNic.ip2nic, at t=0.015s, event #120
```

## Cause

`IP2Nic::toIpUe()` decides between "deliver to l2App (Ethernet)" and "peek an
Ipv4Header (IP)" using the packet's `PacketProtocolTag`. That tag is set to
`ethernetMac` upstream (by the server's encap, restored by GtpUser at the gNB,
preserved by `LtePdcpRrcBase::toDataPort()` when present). But it does **not
survive the UE-side RLC fragment/reassemble round-trip**: by the time the
reassembled SDU reaches the UE's PDCP `toDataPort()`, the tag is gone, so
`toDataPort()` force-tags it `ipv4` (its default), and `toIpUe()`'s
`ethernetMac` check fails -> it falls through to `peekAtFront<Ipv4Header>()` on
a packet whose front chunk is really our `EthernetMacAddressFields` -> throw.

The packet's **content** is fully intact end-to-end (the frame really is
`[EthernetMacAddressFields][Ieee8021qTagEpdHeader][payload]`); only the
out-of-band tag was lost.

## Fix

**File:** `src/stack/ip2nic/IP2Nic.cc`, `toIpUe()`

Detect the Ethernet frame by inspecting the actual chunk at the front, using
the non-throwing `hasAtFront<>()`:

```cpp
auto protocolTag = pkt->findTag<PacketProtocolTag>();
bool taggedEthernet = protocolTag != nullptr && protocolTag->getProtocol() == &Protocol::ethernetMac;
if (taggedEthernet || pkt->hasAtFront<EthernetMacAddressFields>())
{
    prepareForIpv4(pkt, &Protocol::ethernetMac);
    send(pkt, ipGateOut_);
    return;
}
auto ipHeader = pkt->peekAtFront<Ipv4Header>();   // real IP path, unchanged
```

`hasAtFront<EthernetMacAddressFields>()` is a boolean test that never throws,
so it is safe to evaluate before the IPv4 peek. The tag is still honored as a
fast path when present. This is content-based dispatch -- the most robust
discriminator available at this point, independent of whether any out-of-band
tag survived the lower-layer round-trip.

## Why not "just keep the tag alive through RLC"

RLC fragments a PDCP SDU into PDUs and reassembles them; carrying arbitrary
INET packet tags faithfully across that boundary is not something this scope
modifies (it would touch the generic LTE/NR RLC reassembly for all traffic).
Detecting by content at the one delivery point that needs it is smaller,
safer, and regression-free for IP traffic (an IP packet has an `Ipv4Header`,
not `EthernetMacAddressFields`, at its front, so it never takes this branch).
