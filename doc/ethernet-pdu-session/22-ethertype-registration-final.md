# 22 — Final, correct ethertype handling + a debug/release build-mismatch diagnosis

## Supersedes entry 21

Entry 21 ("drop the ethertype registration, remove the PacketProtocolTag")
was **wrong** and is reverted. This entry documents the correct final state
and — more importantly — the actual reason the same error survived every
rebuild.

## Why entry 21 was wrong

`ProtocolTag.msg` defines `PacketProtocolTag::getProtocol()` with a custom
body that **throws** on a null protocol:

```cpp
const Protocol *getProtocol() const {
    if (protocol == nullptr)
        throw cRuntimeError("Protocol is not specified");
    return protocol;
}
```

`EthernetEncapsulation::processPacketFromHigherLayer()` (EthernetEncapsulation.cc:170-173)
calls exactly this on every outgoing frame:

```cpp
const auto& protocolTag = packet->addTagIfAbsent<PacketProtocolTag>();
const Protocol *protocol = protocolTag->getProtocol();               // throws if null
if (protocol && *protocol != Protocol::ieee8022llc)
    typeOrLength = getEthertypeProtocolGroup()->getProtocolNumber(protocol); // throws if unregistered
```

So encap mandates BOTH: a present `PacketProtocolTag` AND a protocol that is
registered in the ethertype group. Entry 21 removed the tag, which would have
produced a *different* throw ("Protocol is not specified") — not a fix at all.

## The real reason the error never changed: debug/release build mismatch

The decisive observation came from comparing build timestamps:

| Artifact                       | Built            |
|--------------------------------|------------------|
| `src/libsimu5g.dll` (release)  | today            |
| `src/libsimu5g_dbg.dll` (debug)| **the day before** |

The OMNeT++ IDE runs the **debug** build by default in Qtenv. The debug
library was a full day stale — it still contained the original `buildFrame()`
that set `PacketProtocolTag = ethernetMac` without registration. Every "fix +
rebuild" cycle was rebuilding *release* while the simulation kept loading the
old *debug* DLL. The NED rename to `tsnEncap` appeared to take effect only
because NED is parsed at runtime independent of which compiled library is
loaded — which masked the fact that the C++ was never being re-run.

**Lesson for the build workflow:** the compiled mode that is *run* (debug in
the IDE by default) must be the mode that is *rebuilt* after any `.cc`/`.h`
change. A green "Build" in one mode does not update the other.

## Final correct code

**File:** `src/apps/l2/TsnEtherApp.cc`
- `initialize()` at `INITSTAGE_LOCAL`: registers `Protocol::ethernetMac` in
  `ProtocolGroup::getEthertypeProtocolGroup()` mapped to `par("etherType")`
  (idempotent, guarded by `findProtocolNumber(...) == -1`). Safe against init
  order: all `initialize()` stages of all modules complete before any `t>0`
  event, and the first frame is sent at `startTime_ > 0`.
- `buildFrame()`: sets `PacketProtocolTag = Protocol::ethernetMac` (encap
  resolves it to the registered ethertype). Everything else
  (`EthernetMacAddressFields` + `Ieee8021qTagEpdHeader` + payload,
  `MacAddressReq`, `PcpReq`) unchanged from entry 20.

No changes to `EthernetPduSessionHost.ned` (still the direct-wired
`tsnEncap: EthernetEncapsulation` from entry 20/rename), `TrafficFlowFilter.cc`
(still pops outer header + FCS), or `TsnPcpClassifier.cc` (still reads PcpReq).
