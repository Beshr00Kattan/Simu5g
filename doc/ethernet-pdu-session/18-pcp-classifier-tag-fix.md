# 18 — TsnPcpClassifier: switch from peeking wire chunks to inet::PcpReq

## Symptom

After entry 16's unified single-header frame redesign, the exact same class
of error resurfaced at the new location:

```
Cannot convert chunk from type inet::ByteCountChunk to type
inet::Ieee8021qTagEpdHeader ... module (TsnPcpClassifier)
SingleCell_Standalone.server.tsnEth.queue.classifier
```

Both `EthernetMacHeader` and `Ieee8021qTagEpdHeader` are fixed-length chunks
(14B / 4B respectively, confirmed in `EthernetMacHeader.msg` /
`Ieee8021qTagHeader.msg`) inserted at known offsets by
`TsnEtherApp::buildFrame()`, so the classifier's offset arithmetic itself was
correct. The packet arriving at `inet::PriorityQueue`'s internal classifier
submodule nonetheless did not expose the same chunk boundaries the packet had
at construction time — consistent with (though not fully root-caused past)
INET's chunk immutability/simplification machinery collapsing adjacent
chunks into a canonical byte-region representation once a packet is queued.

## Fix

Rather than keep chasing chunk-boundary behavior inside a queueing module,
switched to the real, purpose-built INET mechanism for this exact situation:
`inet::PcpReq` (`inet/linklayer/common/PcpTag.msg`), whose own doc comment
states it "determines the PCP that should be used to send the packet" and
"may be present on a packet from the application to the mac protocol" — this
is the same category of mechanism `inet::QosClassifier`/`RandomQosClassifier`
use, precisely because re-parsing wire bytes for a queueing module's own
internal scheduling decision is unnecessary and, as observed here, fragile.

- **`src/apps/l2/TsnEtherApp.cc`**: `buildFrame()` now also sets
  `pkt->addTagIfAbsent<PcpReq>()->setPcp(pcp_)`. The real
  `Ieee8021qTagEpdHeader` chunk is still inserted into the packet's data and
  still travels the wire end-to-end (unchanged) — the tag is a companion for
  internal simulation bookkeeping, not a replacement for the real header.
- **`src/apps/l2/TsnPcpClassifier.cc`**: `classifyPacket()` now reads
  `packet->findTag<PcpReq>()` instead of peeking `EthernetMacHeader` +
  `Ieee8021qTagEpdHeader` off the packet's chunk stack.

This does not weaken the "real, byte-serialized frame" requirement from the
original scope: the VLAN tag remains genuine wire bytes, popped by the UE's
`TsnEtherApp::handleFrame()` exactly as before. Only the *queue's own*
internal priority decision now reads a tag instead of re-parsing bytes it has
no other reason to touch.
