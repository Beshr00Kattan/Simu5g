# Documentation entry: Upf.ned MacForwardingTable submodule

## File: `src/nodes/Upf.ned`
**Baseline (confirmed by direct read):** `module Upf extends NetworkLayerNodeBase`,
gate `filterGate`, submodules `pppIf`, `udp`, `trafficFlowFilter`, `gtp_user`, `tn`,
`at`; connections include the fixed (non-dispatcher) link
`pppIf.upperLayerOut --> trafficFlowFilter.internetFilterGateIn`.

**Modified/new code:**
- `import inet.linklayer.ethernet.common.MacForwardingTable;`
- `trafficFlowFilter` gains one new parameter: `macForwardingTableModule = "^.macTable"`.
- new submodule: `macTable: MacForwardingTable { interfaceTableModule = "^.interfaceTable"; }`.

**Statement-by-statement / why:**
- `macTable: MacForwardingTable`: this is `inet::MacForwardingTable` — the real,
  already-existing learning+aging FDB module INET's own `EthernetSwitch` uses
  internally (confirmed during investigation: `EthernetSwitch.ned`'s `bridging`
  submodule, typically `Ieee8021dRelay`, calls into a `macTable: <default("MacForwardingTable")>`
  sibling for `learnUnicastAddressForwardingInterface()`/`getUnicastAddressForwardingInterface()`/
  aging). Reusing it here — rather than reimplementing learn/age/lookup logic from
  scratch, as the reference shim's bespoke `TsnMacForwarder.allowedDst` string
  comparison did — is the direct fix for the reference report's Section 8 "Not
  Modeled: Bridge learning and full IEEE 802.1 behavior" row.
- `interfaceTableModule = "^.interfaceTable"`: `MacForwardingTable.ned` declares this
  parameter with no default; `Upf` inherits an `interfaceTable: InterfaceTable`
  submodule from `LinkLayerNodeBase` (confirmed by reading INET's node-base NED
  chain), so `"^.interfaceTable"` (one level up from `macTable`, i.e. `Upf` itself)
  resolves correctly with no new submodule needed for this purpose.
- **Deliberate, documented adaptation:** `MacForwardingTable`'s API
  (`learnUnicastAddressForwardingInterface(int interfaceId, MacAddress, vid)`,
  `getUnicastAddressForwardingInterface(MacAddress, vid) -> int`) was designed for a
  real switch with numbered physical ports. The UPF has no such ports for Ethernet
  PDU session UEs — the equivalent "forwarding destination" is a UE's `MacNodeId`,
  not a `NetworkInterface` id. Reading `MacForwardingTable.cc`'s implementation of
  exactly these two methods during investigation confirmed neither cross-checks its
  `interfaceId` argument against the `InterfaceTable` (that cross-checking, where it
  exists, lives in the relay/switch module that calls this table, e.g.
  `Ieee8021dRelay`, not in the table itself) — so storing a `MacNodeId` value in the
  slot the API calls `interfaceId` is a safe, intentional reuse of the table's
  generic learn/age/lookup mechanics, not a misuse of interface semantics. This
  adaptation is documented here precisely so it is not mistaken for an oversight.
- **Why the UPF, not a new switch node:** TS 23.501 SS5.6.10.2 places Ethernet PDU
  session MAC learning at the UPF itself ("the UPF may include a MAC address
  learning function"). This matches the plan's scope decision 2 and is why
  `SingleCell_Standalone.ned`'s topology gains no new switch node — the existing
  `upf` submodule is the correct, spec-accurate location for this table.

**Replaces:** the reference shim's `TsnMacForwarder` module (Section 9.6 of the
reference report) and its `SwitchMacTsn`/`gNodeBMacTsn`-adjacent placement outside the
UPF entirely. Multi-UE support, which the shim's own Section 5 "Multi-UE Limitation"
box called out as broken (`allowedDst` only accepts one MAC), falls out for free here
because a real keyed table has no such single-value limitation.
