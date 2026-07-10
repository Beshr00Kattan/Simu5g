# 25 — gNB PDCP: getDestId must not IP-resolve an Ethernet packet

## Symptom (the next error after entry 24's fix)

With `TsnEtherApp` finally executing, the frame reached the gNB's PDCP and threw:

```
NRTxPdcpEntity::deliverPdcpPdu - the destination is not a UE but Dual
Connectivity is not enabled. -- in module (NRPdcpRrcEnb)
SingleCell_Standalone.gnb.cellularNic.pdcpRrc, at t=0.01s, event #82
```

## Cause

`NRTxPdcpEntity::deliverPdcpPdu()` (NRTxPdcpEntity.cc:37-39) reads
`lteInfo->getDestId()` and throws if it is not a UE. The destId had been
resolved correctly upstream (from the real destination MAC, carried in
`FlowControlInfo.destId`), and `NRPdcpRrcEnb::fromDataPort()` even has an
explicit Ethernet branch that reads it correctly. But the PDCP tx entity's
`setIds()` (NRTxPdcpEntity.cc:78) then does:

```cpp
lteInfo->setDestId(pdcp_->getDestId(lteInfo));
```

overwriting the good destId with `NRPdcpRrcEnb::getDestId()`, which was purely
IP-based:

```cpp
destId = binder_->getNrMacNodeId(Ipv4Address(lteInfo->getDstAddr()));
```

An Ethernet PDU session packet has no IP, so `dstAddr == 0`,
`getNrMacNodeId(0.0.0.0)` returns a non-UE id, and `deliverPdcpPdu` throws.

## Fix

**File:** `src/stack/pdcp_rrc/layer/NRPdcpRrcEnb.cc`, `getDestId()`

Added an early return for the no-IP (Ethernet) case, preserving the destId
already resolved from the MAC:

```cpp
if (lteInfo->getDstAddr() == 0)
    return lteInfo->getDestId();
```

`dstAddr == 0` is a reliable discriminator here: every IP PDU session packet
carries a real (non-zero) destination IP, while an Ethernet PDU session packet
never sets one. `getDestId()` and `setIds()` both only receive the
`FlowControlInfo` (not the packet), so this lteInfo-only check is the minimal
correct signal available without adding a new FlowControlInfo field. With it,
`setDestId(getDestId(...))` becomes an identity for Ethernet packets and the
correct UE destId (set by `IP2Nic::toStackBsEthernet()`) survives to
`deliverPdcpPdu`, which then recognizes the destination as a UE and forwards
to the lower layer.
