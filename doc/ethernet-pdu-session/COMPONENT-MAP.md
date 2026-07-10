# Component Map — where everything lives, in plain language

This is the "where do I find X" index for the Ethernet PDU Session
implementation. Every row is: **what it does (plain words)** → **the file, with
its directory**. Read it top-to-bottom and you follow one packet from the server
to the phone (UE).

All paths are relative to `samples/simu5g/`.

For the deep, line-by-line reasoning behind each file, the numbered entries in
this same folder (`doc/ethernet-pdu-session/00-…` through `32-…`, plus
`99-final-report.md`) are the detailed record. This file is the quick map on
top of them.

---

## The one-sentence summary

A server builds a **real Ethernet frame** (with a real VLAN tag, no IP address),
and that frame travels **all the way to the phone without ever being put inside
an IP packet** — the 5G core forwards it by looking at **MAC addresses**, not IP
addresses.

---

## 1. The traffic: who sends and receives the Ethernet frames

| What it does | File (with directory) |
|---|---|
| Base "Ethernet app": builds and receives raw Ethernet frames, never touches IP/UDP | `src/apps/l2/L2App.ned`, `L2App.cc`, `L2App.h` |
| The real sender/receiver used in the scenario: `L2App` + a **real 802.1Q VLAN tag** (VLAN id + priority) + a frame-id + the delay/loss measurement signals | `src/apps/l2/TsnEtherApp.ned`, `TsnEtherApp.cc`, `TsnEtherApp.h` |
| The 4-byte "frame number" carried **inside** each frame, so delay/loss can be measured reliably | `src/apps/l2/TsnEthFrameId.msg` |
| Sorts frames into priority queues by their VLAN priority (PCP) at the server's exit port | `src/apps/l2/TsnPcpClassifier.ned`, `TsnPcpClassifier.cc`, `TsnPcpClassifier.h` |

> Note: `src/apps/l2/TsnEtherSourceApp.*` is an earlier send-only variant; the
> active scenario uses `TsnEtherApp` on both ends (sender on the server, sink on
> the UE), so that is the one to read.

## 2. The "address book": how the system knows which MAC belongs to which phone

| What it does | File (with directory) |
|---|---|
| Stores, per UE: its **session type** (IPv4 or Ethernet) and its **MAC address → UE-id** mapping. This is the central lookup the whole design leans on. | `src/common/binder/Binder.h` (see `setPduSessionType` / `getPduSessionType` / `setUeMacAddress` / `getNodeIdForMacAddress`), `Binder.cc` |
| At the moment a UE attaches, reads the UE's declared `pduSessionType` and `tsnMac` from config and writes them into the Binder above | `src/stack/mac/layer/LteMacUe.cc` (around the `setPduSessionType` / `setUeMacAddress` calls) |

## 3. The UPF: the core box that classifies by MAC and sends over N3

| What it does | File (with directory) |
|---|---|
| The UPF node definition — gets a dedicated Ethernet port to the switch (`tsnEth`), a dedicated Ethernet port to the gNB (`n3Eth`), and a real learning MAC table (`macTable`) | `src/nodes/Upf.ned` |
| The actual **MAC-based forwarding decision**: reads the frame's destination MAC, looks up which UE owns it (learning table first, Binder second), finds that UE's serving base station | `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc` (`handleEthernetPacket`), `TrafficFlowFilter.ned`, `TrafficFlowFilter.h` |
| A **real, reused INET learning bridge table** (the same class a normal Ethernet switch uses) — this is the "forwarding table" | `inet::MacForwardingTable` (INET library; instantiated as the `macTable` submodule in `src/nodes/Upf.ned`) |
| Wraps the frame for the N3 hop and (in this project) sends it as **MAC-in-MAC over Ethernet, no IP** | `src/corenetwork/gtp/GtpUser.cc` (`handleFromTrafficFlowFilter`, `sendOverN3Ethernet`), `GtpUser.ned`, `GtpUser.h` |
| The real GTP-U header definition + the PDU-Session-Container extension header (real 3GPP fields) | `src/corenetwork/gtp/GtpUserMsg.msg` |

## 4. The gNB (5G base station): receives over N3 and unwraps

| What it does | File (with directory) |
|---|---|
| The gNB node definition — adds a dedicated Ethernet port (`n3Eth`) to receive the IP-less N3 frames. Radio side is left completely unchanged. | `src/nodes/NR/gNodeB.ned` |
| The shared base station definition (radio stack) — **not modified** for this work, shown here so you know the radio path is stock Simu5G | `src/nodes/eNodeB.ned` |
| Strips the outer Ethernet header on arrival, then unwraps to recover the original frame | `src/corenetwork/gtp/GtpUser.cc` (`handleFromN3Ethernet`, `decapAndDeliver`) |

## 5. The radio stack: carrying an Ethernet frame over the air (normally IP-only)

