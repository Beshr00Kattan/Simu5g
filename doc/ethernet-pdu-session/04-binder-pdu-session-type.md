# Documentation entry: Binder PduSessionType

## File: `src/common/binder/Binder.h`
**Baseline:** `class Binder` had `macNodeIdToIPAddress_`/`nrMacNodeIdToIPAddress_` maps
and `getMacNodeId`/`getNrMacNodeId`/`setMacNodeId`/`setX2NodeId` inline methods
(confirmed by direct read); no `PduSessionType` concept existed anywhere in Simu5G
(confirmed by exhaustive grep during planning).

**Modified/new code:**
- `enum class PduSessionType { IPV4, ETHERNET };` at file scope, above `class Binder`.
- `std::map<MacNodeId, PduSessionType> pduSessionType_;` private member.
- `void setPduSessionType(MacNodeId, PduSessionType)` / `PduSessionType
  getPduSessionType(MacNodeId)` inline public methods, next to `setMacNodeId`.

**Statement-by-statement:**
- `enum class PduSessionType { IPV4, ETHERNET }`: scoped enum (not a plain `enum`,
  to avoid polluting the global namespace with `IPV4`/`ETHERNET`, consistent with
  modern C++ used elsewhere in this file, e.g. `RanNodeType`).
- `getPduSessionType()`'s `find()`-miss branch returns `PduSessionType::IPV4`: this is
  the load-bearing backward-compatibility line — every UE in every pre-existing
  scenario never calls `setPduSessionType()`, so every existing lookup returns IPV4,
  meaning `TrafficFlowFilter`'s new branch (see its own doc entry) always takes the
  unchanged, original IP-classification path for them.

**Why (architectural justification):** per the plan's scope decision 1, this is
deliberately *not* a NAS PDU Session Establishment message exchange — Simu5G has no
NAS/SM layer at all, and building one would be disproportionate to what Layer A
actually needs (a real, queryable, forwarding-plane-affecting session-type attribute).
This mirrors the "configure once, register at init" pattern Simu5G already uses for
`setMacNodeId()`, called from the same place (`LteMacUe::initialize()` — see the next
doc entry) that already performs the IP↔MacNodeId registration this project's pattern
is modeled on.

**Replaces:** nothing directly (the reference shim had no PDU-session-type concept at
all — it hard-coded a single always-TSN topology). This is new ground, not a
modification of prior shim behavior.
