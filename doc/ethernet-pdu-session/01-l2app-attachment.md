# Documentation entry: L2App dispatcher attachment (resolves the open item from entry 00)

## Files: `src/apps/l2/L2App.{ned,h,cc}` (amendment), `TsnEtherApp.cc` (amendment)

**Problem investigated:** `L2App`/`TsnEtherApp` cannot be wired via the existing
`app[]`-vector mechanism (`Ue.ned`/INET's `ApplicationLayerNodeBase`), because that
vector's gates (`socketOut`/`socketIn`) feed the **transport-layer** dispatcher (`at`),
and an Ethernet PDU session has no transport layer above it at all. The right
attachment point is the same one `inet::Ipv4NetworkLayer` uses on the **network-layer**
dispatcher (`nl`): confirmed by reading `ApplicationLayerNodeBase.ned` /
`NetworkLayerNodeBase.ned` (both shared by INET's `StandardHost` and, via the same
naming convention, Simu5G's own `Ue.ned`) that both host types expose an `nl:
MessageDispatcher` sitting directly below `ipv4`/`ipv6` and directly above
`cellularNic`/`eth[]`.

Read `inet::MessageDispatcher::handleMessage()` (`IProtocolRegistrationListener.cc`,
`MessageDispatcher.cc`) to confirm it throws `"Unknown message"` for any packet that
carries none of: a `SocketReq`/`SocketInd`, a `DispatchProtocolReq` matched against a
protocol some module registered, or an `InterfaceReq` matched against an
`InterfaceTable` entry. This is not new project-specific plumbing to invent — it is
the real, already-existing INET dispatch contract every network-layer module honors.

**Resolution — two real INET mechanisms reused, not one new dispatch mechanism added:**
1. `L2App::initialize(INITSTAGE_NETWORK_LAYER)` calls
   `inet::registerProtocol(Protocol::ethernetMac, gate("out"), gate("in"))`. Confirmed
   this is the exact call `inet::Ipv4` makes for itself
   (`registerProtocol(Protocol::ipv4, gate("queueOut"), gate("queueIn"))`, `Ipv4.cc:111`)
   and `inet::Udp` makes on its network-facing side (`Udp.cc:119`). This tells the `nl`
   dispatcher "packets tagged protocol=ethernetMac travelling down leave via my `out`
   gate; indications for that protocol travelling up arrive at my `in` gate."
2. `L2App::initialize(INITSTAGE_APPLICATION_LAYER)` resolves `cellularNic`'s interface
   ID once via `IInterfaceTable::findInterfaceByName()` (the interface is already
   registered there by `IP2Nic::registerInterface()` — confirmed present from the
   earlier IP2Nic investigation, so no NIC-side change is needed), and every outgoing
   frame is tagged `pkt->addTagIfAbsent<InterfaceReq>()->setInterfaceId(interfaceId_)`
   — the identical idiom `inet::Ipv4::routePacket()` uses (`Ipv4.cc:433` etc.) to name
   its chosen egress `NetworkInterface` before handing a datagram to the dispatcher.

**Why (architectural justification):** the task requires "prefer extending existing
Simu5G/INET mechanisms over adding parallel bypass modules." The reference shim
avoided this whole question by adding brand-new `tsnIn`/`tsnOut` gates that bypass the
dispatcher entirely (Section 9.2/9.9 of the reference report: `ServerMacTsn`,
`gNodeBMacTsn` explicit `tsnIn`/`tsnOut` gates). Here, `L2App` is a real, ordinary
network-layer-level module from the dispatcher's point of view — it just happens to
carry Ethernet frames instead of IP datagrams.
