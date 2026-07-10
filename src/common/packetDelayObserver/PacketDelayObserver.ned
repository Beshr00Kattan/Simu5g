//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: network-wide, tag-independent
// end-to-end delay measurement. See
// doc/ethernet-pdu-session/28-packet-observers.md for the full
// baseline/modified/rationale documentation entry for this file.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

package simu5g.common.packetDelayObserver;

//
// Subscribes, at the SYSTEM MODULE (whole-simulation) level, to
// "packetDelaySentSignal" / "packetDelayReceivedSignal" -- emitted by any
// app in the network (CbrSender/CbrReceiver, TsnEtherApp) with the sent
// packet's own real, serialized frame/sequence id as the signal value, not a
// Packet-level tag. Signals bubble up the component tree to the system
// module regardless of which node emitted them, so this module needs no
// wiring to the apps it observes -- it can be placed anywhere in the
// network (one instance is enough for the whole simulation).
//
simple PacketDelayObserver {
    parameters:
        @display("i=block/clock;bgb=200,60");
        @signal[packetDelayByObserver](type=simtime_t);
        @statistic[packetDelayByObserver](
            title="End-to-End Packet Delay (By Observer)";
            source=packetDelayByObserver;
            record=vector, histogram, mean, sum;
            unit=s;
        );
        @signal[packetsSentByObserver](type=long);
        @statistic[packetsSentByObserver](
            title="Packets Sent (By Observer)";
            source=packetsSentByObserver;
            record=last;
        );
        @signal[packetsReceivedByObserver](type=long);
        @statistic[packetsReceivedByObserver](
            title="Packets Received (By Observer)";
            source=packetsReceivedByObserver;
            record=last;
        );
}
