# 17 — tsnSwitch as the sole server<->upf connection (no direct link)

## Change

Entry 16 removed `router` but kept a direct `server.pppg++ <--> upf.filterGate`
PPP link so `VoIP-DL`/`VoIP-UL` would keep an IP route. The user pointed out
(from the Qtenv topology view) that having *both* a direct server-upf line
*and* a server-tsnSwitch-upf path is not logical for this deployment: the
switch should be the only way these two nodes talk to each other.

**File:** `simulations/NR/networks/SingleCell_Standalone.ned`
- Removed `server.pppg++ <--> Eth10G <--> upf.filterGate;` entirely.
- `upf.filterGate` is now left unconnected at the network level (same status
  as `iUpf.filterGate` already had — both are `@loose` gates, so this is
  valid, not an error).
- `tsnSwitch` (via `server.tsnEthg <--> tsnSwitch.ethg++ <--> upf.tsnEthg`) is
  now the **only** connection between `server` and `upf`.

## Consequence (explicit, not accidental)

`VoIP-DL`/`VoIP-UL` (the two pre-existing configs that send ordinary UDP/IP
traffic between `server` and `ue[0]`) relied on the server-router-upf /
server-upf PPP path to reach the UPF. With no IP link between `server` and
`upf` at all, `server` has no IP route to the UPF and these two configs will
not deliver traffic in the current topology. This trade-off follows directly
from the explicit instruction that a direct server-upf link is "not logical"
next to the switch; it has not been separately fixed since the active work in
this session is entirely the `EthernetPduSession` config, not `VoIP-DL/UL`.
If those two configs need to keep working, the appropriate fix (not applied
here, flagged for a future decision) would be running the ordinary IP traffic
over the same `tsnSwitch` link (i.e. `server`/`upf` speaking IP-over-Ethernet
on `tsnEth` instead of a separate PPP interface) rather than reintroducing a
second physical link.
