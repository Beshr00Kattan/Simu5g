# Documentation entry: `[Config EthernetPduSession]`

## File: `simulations/NR/standalone/omnetpp.ini`
**Baseline (confirmed by direct read):** three configs exist --
`[Config Standalone]` (base: network, gNB/UE placement, cell IDs), `[Config VoIP-DL]`
(`extends=Standalone`), `[Config VoIP-UL]` (`extends=Standalone`). No other
`[Config ...]` sections exist in this file.

**New code:** `[Config EthernetPduSession]`, `extends=Standalone` — added after
`[Config VoIP-UL]`, leaving all three existing sections completely untouched (not
one existing line was edited).

**Statement-by-statement:**
- `*.ue[0].pduSessionType = "Ethernet"` / `*.ue[0].tsnMac = "00:00:00:00:00:02"`:
  registers this UE's session type and MAC address (see entries 04/07).
- `*.ue[*].numApps = 0` / `*.server.numApps = 0` then `*.server.numApps = 1`: this
  scenario runs no ordinary UDP/IP apps at all (unlike VoIP-DL/UL) — matching the
  reference shim's own `omnetppTSN.ini` pattern of disabling ordinary apps for its
  TSN-only scenario, for the same reason: the measured traffic must not share a path
  with, or be confused for, ordinary IP application traffic.
- `*.server.app[0].typename = "TsnEtherSourceApp"` + its `srcMac`/`destMac`/`vid`/
  `pcp`/`destAddress="upf"` parameters: configures the AF-side source (entry 03).
  `pcp = 5` is deliberately `>= highPriorityMinPcp` (default 4), so this flow visibly
  takes the high-priority queue in the Layer B wiring below.
- `*.ue[0].numL2Apps = 1` + `*.ue[0].l2App[0].typename = "TsnEtherApp"` +
  `destMac = ""`: configures the UE-side sink (entry 01) — the empty `destMac`
  is what makes `L2App::initialize()` skip starting a send timer, per its own
  documented "only the sending side...leaves destMac empty" logic.
- `*.server.ppp[0].queue.typename = "inet.queueing.queue.PriorityQueue"` + `classifier.typename
  = "simu5g.apps.l2.TsnPcpClassifier"`: Layer B's real PCP-based priority queueing
  (entry 12), applied purely through configuration, no NED changes.

**Why `extends=Standalone` (not a new, separate network config):** reuses the exact
same gNB/UE placement, cell IDs, and radio parameters every other config in this file
already uses — the only things that differ are the session type/MAC registration, the
app types, and the queue configuration, which is exactly what "extends" is for.

**Replaces:** the reference shim's `[Config L2TsnEtherPdu]` in a separate
`omnetppTSN.ini` file extending a separate `SingleCell_Standalone_L2Tsn` network
(Section 2/9.1 of the reference report) — here the same real network and the same
`omnetpp.ini` file used by every other scenario gains one more config section.
