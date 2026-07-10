//
//                  Simu5G
//
// Real 3GPP Ethernet PDU Session work: network-wide, tag-independent
// packet loss measurement. See
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

package simu5g.common.packetLossObserver;

//
// Subscribes, at the SYSTEM MODULE level (see PacketDelayObserver.ned's
// identical rationale), to "appPacketSent" / "appPacketReceived" -- a plain
// per-packet counter tick (value=1L), emitted by any app in the network.
// Counts totals network-wide and reports loss count/percentage at finish().
//
simple PacketLossObserver {
    parameters:
        @signal[totalPacketLoss](type=long);
        @signal[packetLossPercentage](type=double);
        @statistic[totalPacketLoss](title="Total Packet Loss"; source=totalPacketLoss; record=last,sum; unit=packets);
        @statistic[packetLossPercentage](title="Packet Loss Percentage"; source=packetLossPercentage; record=last; unit=%);
}