| What it does | File (with directory) |
|---|---|
| Chooses the destination UE from the **MAC address** instead of an IP address, and delivers the frame to the UE's Ethernet app instead of an IP layer | `src/stack/ip2nic/IP2Nic.cc` (`toStackBsEthernet`, `toIpUe`), `IP2Nic.h` |
| Skips IP-header compression (ROHC) for Ethernet frames — there is no IP header to compress | `src/stack/pdcp_rrc/layer/LtePdcpRrc.cc` (`headerCompress`, `headerDecompress`, `toDataPort`) |
| gNB-side: takes the already-resolved destination UE id from the MAC lookup instead of re-deriving it from an IP address | `src/stack/pdcp_rrc/layer/NRPdcpRrcEnb.cc` (`fromDataPort`) |
| UE-side PDCP counterpart | `src/stack/pdcp_rrc/layer/NRPdcpRrcUe.cc` |

## 6. The hosts (the two endpoints in the topology)

| What it does | File (with directory) |
|---|---|
| The **server**: a normal host plus a real dedicated Ethernet port + real Ethernet-framing module + a slot for the sender app | `src/nodes/EthernetPduSessionHost.ned` |
| The **UE (phone)**: gains a slot for the Ethernet app and the `pduSessionType` / `tsnMac` settings | `src/nodes/Ue.ned` |

## 7. Measurement (the evidence the frames arrive)

| What it does | File (with directory) |
|---|---|
| Counts sent vs. received frames network-wide and measures per-frame delay, keyed by the frame-id | `src/common/packetDelayObserver/PacketDelayObserver.cc`, `.ned`, `.h` |
| Counts total loss and loss percentage network-wide | `src/common/packetLossObserver/PacketLossObserver.cc`, `.ned`, `.h` |

## 8. The scenario (what you actually run)

| What it does | File (with directory) |
|---|---|
| The network topology: `server — tsnSwitch — upf — (Ethernet N3) — gnb — ue` | `simulations/NR/networks/SingleCell_Standalone.ned` |
| The run configuration: `[Config EthernetPduSession]` — turns on the Ethernet session, wires up the sender/sink, selects the IP-less N3 transport | `simulations/NR/standalone/omnetpp.ini` |
| The results of the last run (counts, delay, loss) | `simulations/NR/standalone/results/EthernetPduSession/0.sca` |

---

## The journey of one frame, as a path through the files above

1. **Server app builds the frame** — `src/apps/l2/TsnEtherApp.cc` → real Ethernet + VLAN, no IP.
2. **Through the switch** to the UPF — `simulations/NR/networks/SingleCell_Standalone.ned` (`tsnSwitch`).
3. **UPF decides by MAC** which UE and which base station — `src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc`.
4. **UPF sends over N3 as Ethernet** (no IP) — `src/corenetwork/gtp/GtpUser.cc` (`sendOverN3Ethernet`).
5. **gNB receives and unwraps** — `src/corenetwork/gtp/GtpUser.cc` (`handleFromN3Ethernet` → `decapAndDeliver`).
6. **Radio stack carries it to the right UE by MAC** — `src/stack/ip2nic/IP2Nic.cc`, `src/stack/pdcp_rrc/layer/*`.
7. **UE app receives the frame** — `src/apps/l2/TsnEtherApp.cc` (sink instance).
8. **Observers record it** — `src/common/packetDelayObserver/`, `src/common/packetLossObserver/`.

---

## What is genuine, and what is simplified (say this plainly)

**Genuine (real bytes / real reused mechanisms):**
- Real serialized Ethernet frames and real 802.1Q VLAN tags on the wire.
- Real, reused INET learning MAC table (`inet::MacForwardingTable`) inside the UPF.
- Real GTP-U header fields and a real PDU-Session-Container extension header.
- Real MAC-in-MAC (outer Ethernet header) on N3 — genuinely no IP anywhere on the path.
- Delivery independently confirmed by two separate measurement paths (observers + per-hop logs).

**Simplified / out of scope (do not overclaim):**
- **Downlink only** (server → UE). No uplink Ethernet path is implemented.
- The N3 "I-SID" (which-UE identifier) reuses the existing GTP-U `teid` field; it is **not** a byte-accurate IEEE 802.1ah I-TAG. Call this "802.1ah-inspired," not "802.1ah-compliant."
- The N3 outer MAC is a fixed broadcast address — fine for one gNB on a point-to-point link, not a multi-gNB backbone.
- **Non-3GPP by design:** 3GPP defines N3 as an IP interface. Running Ethernet directly on N3 is an experimental research choice; the standard GTP-U path is kept as a selectable fallback (`n3Transport = "gtp"`).
- No handover on the Ethernet path; single cell, single UE.
- The per-app `frameDelay` scalar reads `nan`; the **network-wide observer** delay figure is the reliable one.
