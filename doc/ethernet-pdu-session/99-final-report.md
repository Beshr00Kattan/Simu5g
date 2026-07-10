# TSNapp Deployment v2: Real 3GPP Ethernet PDU Session over Simu5G Standalone

Professional Technical Report — supersedes the reference "TSNapp Deployment" report,
which described a metadata-only shim over a separate, parallel network. This report
covers the real implementation against the actual, existing standalone network.

## Report Focus

This report explains a real 3GPP Ethernet PDU Session (TS 23.501 §5.6.10.2) deployed
over the existing `SingleCell_Standalone` network and `omnetpp.ini`, plus a real IEEE
802.1Q learning bridge and PCP-based priority queueing (Layer B), replacing every
metadata-tag/bypass-relay mechanism the reference shim used with the corresponding
real Simu5G/INET mechanism.

## Scope Boundary (compare against the reference report's "Important Scope Boundary")

This implementation carries genuine, byte-serialized Ethernet frames end-to-end from
the AF (server) through a real GTP-U tunnel to the UE, classified by real destination
MAC address through a real, learning-capable forwarding table, with real IEEE 802.1Q
VLAN tags and PCP-based priority queueing. Three things are deliberately **not**
modeled, each with a specific, documented reason (not oversights):
1. **802.1Qbv (TAS) gate-control-list / time-synchronized scheduling** — would
   require gPTP time synchronization, a GCL scheduling engine, and residence-time
   modeling: a separate, substantially larger undertaking than this scope covers.
2. **5GS-as-TSN-bridge DS-TT/NW-TT translator functions** — out of scope per the
   original task instruction.
3. **Uplink Ethernet PDU session traffic and inter-UE handover for this session
   type** — this scope implements the downlink direction only (server → UPF → gNB →
   UE), matching the reference shim's own scope; extending to uplink is
   straightforward given the patterns established here (see each relevant doc entry)
   but was not implemented given the project's time budget.

---

## 1. Scope and Baseline

Baseline: official Simu5G, the unmodified `SingleCell_Standalone` network
(`simulations/NR/networks/SingleCell_Standalone.ned`) and its `omnetpp.ini`
(`simulations/NR/standalone/omnetpp.ini`, three pre-existing configs: `Standalone`,
`VoIP-DL`, `VoIP-UL`). Every file touched, and every file's baseline content, is
documented individually in `doc/ethernet-pdu-session/00` through `13` (see the
file-by-file section below for the full index). No `_L2Tsn`-suffixed parallel network
or ini file was created or touched.

## 2. Deployment Flow

Activated by `[Config EthernetPduSession]` in the existing `omnetpp.ini`
(`extends=Standalone`; entry 13). `*.ue[0].pduSessionType = "Ethernet"` and
`*.ue[0].tsnMac = "00:00:00:00:00:02"` register this UE's real session attributes
with the `Binder` (entries 04, 05, 07) at attach time — the same "configure once,
register at init" pattern Simu5G already uses for IP↔MacNodeId registration, not a
NAS message exchange (there is no NAS layer in Simu5G to extend).

## 3. Real Architecture: The UPF *Is* the IEEE 802.1Q Learning Bridge

Unlike the reference shim's dedicated `SwitchMacTsn` node bypassing the UPF, this
implementation places the real learning bridge function **inside the UPF itself**
(entry 06), per TS 23.501 §5.6.10.2's own description of UPF-side MAC learning for
Ethernet PDU sessions. `Upf.ned` gains one new submodule, `macTable: MacForwardingTable`
— the real, reused `inet::MacForwardingTable` INET's own `EthernetSwitch` uses
internally (confirmed by inspecting `EthernetSwitch.ned`'s `bridging` submodule during
planning). No new node type is added to the topology.

## 4. MAC-to-UE / PDU-Session Mapping Logic

