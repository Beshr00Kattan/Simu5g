# Documentation entry: TsnEtherApp (real 802.1Q VLAN tag)

## Files: `src/apps/l2/TsnEtherApp.{ned,h,cc}`
**Baseline:** No baseline — new file. The reference shim's `TSNApp` (Section 9.3)
carried `vid`/`pcp` as plain integer fields inside the `TsnEtherTag` metadata tag,
never serialized to wire bytes.

**New code / explanation:**
- `TsnEtherApp.ned`: adds `vid` (12-bit VLAN ID) and `pcp` (3-bit priority) parameters
  to `L2App`.
- `TsnEtherApp::buildFrame()` overrides (does not call) `L2App::buildFrame()` and
  instead builds `[EthernetMacAddressFields(dest,src)] + [Ieee8021qTagEpdHeader(pcp,
  dei,vid,typeOrLength)] + payload`. `Ieee8021qTagEpdHeader` is a real INET chunk
  (`inet/linklayer/ieee8021q/Ieee8021qTagHeader.msg`) that folds the VLAN control
  fields and the trailing real EtherType into one 4-byte chunk, with the C-tag TPID
  (0x8100) implied rather than stored — one of INET's two genuine 802.1Q wire
  encodings (the other, `Ieee8021qTagTpidHeader`, stores an explicit TPID and is used
  e.g. for S-tags; not used here since a single C-tag is sufficient for this scope).
- `handleFrame()` pops the two chunks in mirror order, dropping (with
  `framesDroppedSignal_`) any packet missing either — a genuine parse failure, not a
  tag-absence heuristic.

**Why (spec justification):** IEEE 802.1Q §9.6 defines the C-VLAN tag format (PCP,
DEI, VID fields); using INET's real chunk class for it is the direct fix for the
reference report's Section 8 "Not Modeled: Serialized Ethernet headers" and "VLAN-like
vid/pcp metadata" rows — both become genuinely modeled (real wire bytes) here.

**Replaces:** shim's `TsnEtherTag.vid`/`TsnEtherTag.pcp` metadata int fields (Section
9.4 of the reference report).
