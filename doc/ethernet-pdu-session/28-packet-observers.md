# 28 — Network-wide packet delay/loss observers, adapted from the user's own modules

## What was added

The user supplied two prior working modules of their own (`cbr.zip`,
`Packet Loss and Delay Observer.zip`) and asked for them to be added to this
project, adapted to fit it. Contents:

- `cbr.zip`: the stock Simu5G CBR app (`CbrSender`/`CbrReceiver`), instrumented
  with four extra signals (`packetDelaySentSignal`, `appPacketSent`,
  `packetDelayReceivedSignal`, `appPacketReceived`) keyed by the CBR packet's
  own real `IDframe` field.
- `Packet Loss and Delay Observer.zip`: two standalone modules,
  `PacketDelayObserver` and `PacketLossObserver`, that each subscribe to a
  pair of those signals **at the system-module level** (i.e. network-wide,
  no per-app wiring needed) and compute end-to-end delay / loss statistics.

## Why this fits directly into the existing `frameDelay: nan` gap

The code-review report's §14 already flagged that `TsnEtherApp`'s own delay
measurement reads `nan`, because it relies on `CreationTimeTag` (a
Packet-level tag), which does not survive the UE-side RLC fragment/reassemble
round-trip -- confirmed by a live run. The uploaded CBR instrumentation solves
the *identical* problem with a *different* mechanism: it keys delay
measurement off the packet's own real, serialized `IDframe` field instead of
a tag. A field inside a packet's payload is not out-of-band metadata; RLC has
no way to drop it without dropping the frame's own data, so it is far more
robust than a tag. Adapting this pattern to `TsnEtherApp` closes the exact
gap flagged earlier, using the user's own already-working design instead of
inventing a new mechanism.

## Files added, unchanged from the uploads except for path/package adjustment

- `src/common/packetDelayObserver/PacketDelayObserver.{ned,h,cc}`
- `src/common/packetLossObserver/PacketLossObserver.{ned,h,cc}`

(package changed to `simu5g.common.packetDelayObserver` /
`simu5g.common.packetLossObserver` to match this project's existing
`src/common/<feature>/` convention; one unused leftover NED declaration
(`packetScheduledWithMcs`, referenced nowhere in the uploaded `.cc`) was
dropped as dead code; `PacketDelayObserver`'s matched-entry cleanup now
erases the entry from `sentTimes` after a successful delay computation, to
avoid unbounded map growth over a long run -- the only functional addition
beyond the original).

Both subscribe via `getSystemModule()->subscribe(...)`, so a single instance
of each, placed anywhere in the network, observes every app in the whole
simulation without any NED-level wiring to the apps it watches. They were
added as two new submodules of `SingleCell_Standalone`
(`simulations/NR/networks/SingleCell_Standalone.ned`).

## Files modified to adopt the same instrumentation

**`src/apps/cbr/CbrSender.{h,cc,ned}` / `CbrReceiver.{h,cc,ned}`** -- the
uploaded signal additions, applied verbatim (minus the uploaded version's
commented-out dead `monitorOut`/`sendDirect` experiment code, which was not
carried over). Existing CBR statistics/behavior are completely unchanged;
these are pure additions.

**`src/apps/l2/TsnEtherApp.{h,cc}`** -- adapted (not copied verbatim, since
`TsnEtherApp` has no `IDframe`-style existing field to reuse):

- New file `src/apps/l2/TsnEthFrameId.msg`: a small real `FieldsChunk`,
  `TsnEthFrameIdHeader { unsigned int frameId; }` (4 bytes) -- the
  `TsnEtherApp`-side equivalent of `CbrPacket.IDframe`. Real, serialized
  payload data, not a tag.
- `buildFrame()`: inserts `TsnEthFrameIdHeader` (set to `seqCounter_`,
  `L2App`'s own existing per-instance monotonic counter) into the frame,
  immediately before the abstract `ByteCountChunk` filler (whose length is
  reduced by 4 bytes so the frame's total configured `packetLength_` is
  unchanged). Emits `packetDelaySentSignal`/`appPacketSent` with that same id.
- `handleFrame()`: pops `TsnEthFrameIdHeader` and emits
  `packetDelayReceivedSignal`/`appPacketReceived` with the id read back from
  the wire. The existing `CreationTimeTag`-based `frameDelay` signal/log line
  is left in place unchanged (still reads "n/a" for the documented reason,
  see entry 26) -- this is an addition, not a replacement, so nothing that
  already depended on `framesReceived`/`frameDelay` breaks.

Both `TsnEtherApp` and CBR register the *same* signal names
(`packetDelaySentSignal`, `appPacketSent`, etc.) -- `registerSignal()` returns
the same id for the same name regardless of which module calls it, so one
pair of network-wide observers correctly measures either traffic source (they
are never both active in the same config in this project's current scenarios,
so there is no id-collision risk in practice; a future multi-source scenario
would need a source-qualified id, noted in `TsnEthFrameId.msg`'s own comment).

## Build note

New source files under a **new directory** (`src/common/packetDelayObserver/`,
`src/common/packetLossObserver/`) and a **new `.msg` file**
(`src/apps/l2/TsnEthFrameId.msg`) require the project's `Makefile` to be
regenerated (`opp_makemake --make-so -f --deep ...`, i.e. `make makefiles`)
before a plain `make` will pick them up -- this project's Makefile lists
object files explicitly, it does not glob. Done once as part of landing this
entry; a normal `make` picks up further edits to these same files from here on.
