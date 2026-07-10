# 23 — The fix that worked: tag as ieee8022llc to bypass the ethertype lookup

## Why the entry-22 registration approach failed at runtime

Entry 22 registered `Protocol::ethernetMac` in INET's ethertype registry
(`ProtocolGroup::getEthertypeProtocolGroup()->addProtocol(...)`) at
`INITSTAGE_LOCAL`, and set `PacketProtocolTag = ethernetMac` on the frame.
On a verified-current release build (`libsimu5g.dll` timestamp newer than the
source, registration confirmed present via grep), encap STILL threw:

```
Unknown protocol: id = 24, name = ethernetmac -- in module
(inet::EthernetEncapsulation) SingleCell_Standalone.server.tsnEncap
```

Reading `ProtocolGroup::getEthertypeProtocolGroup()` (ProtocolGroup.cc:167)
shows the group is not a plain static but a **simulation-shared variable**:

```cpp
static int handle = cSimulationOrSharedDataManager::registerSharedVariableName("inet::ProtocolGroup::ethertype");
return &getSimulationOrSharedDataManager()->getSharedVariable<ProtocolGroup>(handle, "ethertype", ethertypeProtocols);
```

In principle this mechanism exists precisely to share such state across
module code and DLL boundaries. In practice, the `addProtocol()` call made
from `TsnEtherApp` (in `libsimu5g.dll`) was not visible to the
`getProtocolNumber()` call made from `EthernetEncapsulation` (in the INET
library) at the point the frame was encapsulated -- the registration simply
did not take effect where it needed to. Rather than keep trying to force a
cross-module registration to stick (an interaction not observable without a
live debugger on the user's machine), the robust move is to remove the
dependency on that registry altogether.

## The fix

`EthernetEncapsulation::processPacketFromHigherLayer()` only consults the
ethertype registry when the packet's protocol is non-null AND not
`ieee8022llc` (EthernetEncapsulation.cc:172):

```cpp
if (protocol && *protocol != Protocol::ieee8022llc)
    typeOrLength = getEthertypeProtocolGroup()->getProtocolNumber(protocol); // can throw
else
    typeOrLength = packet->getByteLength();                                  // never throws
```

So tagging the frame `Protocol::ieee8022llc` (instead of `ethernetMac`):
- satisfies encap's hard requirement that the protocol be non-null
  (`getProtocol()` throws on null),
- takes the `else` branch -> `typeOrLength = packet->getByteLength()`, a
  valid IEEE 802.3 length field (our data part is < 1536 B), and
- never calls `getProtocolNumber()`, so "Unknown protocol" is structurally
  impossible regardless of the shared-variable behavior above.

Encap then sets `PacketProtocolTag = Protocol::ethernetMac` itself
(EthernetEncapsulation.cc:191) before sending to the MAC, so the switch, the
UPF's `TrafficFlowFilter`, `GtpUser`, and PDCP all still see the real
`ethernetMac` protocol tag downstream -- nothing about the end-to-end
protocol semantics changes; only the transient tag encap reads on ingress
differs.

**File:** `src/apps/l2/TsnEtherApp.cc`
- `initialize()`: removed the `ProtocolGroup` registration block entirely
  (and the `<inet/common/ProtocolGroup.h>` include); added
  `<inet/common/Protocol.h>` for `Protocol::ieee8022llc`.
- `buildFrame()`: `PacketProtocolTag` set to `Protocol::ieee8022llc` instead
  of `Protocol::ethernetMac`.

No other files change. This supersedes the ethertype-registration parts of
entries 20 and 22 (the direct-wired `tsnEncap: EthernetEncapsulation`
topology from those entries is unchanged and correct).

## Trade-off note (for the report)

The server-built outer Ethernet header now carries an 802.3 *length* in its
type/length field rather than a specific EtherType value. This is fully valid
Ethernet framing and irrelevant to forwarding here (the switch forwards by
destination MAC; the UPF pops the outer header without inspecting
type/length). The genuine 802.1Q VLAN tag with real PCP/VID/EtherType is
carried in the *inner* frame (`Ieee8021qTagEpdHeader`), which is what is
tunneled end-to-end to the UE and popped by the UE's `TsnEtherApp` -- so the
"real, byte-serialized 802.1Q frame" property the project requires is
unaffected.
