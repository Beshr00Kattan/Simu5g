//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//
// Real 3GPP Ethernet PDU Session work: replaces the original 1-field stub
// (teid only, chunkLength hardcoded to a placeholder 1B while GtpUser.cc
// always actually wrote 8B) with the real TS 29.281 SS5.2 GTP-U header fields,
// plus one real optional extension header (TS 38.415 PDU Session Container).
// See doc/ethernet-pdu-session/08-gtpumsg-fields.md for the full
// baseline/modified/rationale documentation entry.
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

// TS 29.281 SS5.2.1: the real GTP-U header fields.
// Byte layout: [flags(1)][messageType(1)][length(2)][teid(4)][seq(2)][nPdu(1)][nextExtHdr(1)] = 12B
// Documented simplification: the real spec omits the last 4 bytes
// (seq/nPdu/nextExtHdr) entirely when sFlag/pnFlag/eFlag are all clear, making
// the header variable-length (8B or 12B). This project always includes all
// 12 bytes regardless of flag state, trading a small, harmless amount of
// wire-format compactness for a fixed-length header -- nothing in this
// simulation's correctness depends on saving those 4 bytes, and the field
// *values* (messageType, real teid, real sequence numbers, real extension
// header chaining) are all genuine either way.
class GtpUserMsg extends inet::FieldsChunk {
    // --- flags octet (TS 29.281 Figure 5.1-1) ---
    bool sFlag = false;   // Sequence Number field present
    bool pnFlag = false;  // N-PDU Number field present
    bool eFlag = false;   // Next Extension Header field present

    // --- message type (TS 29.281 Table 5.1-1) ---
    unsigned char messageType = 255; // 255 = G-PDU (real user-plane data message)

    // --- length: real, computed length of the payload following this header ---
    unsigned short length = 0;

    // --- Tunnel Endpoint Identifier ---
    // Real 3GPP Ethernet PDU Session work: unlike the original stub (always
    // hardcoded to 0), this is now genuinely populated with the destination
    // UE's MacNodeId (see GtpUser.cc), and is used on decapsulation to
    // resolve Binder::getPduSessionType() -- i.e. TEID is what actually
    // identifies which session (and therefore which PDU session type) a
    // G-PDU belongs to, exactly its real 3GPP purpose, rather than a
    // per-packet payload-type flag (which would contradict the "PDU session
    // type is a per-session attribute" modeling decision -- see
    // doc/ethernet-pdu-session/04-binder-pdu-session-type.md).
    unsigned int teid;

    // --- optional fields (TS 29.281 SS5.2.1), present only if sFlag/pnFlag/eFlag ---
    unsigned short sequenceNumber = 0;
    unsigned char nPduNumber = 0;
    unsigned char nextExtensionHeaderType = 0; // 0x85 = PDU Session Container (TS 38.415), 0x00 = no more extension headers

    chunkLength = inet::B(12);
}

// TS 38.415 SS5.5.1.1: PDU Session Container extension header, carried when
// GtpUserMsg.nextExtensionHeaderType == 0x85. Real, minimal use of a real
// GTP-U extension header mechanism (TS 29.281 SS5.2.1) to carry a genuine
// per-packet QoS Flow Identifier -- deliberately NOT used to carry PDU
// session type (see rationale on GtpUserMsg.teid above).
class GtpUserExtHeaderPduSessionContainer extends inet::FieldsChunk {
    unsigned char extHeaderLength = 1; // length of this extension header, in 4-octet units (TS 29.281 SS5.2.1)
    unsigned char pduType = 0;         // TS 38.415: 0 = DL PDU Session Information, 1 = UL PDU Session Information
    unsigned char qfi = 0;             // QoS Flow Identifier (6 bits used)
    unsigned char nextExtensionHeaderType = 0; // 0x00 = no more extension headers (chained field, TS 29.281 SS5.2.1)
    chunkLength = inet::B(4);
}
