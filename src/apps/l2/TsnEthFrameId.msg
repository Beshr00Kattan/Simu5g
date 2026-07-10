//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: a real, serialized frame-identifier
// field, carried in the frame's own payload bytes -- not a Packet-level tag.
// See doc/ethernet-pdu-session/28-packet-observers.md for the full
// baseline/modified/rationale documentation entry.
//
// Why this exists: TsnEtherApp's own delay measurement (CreationTimeTag,
// popped in handleFrame()) does not survive the UE-side RLC fragment/
// reassemble round-trip (confirmed live -- see
// doc/ethernet-pdu-session/26-ip2nic-ue-content-detection.md and the
// "frameDelay: nan" gap noted in the code-review report's SS14). A tag is
// out-of-band metadata; RLC has no obligation to preserve it. A field inside
// the frame's own data is not metadata -- it is the payload, and travels
// with the frame through every hop exactly like the real CbrPacket.IDframe
// field the stock CBR app already uses for the same purpose.
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

class TsnEthFrameIdHeader extends inet::FieldsChunk {
    // Real 3GPP Ethernet PDU Session work: a monotonically increasing,
    // per-source sequence number (L2App::seqCounter_'s value at send time),
    // read back by TsnEtherApp::handleFrame() on the receiving end and used
    // as the correlation key for PacketDelayObserver/PacketLossObserver's
    // network-wide "packetDelaySentSignal"/"packetDelayReceivedSignal"
    // (see TsnEtherApp.cc). Safe as a bare per-source counter in this
    // scope's single-active-TX-source scenarios (EthernetPduSession never
    // runs alongside another app emitting the same signal); a
    // multi-source deployment would need a source-qualified id instead.
    unsigned int frameId;
    chunkLength = inet::B(4);
}
