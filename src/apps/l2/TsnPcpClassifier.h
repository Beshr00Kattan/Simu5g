//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: Layer B / PCP-based priority queueing.
// See doc/ethernet-pdu-session/12-pcp-priority-queue.md for the full
// baseline/modified/rationale documentation entry for this file.
//

#ifndef _TSNPCPCLASSIFIER_H_
#define _TSNPCPCLASSIFIER_H_

#include <inet/queueing/base/PacketClassifierBase.h>

// See TsnPcpClassifier.ned for the full rationale.
class TsnPcpClassifier : public inet::queueing::PacketClassifierBase
{
  protected:
    int highPriorityMinPcp_ = 4;

  protected:
    virtual void initialize(int stage) override;
    virtual int classifyPacket(inet::Packet *packet) override;
};

#endif
