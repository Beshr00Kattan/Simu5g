# Documentation entry: GtpUserMsg real header fields + PDU Session Container

## File: `src/corenetwork/gtp/GtpUserMsg.msg`
**Baseline (confirmed by direct read):**
```
class GtpUserMsg extends inet::FieldsChunk {
    unsigned int teid;
    chunkLength = inet::B(1); // TODO: size 0
}
```
`teid` was always hardcoded to `0` by every call site in `GtpUser.cc`; `chunkLength`
was declared as a placeholder 1 byte while the code always actually wrote 8 bytes
(reconciled at runtime by a padding-write "remainders" hack in the old serializer).
No message type, flags, sequence number, or extension-header support existed.

**Modified/new code:** replaced the single field with the real TS 29.281 §5.2.1
fixed-header fields (`sFlag`/`pnFlag`/`eFlag`, `messageType` default 255 = G-PDU,
`length`, `teid`, `sequenceNumber`, `nPduNumber`, `nextExtensionHeaderType`), plus a
new sibling chunk class `GtpUserExtHeaderPduSessionContainer` (TS 38.415 §5.5.1.1:
`extHeaderLength`, `pduType`, `qfi`, `nextExtensionHeaderType`).

**Statement-by-statement / why:**
- `messageType = 255`: TS 29.281 Table 5.1-1's real value for "G-PDU" (user-plane
  data), as opposed to the control messages (echo request/response, error
  indication, etc.) this project doesn't model — a real, correct constant instead of
  an arbitrary placeholder.
- `teid`: **the load-bearing field for this project's IP-vs-Ethernet dispatch.**
  Rather than repeating a per-packet "is this Ethernet" flag (which would contradict
  this project's own modeling decision that PDU session type is a per-session, not
  per-packet, attribute — see entry 04), `teid` is now genuinely populated with the
  destination UE's `MacNodeId` on encapsulation (see `GtpUser.cc`'s doc entry), and
  the decapsulating side resolves `Binder::getPduSessionType(teid)` to decide how to
  handle the payload — using TEID for exactly what TS 29.281 defines it for
  (identifying the tunnel/session), not inventing a new field to do that job.
- `nextExtensionHeaderType` + `eFlag`: real TS 29.281 extension-header chaining
  fields, used to signal the presence of `GtpUserExtHeaderPduSessionContainer`
  (`nextExtensionHeaderType == 0x85`, TS 38.415's registered value for the PDU
  Session Container).
- **Documented simplification:** the real spec makes the last 4 bytes (sequence
  number / N-PDU number / next-extension-header-type) conditionally absent when
  `sFlag`/`pnFlag`/`eFlag` are all clear, making the header 8 or 12 bytes. This
  project always includes all 12 bytes (`chunkLength = inet::B(12)` unconditionally)
  for implementation simplicity — the field *values* are genuine either way, and
  nothing in this simulation depends on the 4-byte wire-format saving.
- `GtpUserExtHeaderPduSessionContainer.qfi`: a genuine, spec-defined per-packet QoS
  Flow Identifier field (TS 38.415) — included to demonstrate the real GTP-U
  extension-header mechanism end-to-end, not to carry PDU session type (which stays
  at the TEID/session level, per the above).

**Replaces:** the reference shim's approach of not touching GTP-U at all (it bypassed
`GtpUser`/`TrafficFlowFilter` entirely via a parallel `tsnFwd`/`TsnMacForwarder` path,
Section 7/9.6 of the reference report) — this is the direct fix for the reference
report's own Section 8 "Not Modeled: UPF Ethernet PDU-session classification/
tunneling" row.

## Files: `src/corenetwork/gtp/GtpUserMsgSerializer.{h,cc}`
**Baseline:** wrote/read only a 4-byte TEID inside a stream padded to whatever
`chunkLength` the caller had set (always 8B in practice), via a computed
`remainders` byte-fill loop.

**Modified/new code:** `GtpUserMsgSerializer::serialize()`/`deserialize()` rewritten
to read/write the real 12-byte layout field-by-field (flags octet packed as
`version(3 bits,=1) | PT(1,=1) | spare(1,=0) | E | S | PN`, matching TS 29.281 Figure
5.1-1, then messageType/length/teid/sequenceNumber/nPduNumber/nextExtensionHeaderType
in order) — no more padding-fill hack, since every byte written now corresponds to a
real field. A new `GtpUserExtHeaderPduSessionContainerSerializer` class serializes/
deserializes the 4-byte extension header the same way.

**Why:** keeps the wire representation consistent with the new real fields — Simu5G's
existing `GtpUser.cc` already actively serializes packets at runtime (via
`PacketPrinter::printPacket()` on the receiving side), so an unregistered or
inconsistent chunk layout is a genuine runtime-correctness risk, not just a paperwork
concern.
