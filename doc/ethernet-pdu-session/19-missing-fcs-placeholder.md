# 19 — TsnEtherApp must append a placeholder EthernetFcs itself

## Symptom

```
Cannot convert chunk from type inet::ByteCountChunk to type inet::EthernetFcs.
... in module (inet::EthernetMac) SingleCell_Standalone.server.tsnEth.mac
```

Progress marker: this is past the classifier fix (entry 18) — the frame now
reaches the real MAC correctly. The error is inside INET's own
`EthernetMac::handleUpperPacket()` → `EthernetMacBase::addPaddingAndSetFcs()`.

## Root cause

`EthernetMacBase::addPaddingAndSetFcs()` (`EthernetMacBase.cc:673`) does:

```cpp
auto ethFcs = packet->removeAtBack<EthernetFcs>(ETHER_FCS_BYTES);
ethFcs->setFcsMode(fcsMode);
... ethFcs->setFcs(...);
```

It **removes an existing placeholder `EthernetFcs` chunk and fills in its
real value** — it does not append one itself. Normally
`inet::EthernetEncapsulation::processPacketFromHigherLayer()` appends this
placeholder on every packet's behalf as part of building the outer frame.
Since the server's `TsnEtherApp` is now direct-wired to `tsnEth`, bypassing
encap entirely (see entry 16), nothing was appending that placeholder — the
packet's last chunk was still the `ByteCountChunk` payload, hence the
conversion error.

## Fix

**File:** `src/apps/l2/TsnEtherApp.cc`, `buildFrame()`

Added `pkt->insertAtBack(makeShared<EthernetFcs>());` immediately after
inserting the real outer `EthernetMacHeader`, mirroring exactly what
`EthernetEncapsulation` would have done. `EthernetMac`'s send path fills in
the real FCS value (or padding) into this placeholder before transmission;
`TrafficFlowFilter::handleEthernetPacket()` (unchanged since entry 16) still
pops this same trailing `EthernetFcs` chunk on the UPF side, since it is a
physical-layer artifact of the one `tsnEth<->tsnSwitch<->tsnEth` wire hop and
is not part of the logical frame tunneled onward to the UE.