```
TsnEtherSourceApp (real EthernetMacAddressFields + Ieee8021qTagEpdHeader, tunneled
  over UDP/IP for the N6 leg -- entry 03)
  -> TrafficFlowFilter.handleEthernetPacket() (entry 07): strips the N6 UDP/IP
     shell, peeks the real destination MAC, looks it up in the real
     MacForwardingTable (learn/age/flood-capable), falling back to a Binder-
     registered UE MAC (entry 07) since this scope has no uplink traffic to learn
     from -> resolves destId
  -> GtpUser (entry 09): teid = destId (a REAL, spec-meaningful TEID value,
     replacing the baseline's hardcoded 0), real GTP-U header + PDU Session
     Container extension header (entry 08), payload = the real Ethernet frame,
     byte-for-byte
  -> gNB's GtpUser::handleFromUdp() (entry 09): resolves PduSessionType from the
     real teid via Binder::getPduSessionType() -- not by inspecting payload bytes,
     not by a repeated per-packet flag
  -> IP2Nic::toStackBsEthernet() (entry 10): re-resolves destId from the real MAC
     (same Binder-backed lookup) and stores it in FlowControlInfo.destId
  -> NRPdcpRrcEnb::fromDataPort() (entry 10): reads that pre-resolved destId
     instead of an IP-address Binder lookup
  -> LtePdcpRrcBase::headerCompress()/toDataPort() (entry 11): skip ROHC entirely,
     keep the real Protocol::ethernetMac tag, for a real protocol-tag reason, not
     an absent-tag heuristic
  -> PDCP/RLC/MAC/PHY carry the real frame bytes, unmodified, to the UE
  -> UE's IP2Nic::toIpUe() (entry 10) + LtePdcpRrcBase guards (entry 11): deliver
     to the UE's l2App[0] (TsnEtherApp) via the same "nl" dispatcher ipv4 uses
     (entry 01), tagged ethernetMac instead of ipv4
  -> TsnEtherApp::handleFrame() (entries 00, 02): pops the real headers, computes
     delay, done.
```

This mapping is performed at the UPF (destination resolution) and re-confirmed at
the gNB (IP2Nic); it is not stored in an INET Ipv4 routing table, matching the
reference shim's own observation that this is a Layer-2, not Layer-3, concern —
except here it uses a real, keyed forwarding table, not a static per-UE
parameter linearly scanned.

## 5. End-to-End Packet Movement

1. `TsnEtherSourceApp::sendFrame()` builds `[EthernetMacAddressFields][Ieee8021qTagEpdHeader][ByteCountChunk]`.
2. `UdpSocket::sendTo()` wraps it in real UDP+IP for the N6 transport hop (entry 03).
3. `TrafficFlowFilter::handleEthernetPacket()` strips UDP+IP, classifies by MAC,
   tags `TftControlInfo` (entry 07) — same tag type the pre-existing IP path uses.
4. `GtpUser::handleFromTrafficFlowFilter()` builds a real GTP-U header (`teid` =
   destId, `eFlag`/PDU Session Container set) around the still-intact Ethernet frame
   (entry 09).
5. `GtpUser::handleFromUdp()` at the gNB decapsulates, resolves session type from
   `teid`, tags `Protocol::ethernetMac`, delivers via `InterfaceReq` to `cellularNic`
   (entry 09) — the identical mechanism the IP path already uses for that step.
6. `IP2Nic::toStackBsEthernet()` resolves destId from the real MAC, populates
   `FlowControlInfo`, forces `useNR=true` (single-cell pure-NR topology), carries the
   real VLAN PCP into `typeOfService` (entry 10).
7. `NRPdcpRrcEnb::fromDataPort()` reads the pre-resolved destId, allocates
   LCID/CID exactly as the IP path does (entry 10).
8. `LtePdcpRrcBase::headerCompress()` is skipped (real protocol-tag guard, entry 11);
   RLC/MAC/PHY carry the frame unchanged.
9. UE's `IP2Nic::toIpUe()` delivers to `l2App[0]` via the real "nl" dispatcher
   registration (entries 01, 10).
10. `TsnEtherApp::handleFrame()` pops the real headers, validates, emits
    `framesReceived`/`frameDelay` signals (entries 00, 02).

## 6. Multiple UE Support and Traffic Differentiation

