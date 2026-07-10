# 27 — MAC source/destination logging, promoted to EV_ERROR for visibility

## Request

With the pipeline confirmed working (entries 22-26), the Cmdenv/Qtenv console
is dominated by very high-volume `INFO`-level scheduler/HARQ logging
(`LteSchedulerEnbUl::racschedule`, `LteMacEnb::macSduRequest`, per-TTI
`start SCHED`/`end SCHED` banners, etc. — none of it written by this project).
Requested: surface the Ethernet PDU session's own key fields (source/destination
MAC, VLAN id/PCP, resolved UE id, GTP-U teid) and make them visually stand out
from that noise rather than being just another `INFO` line among hundreds.

## Approach

OMNeT++'s `EV` macro family has severity levels (`EV_DEBUG` < `EV_DETAIL` <
`EV_INFO` < `EV_WARN` < `EV_ERROR`); a bare `EV <<` defaults to `EV_INFO`. Both
Cmdenv and Qtenv render `EV_ERROR` lines distinctly from `EV_INFO` (Qtenv
colors/highlights them; Cmdenv prefixes differently), and — as importantly —
none of this project's own logging was previously above `INFO`, so it was
indistinguishable from the scheduler noise at a glance.

Every `EV <<` this project's own code had already written for the Ethernet PDU
session data path was promoted to `EV_ERROR`, and each line was expanded to
include the concrete MAC addresses (and other key identifiers) involved, not
just a bare event name. **This is a logging-visibility choice, not an actual
error condition** — every promoted line is documented as such at its call
site, so it isn't mistaken for a real fault during later maintenance.

## File-by-file

- **`src/apps/l2/TsnEtherApp.cc`**
  - `buildFrame()`: new `[ETH-PDU][TX]` line — srcMac, destMac, vid, pcp,
    etherType, payload length, sequence number.
  - `handleFrame()`: new `[ETH-PDU][RX]` line — srcMac/destMac/vid/pcp read
    back from the popped wire chunks, plus measured delay (or an explicit
    "n/a (tag lost in RLC)" when `CreationTimeTag` didn't survive — see the
    `frameDelay` gap noted in the report's §14).

- **`src/corenetwork/trafficFlowFilter/TrafficFlowFilter.cc`**,
  `handleEthernetPacket()`: all three existing `EV <<` lines (unknown-MAC
  drop, unattached-UE drop, successful forward) promoted to `EV_ERROR` and
  expanded with `srcMac`/`destMac`/resolved `destId`/serving BS
  (`[ETH-PDU][UPF]`). `srcMac` is now read out of the frame (it was already
  available via `addrFields`, just unused before).

- **`src/corenetwork/gtp/GtpUser.cc`**
  - `handleFromTrafficFlowFilter()`: new `[ETH-PDU][GTP-U][TX]` line at the
    "send to a BS" branch — the real GTP-U `teid` (the UE's MacNodeId) and
    the resolved tunnel peer (serving BS).
  - `handleFromUdp()`: the existing Ethernet-branch line promoted to
    `[ETH-PDU][GTP-U][RX]`.

- **`src/stack/ip2nic/IP2Nic.cc`**
  - `toStackBsEthernet()` (gNB uplink-to-radio-stack direction): unknown-MAC
    drop and successful-forward lines promoted to `EV_ERROR`
    (`[ETH-PDU][gNB]`), now including `srcMac`, `destMac`, resolved `destId`,
    and PCP.
  - `toIpUe()` (UE delivery): the Ethernet-branch delivery line promoted to
    `EV_ERROR` (`[ETH-PDU][UE]`), including srcMac/destMac read directly off
    the frame and whether the fast-path tag or the content-based fallback
    (entry 26) triggered delivery.

## Reading the log

Every line this project emits for the Ethernet PDU session data path now
starts with the literal tag `[ETH-PDU]`, followed by a stage marker
(`[TX]`/`[RX]`/`[UPF]`/`[GTP-U]`/`[gNB]`/`[UE]`). Searching/filtering the
Cmdenv output for `ETH-PDU` isolates the entire per-frame journey
(server send -> UPF classify -> GTP-U tunnel -> gNB forward -> UE receive)
from the surrounding scheduler noise.
