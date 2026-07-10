# 30 — CHECKPOINT: revert point before the MAC-in-MAC (802.1ah) N3 redesign

## Why this entry exists

The next change (entry 31) replaces the N3 (`upf → gnb`) transport from
GTP-U/UDP/IP to IP-less MAC-in-MAC (IEEE 802.1ah). That is a deliberately
**non-3GPP-compliant, experimental** architecture (N3 is defined by 3GPP as
an IP transport interface). Because it is structural and reversible-by-choice,
a clean, tagged revert point was taken first.

## What this checkpoint contains (the known-good state)

The fully working Ethernet PDU session with the **standards-aligned** N3
transport (GTP-U/UDP/IP carrying the raw Ethernet frame as payload — the
"Ethernet PDU" row of the literature's comparison table). Confirmed by real
runs (Cmdenv, exit 0):

| Metric | 1 s run | ~205 s / 20500-frame run |
|---|---|---|
| framesSent | 100 | 20500 |
| framesReceived | 99 | 20499 |
| framesDropped | 0 | 0 |
| PacketDelayObserver mean delay | 5.0 ms | 5.0 ms (stddev 0) |
| PacketLossObserver loss | ~1% (boundary) | 0.0049% |

End-to-end path at this checkpoint:

```
server(TsnEtherApp) -> tsnSwitch(EthernetSwitch) -> upf(TrafficFlowFilter, MAC-based classify)
  -- GTP-U / UDP / IP tunnel (N3) --> gnb -> PDCP/RLC/MAC/PHY/radio -> ue(TsnEtherApp sink)
```

Everything is MAC-forwarded except the one N3 leg, which is IP (the GTP-U
tunnel). This is exactly the state the code-review report's §14 and the
plain-language explainer describe.

## Git details

- Repository: `git init` was run in `samples/simu5g/` (the project was not
  previously under version control). The stock simu5g `.gitignore` already
  excludes `out/`, `*.dll`, `*.def`, generated `*_m.{cc,h}`, `Makefile`, and
  results, so only real source is tracked (~1019 files).
- Commit: `5dc70b6` — "CHECKPOINT: working Ethernet PDU session, GTP-U/UDP/IP
  on N3 (pre MAC-in-MAC)".
- Tag: **`macinmac-checkpoint`**.

## How to revert to this exact state later

From a shell in `samples/simu5g/`:

```
git reset --hard macinmac-checkpoint
```

then rebuild (the build artifacts and generated `_m.*` files are not tracked,
so a rebuild regenerates them):

```
make MODE=debug -j4        # in samples/simu5g/src, with the OMNeT++ env on PATH
```

To instead inspect the difference without reverting:

```
git diff macinmac-checkpoint            # everything changed since the checkpoint
git stash                               # shelve current work-in-progress if needed
```

If you want to keep the MAC-in-MAC work but branch the checkpoint off for
safety, `git switch -c pre-macinmac macinmac-checkpoint` creates a branch
pinned to this state.

## What comes next (entry 31)

Replace the N3 GTP-U/UDP/IP encapsulation with IEEE 802.1ah MAC-in-MAC so the
`upf → gnb` leg carries pure Ethernet (outer backbone MAC header + optional
I-SID = UE MacNodeId, inner tenant frame), making the entire path server→UE
IP-free. The classification (`TrafficFlowFilter`) and UE-side delivery
(`IP2Nic` Ethernet branch, PDCP guards) are unchanged — only the N3 transport
is swapped, mirroring how the N6 (`server↔upf`) leg was already converted from
a UDP/IP tunnel to a real `tsnSwitch`. GTP-U is kept as a selectable fallback
mode so this checkpoint's behavior remains reproducible from the same build.
