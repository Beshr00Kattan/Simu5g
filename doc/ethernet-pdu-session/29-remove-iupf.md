# 29 — Remove iUpf, connect upf directly to gnb

## What and why

`iUpf` was a second `Upf` instance in the baseline topology (inherited from
the original Simu5G example, representing 3GPP 38.801 deployment option 3 --
an anchor UPF not directly attached to the RAN). With `hasTsnEthPort` left at
its default `false`, it never ran any of this project's Ethernet PDU session
code (no `TrafficFlowFilter` MAC classification, no dedicated `tsnEth`
interface) -- per `Upf.ned`'s own comment, an intermediate UPF "just acts
like an IP router." It was, however, structurally load-bearing: `upf` and
`gnb` had no direct link, so every GTP-U tunnel packet on the N3 leg
physically transited `iUpf`'s PPP hop to reach `gnb`, even though the
tunnel's logical peer (resolved in `TrafficFlowFilter`/`GtpUser`) was always
`gnb` directly.

Removed per explicit instruction, since it added a topology hop with no
bearing on the Ethernet PDU session logic this project is about.

## Change

**File:** `simulations/NR/networks/SingleCell_Standalone.ned`
- Removed the `iUpf: Upf { ... }` submodule.
- Removed `upf.pppg++ <--> Eth10G <--> iUpf.pppg++;` and
  `iUpf.pppg++ <--> Eth10G <--> gnb.ppp;`.
- Added `upf.pppg++ <--> Eth10G <--> gnb.ppp;` -- `upf` now connects directly
  to `gnb`.

No other file changed. `upf`'s own `pppIf`/`gtp_user` wiring (in `Upf.ned`)
is unaffected -- it always sent/received GTP-U packets via `pppIf.phys <--> filterGate`
regardless of what was on the other end of that PPP link; that link is simply
one hop shorter now.

## Verification

Re-ran `EthernetPduSession` (1s, Cmdenv) immediately after the change:

```
server.l2App[0]  framesSent:count      100
ue[0].l2App[0]   framesReceived:count   99
```

Identical to the pre-removal result -- confirms `iUpf` was correctly
non-load-bearing for anything the Ethernet PDU session scenario measures.
