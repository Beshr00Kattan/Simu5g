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
// Real 3GPP Ethernet PDU Session work: rewritten to (de)serialize the real
// TS 29.281 SS5.2.1 header fields GtpUserMsg.msg now declares, instead of the
// original stub that only wrote/read a 4-byte teid inside an artificially
// inflated, mostly-padding 8-byte chunk. See
// doc/ethernet-pdu-session/08-gtpumsg-fields.md.
//

#include "corenetwork/gtp/GtpUserMsg_m.h"
#include "corenetwork/gtp/GtpUserMsgSerializer.h"
#include "inet/common/packet/serializer/ChunkSerializerRegistry.h"

using namespace inet;

Register_Serializer(GtpUserMsg, GtpUserMsgSerializer);

void GtpUserMsgSerializer::serialize(MemoryOutputStream& stream, const Ptr<const Chunk>& chunk) const
{
    const auto& gtpUserMsg = staticPtrCast<const GtpUserMsg>(chunk);

    // TS 29.281 Figure 5.1-1 flags octet: version(bits 7-5, =1) | protocol
    // type (bit 4, =1 for GTP, as opposed to GTP') | spare (bit 3, =0) |
    // E | S | PN (bits 2-0).
    uint8_t flagsOctet = (1 << 5) | (1 << 4)
            | (gtpUserMsg->getEFlag() ? 0x04 : 0)
            | (gtpUserMsg->getSFlag() ? 0x02 : 0)
            | (gtpUserMsg->getPnFlag() ? 0x01 : 0);
    stream.writeByte(flagsOctet);
    stream.writeByte(gtpUserMsg->getMessageType());
    stream.writeUint16Be(gtpUserMsg->getLength());
    stream.writeUint32Be(gtpUserMsg->getTeid());
    stream.writeUint16Be(gtpUserMsg->getSequenceNumber());
    stream.writeByte(gtpUserMsg->getNPduNumber());
    stream.writeByte(gtpUserMsg->getNextExtensionHeaderType());
}

const Ptr<Chunk> GtpUserMsgSerializer::deserialize(MemoryInputStream& stream) const
{
    auto gtpUserMsg = makeShared<GtpUserMsg>();

    uint8_t flagsOctet = stream.readByte();
    gtpUserMsg->setEFlag((flagsOctet & 0x04) != 0);
    gtpUserMsg->setSFlag((flagsOctet & 0x02) != 0);
    gtpUserMsg->setPnFlag((flagsOctet & 0x01) != 0);
    gtpUserMsg->setMessageType(stream.readByte());
    gtpUserMsg->setLength(stream.readUint16Be());
    gtpUserMsg->setTeid(stream.readUint32Be());
    gtpUserMsg->setSequenceNumber(stream.readUint16Be());
    gtpUserMsg->setNPduNumber(stream.readByte());
    gtpUserMsg->setNextExtensionHeaderType(stream.readByte());

    return gtpUserMsg;
}

Register_Serializer(GtpUserExtHeaderPduSessionContainer, GtpUserExtHeaderPduSessionContainerSerializer);

void GtpUserExtHeaderPduSessionContainerSerializer::serialize(MemoryOutputStream& stream, const Ptr<const Chunk>& chunk) const
{
    const auto& ext = staticPtrCast<const GtpUserExtHeaderPduSessionContainer>(chunk);
    stream.writeByte(ext->getExtHeaderLength());
    stream.writeByte(ext->getPduType());
    stream.writeByte(ext->getQfi());
    stream.writeByte(ext->getNextExtensionHeaderType());
}

const Ptr<Chunk> GtpUserExtHeaderPduSessionContainerSerializer::deserialize(MemoryInputStream& stream) const
{
    auto ext = makeShared<GtpUserExtHeaderPduSessionContainer>();
    ext->setExtHeaderLength(stream.readByte());
    ext->setPduType(stream.readByte());
    ext->setQfi(stream.readByte());
    ext->setNextExtensionHeaderType(stream.readByte());
    return ext;
}