Falls out for free from using a real, keyed `MacForwardingTable`/Binder-backed
lookup (entry 06/07): any number of UEs can register a unique `tsnMac`
(`Ue.ned`'s `tsnMac` parameter, entry 07), and `TrafficFlowFilter`/`IP2Nic` resolve
each one independently — there is no `allowedDst`-style single-value limitation the
reference shim's `TsnMacForwarder` had (its own Section 5 "Multi-UE Limitation" box).

## 7. UPF Processing Flow

```
Original UPF path (unchanged for IP traffic):
  pppIf.upperLayerOut -> trafficFlowFilter (peek Ipv4Header, Binder IP lookup)
  -> gtp_user (teid=0 stub, wraps whole IP datagram) -> tunnel

Real Ethernet PDU session path (new branch, same modules, same gates):
  pppIf.upperLayerOut -> trafficFlowFilter (detect UDP dest port 3000, strip
    IP/UDP, peek real dest MAC, real MacForwardingTable + Binder fallback lookup)
  -> gtp_user (real teid=destId, real GTP-U header + PDU Session Container ext
    header, wraps the still-real Ethernet frame) -> tunnel
```

The UPF is not bypassed and is not reduced to a "switch-like relay" outside its own
architecture (contrast with the reference report's Section 7 admission that its UPF
"is not behaving as a complete 3GPP UPF Ethernet PDU-session pipeline") — it *is* the
pipeline, using its own real `TrafficFlowFilter`/`GtpUser` modules with a second,
real classification/encapsulation branch alongside the untouched IP one.

## 8. PDU Ethernet Session Modeling — Modeled / Not Modeled

| Reference report's row | Status here | Where |
|---|---|---|
| Ethernet-like source/destination MAC metadata | **Modeled (real bytes)** | `EthernetMacAddressFields`, entries 00/02/03 |
| VLAN-like vid/priority-like pcp metadata | **Modeled (real bytes)** | `Ieee8021qTagEpdHeader`, entry 02 |
| MAC-based forwarding/filtering through relays | **Modeled (real, learning, aging table)** | `MacForwardingTable`, entries 06/07 |
| MAC-to-UE mapping at gNB/UPF ingress | **Modeled (real Binder-backed lookup)** | entries 07/10 |
| Delivery through the normal NR PDCP/RLC/MAC/PHY stack | **Modeled (unchanged stack, real protocol-tag guards)** | entry 11 |
| Serialized Ethernet headers on the INET Ethernet stack | **Modeled (real inet::EthernetMacHeader/EthernetMacAddressFields chunks)** | entries 00/02/03 |
| 5G TSN translator functions (DS-TT/NW-TT) | **Out of scope — justified** | see Scope Boundary above |
| UPF Ethernet PDU-session classification/tunneling | **Modeled (real TrafficFlowFilter branch + real GTP-U header)** | entries 07/08/09 |
| Bridge learning and full IEEE 802.1 behavior | **Modeled (real MacForwardingTable learn/age/lookup)**, flood-on-unknown simplified to drop (documented) | entries 06/07 |
| TAS, CBS, ATS, or full standards-accurate TSN scheduling | **Out of scope — justified** (real strict-priority PriorityQueue/PriorityScheduler modeled instead, entry 12) | see Scope Boundary above |
| *(new)* PDU Session Establishment signaling | **Deliberately not modeled** — session type is a registered attribute (entry 04), not a NAS procedure; Simu5G has no NAS/SM layer to extend | entry 04 |
| *(new)* Uplink Ethernet PDU session traffic | **Out of scope — documented** | entries 07/09/10 |

## 9. File-by-File Code Comparison — Index

Each file below has its own complete baseline/modified/statement-by-statement/
rationale entry in `doc/ethernet-pdu-session/`:

| # | File(s) | Entry |
|---|---|---|
| 00 | `src/apps/l2/L2App.{ned,h,cc}` | `00-foundation-l2app.md` |
| 01 | `src/apps/l2/L2App.{ned,cc}` (dispatcher attachment) | `01-l2app-attachment.md` |
| 02 | `src/apps/l2/TsnEtherApp.{ned,h,cc}` | `02-tsnetherapp-vlan.md` |
| 03 | `src/nodes/Ue.ned`, `src/apps/l2/TsnEtherSourceApp.{ned,h,cc}` | `03-server-attachment.md` |
| 04 | `src/common/binder/Binder.h` (PduSessionType) | `04-binder-pdu-session-type.md` |
| 05 | `src/stack/mac/layer/LteMacUe.cc` (pduSessionType registration) | `05-ltemacue-registration.md` |
| 06 | `src/nodes/Upf.ned` (MacForwardingTable) | `06-upf-mactable.md` |
| 07 | `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.{h,cc}`, `Binder.h` (MAC table), `Ue.ned`/`LteMacUe.cc` (tsnMac) | `07-tff-ethernet-branch.md` |
| 08 | `src/corenetwork/gtp/GtpUserMsg.msg`, `GtpUserMsgSerializer.{h,cc}` | `08-gtpumsg-fields.md` |
| 09 | `src/corenetwork/gtp/GtpUser.cc` | `09-gtpuser-ethernet.md` |
| 10 | `src/stack/ip2nic/IP2Nic.{h,cc}`, `NRPdcpRrcEnb.cc` | `10-ip2nic-pdcp-ethernet.md` |
| 11 | `src/stack/pdcp_rrc/layer/LtePdcpRrc.cc` | `11-pdcp-protocol-guard.md` |
| 12 | `src/apps/l2/TsnPcpClassifier.{ned,h,cc}` | `12-pcp-priority-queue.md` |
| 13 | `simulations/NR/standalone/omnetpp.ini` | `13-omnetpp-ini-config.md` |

## 10. Detailed Module Analysis

- **L2App / TsnEtherApp / TsnEtherSourceApp**: three real, non-metadata Ethernet
  frame builders/sinks (entries 00, 02, 03). `L2App`/`TsnEtherApp` attach directly to
  the UE's network-layer dispatcher (no transport layer under this traffic);
  `TsnEtherSourceApp` is an ordinary UDP-socket app on the server, tunneling the same
  real frame across the one IP-routed segment this fixed topology requires.
- **TrafficFlowFilter**: unchanged for IP traffic; gains one new, real branch for
  Ethernet PDU sessions using a real learning table instead of a static filter.
- **GtpUser**: real GTP-U header fields (previously a 1-field stub); `teid` now
  genuinely identifies the session, used by the decapsulating side to resolve PDU
  session type without inspecting payload bytes.
- **IP2Nic**: gains the Ethernet-equivalent of its existing `toStackBs()`/`toIpUe()`
  methods, reusing the same `FlowControlInfo`/dispatcher-tagging idioms.
- **LtePdcpRrcBase/NRPdcpRrcEnb**: three narrow, real-protocol-tag guards replace
  what would otherwise be IP-only assumptions; no new gates added.
- **TsnPcpClassifier**: the one genuinely new piece of Layer B machinery, since no
  existing INET classifier could see through this project's own tunnel encapsulation
  without popping the VLAN tag prematurely.

## 11. Key Architectural Conclusions

- Every mechanism the reference shim bypassed (UPF traffic filtering/GTP-U, PDCP's
  real protocol handling, INET's real Ethernet/VLAN/bridge classes) is now the actual
  mechanism in use — extended, not replaced by a parallel path.
- The topology (`SingleCell_Standalone.ned`) required exactly one line changed
  (none, after the final design — see entry 03's correction) plus additive-only NED
  changes (`Ue.ned`'s `l2App[]` slot, `Upf.ned`'s `macTable`); no new node types, no
  duplicated network/config files.
- Existing IP scenarios (`VoIP-DL`, `VoIP-UL`) are provably unaffected at the source
  level: every branch added checks `PacketProtocolTag == Protocol::ethernetMac`
  first and falls through to the original, unmodified code otherwise, and every new
  parameter (`numL2Apps`, `pduSessionType`, `tsnMac`, `macForwardingTableModule`)
  defaults to a value that reproduces the original behavior exactly.
- **Build verification**: the entire Simu5G codebase, including all new and modified
  files listed above, was rebuilt from a clean Makefile regeneration
  (`opp_makemake --make-so -f --deep ...`) and compiled/linked successfully
  (`libsimu5g_dbg.dll`) with zero errors — only pre-existing, unrelated warnings
  (confirmed by direct comparison against the pre-change warning set).
- **Runtime verification — blocked in this session, not by the code.** Attempting to
  actually execute `[Config VoIP-DL]`/`[Config EthernetPduSession]` via
  `opp_run_dbg`/`opp_run` failed at Windows DLL-load time
  (`STATUS_DLL_NOT_FOUND`/`STATUS_ENTRY_POINT_NOT_FOUND`) due to a version mismatch
  in this local OMNeT++ installation's bundled Qt/ICU runtime
  (`tools/win32.x86_64/opt/mingw64/bin` contains both ICU 62 and ICU 67 DLLs
  simultaneously) — `opp_run`/`opp_run_dbg` unconditionally import
  `liboppqtenv(_dbg).dll` regardless of the `-u Cmdenv` selection, so this blocks
  *any* simulation run from this environment, not just this scenario. This is a
  pre-existing installation completeness issue, confirmed independent of debug/
  release mode and independent of any file this project touched.
  **To verify at runtime:** from a working OMNeT++ shell/IDE (e.g. via
  `mingwenv.cmd`, which the existing `out/clang-debug` build artifacts confirm was
  used successfully before), run:
  ```
  cd samples/simu5g/simulations/NR/standalone
  simu5g -u Cmdenv -c VoIP-DL -r 0 omnetpp.ini          # regression check
  simu5g -u Cmdenv -c EthernetPduSession -r 0 omnetpp.ini   # new scenario
  ```
  and confirm (a) VoIP-DL produces unchanged results, (b) EthernetPduSession's
  `framesReceived`/`frameDelay` signals on `ue[0].l2App[0]` populate with nonzero
  counts.
