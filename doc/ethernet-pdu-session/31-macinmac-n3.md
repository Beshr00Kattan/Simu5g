# 31 — IP-less N3 via MAC-in-MAC (IEEE 802.1ah): whole-Ethernet server→UE

## Goal

Remove IP from the **last** leg that still used it — N3 (`upf → gnb`) — so that
**every hop from server to UE forwards Ethernet with no IP anywhere.** Up to
entry 30, N6 (`server↔upf`) and the radio were already IP-free; only N3 carried
the tenant frame inside a GTP-U/UDP/IP tunnel (the standard, 3GPP-compliant
transport).

## Why MAC-in-MAC (and not the two alternatives)

From the user-supplied research (`MAC-Aware Forwarding in 5G and Simu5G`,
`Ethernet-over-5G Topologies`, `TSN over 5G: Single-MAC Address Models`), the
comparison table lists exactly which N3 transports keep MAC transparency:

- **GTP-U (baseline):** MAC preserved, but transport is UDP/IP. ← what entry 30 has.
- **SRv6 L2VPN:** MAC preserved, but transport is **IPv6** — still IP. Rejected:
  wrong direction (would *add* an IPv6 stack).
- **Co-located UPF+gNB:** removes the *link*, not the *protocol*; not
  representable as two separate simulated nodes. Rejected.
- **MAC-in-MAC (IEEE 802.1ah, Provider Backbone Bridging):** an **outer**
  Ethernet header (backbone MACs) wraps the tenant frame; the tenant
  MAC/VLAN/PCP travel intact as the inner frame. **+16 B, full L2 features, and
  no IP at all.** Academic precedent: Hu et al. (2022) used 802.1ah to tunnel
  PROFINET over wireless TSN.

MAC-in-MAC is the only cited approach that is simultaneously MAC-transparent
**and** IP-less, so it is the one implemented here.

## Standards caveat (state this to the professor)

This is **deliberately non-3GPP-compliant.** 3GPP defines N3 as an IP transport
interface; running Ethernet directly on N3 is not in any release. It is an
**experimental / research** architecture — and, per the `MAC-Aware Forwarding`
paper, a genuine research gap (no published simulator implements a MAC-forwarding
UPF). The standard GTP-U path is kept as a selectable fallback (`n3Transport="gtp"`,
the default) so the entry-30 checkpoint behavior remains reproducible from the
same build. It also requires UPF and gNB to share one L2 domain (true for the
private/on-prem industrial-5G deployment this TSN work targets; not for a macro
operator core).

## What changed (only the N3 leg; classification & delivery are untouched)

The frame on N3 changes from
`[IPv4][UDP][GTP-U][ext][tenant Eth frame]` to
**`[outer EthernetMacHeader][GtpUserMsg][ext][tenant Eth frame][EthernetFcs]`** —
an 802.1ah backbone frame. The `GtpUserMsg.teid` (already = the UE's MacNodeId)
now plays the role of the 802.1ah **I-SID** (which session/UE this frame is
for), carried directly in Ethernet instead of over UDP/IP.

**`src/corenetwork/gtp/GtpUser.{ned,h,cc}`**
- New param `n3Transport @enum("gtp","ethernet") = default("gtp")`.
- New gates `n3EthOut` (UPF egress), `n3EthIn` (gNB ingress), both `@loose`.
- `handleFromTrafficFlowFilter()`, BS branch: if `n3Transport=="ethernet"`,
  call `sendOverN3Ethernet()` instead of `socket_.sendTo()` — no
  `getModuleNameByMacNodeId`/`L3AddressResolver`, no UDP/IP.
- `sendOverN3Ethernet()`: builds the outer `EthernetMacHeader` (dst = broadcast
  on the point-to-point backbone, src = a fixed transport MAC, EtherType 0x88B7)
  + placeholder `EthernetFcs` around the GtpUserMsg packet, tags
  `MacAddressReq`, and sends out `n3EthOut`. Built manually (like TsnEtherApp)
  because `n3EthOut` is direct-wired to a basic `EthernetMac`.
- `handleFromN3Ethernet()` (gNB): pops the outer header + FCS, then calls the
  shared `decapAndDeliver()`.
- `handleFromUdp()` refactored into a thin wrapper over the new
  `decapAndDeliver()` (the exact decap body from before) — so the GTP-U path and
  the Ethernet-N3 path share identical decapsulation, and only the *transport*
  differs.

**`src/nodes/Upf.ned`** — new param `hasEthernetN3`, a dedicated
`n3Eth: EthernetInterface` (promiscuous), gate `n3Ethg`, and
`gtp_user.n3EthOut --> n3Eth.upperLayerIn` (direct-wired, no nl/encap — the
same pattern as `tsnEth`).

**`src/nodes/NR/gNodeB.ned`** — added in the **subclass** (shared `eNodeB.ned`
untouched): param `hasEthernetN3`, `n3Eth: EthernetInterface` (promiscuous),
gate `n3Ethg`, and `n3Eth.upperLayerOut --> gtpUser.n3EthIn`. The decapsulated
frame still exits via the inherited `gtpUser.pppGate → nl → cellularNic` path,
so the whole radio side is unchanged.

**`simulations/NR/networks/SingleCell_Standalone.ned`** — `upf.hasEthernetN3 =
true`, `gnb.hasEthernetN3 = true`, and the N3 link changed from
`upf.pppg <--> gnb.ppp` (PPP/IP) to `upf.n3Ethg <--> Eth10G <--> gnb.n3Ethg`
(real Ethernet).

**`simulations/NR/standalone/omnetpp.ini`**, `[Config EthernetPduSession]` —
`*.upf.gtp_user.n3Transport = "ethernet"`, `*.gnb.gtpUser.n3Transport = "ethernet"`.

## What is unchanged (the reuse)

`TrafficFlowFilter::handleEthernetPacket()` (MAC→UE classification), the gNB's
`IP2Nic`/PDCP Ethernet branches, the UE-side `TsnEtherApp`, and the observers
are all untouched — only the N3 *transport* module changed. This mirrors exactly
how the N6 leg was earlier converted from a UDP/IP tunnel to a real `tsnSwitch`.

## Result

Every hop server→UE now carries Ethernet with no IP:
`server → tsnSwitch → upf → [outer-Ethernet N3] → gnb → radio → ue`. Log tag
`[ETH-PDU][N3-ETH]` marks the IP-less N3 transmit/receive (vs. the former
`[ETH-PDU][GTP-U]`). To restore the standard tunnel: set `n3Transport="gtp"`
and the N3 link back to PPP, or `git reset --hard macinmac-checkpoint`.
