# Documentation entry: LteMacUe registration of PduSessionType

## File: `src/stack/mac/layer/LteMacUe.cc`
**Baseline (confirmed by direct read, lines 207-225):**
```cpp
IInterfaceTable *interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
NetworkInterface * iface = interfaceTable->findInterfaceByName(par("interfaceName").stringValue());
if(iface == nullptr) throw new cRuntimeError(...);
auto ipv4if = iface->getProtocolData<Ipv4InterfaceData>();
if(ipv4if == nullptr) throw new cRuntimeError(...);
binder_->setMacNodeId(ipv4if->getIPAddress(), nodeId_);

// for emulation mode
const char* extHostAddress = getAncestorPar("extHostAddress").stringValue();
if (strcmp(extHostAddress, "") != 0) {
    binder_->setMacNodeId(Ipv4Address(extHostAddress), nodeId_);
}
```

**Modified/new code:** two lines inserted directly after the `setMacNodeId(ipv4if...)`
call:
```cpp
const char *pduSessionTypeStr = getAncestorPar("pduSessionType").stringValue();
binder_->setPduSessionType(nodeId_,
        (strcmp(pduSessionTypeStr, "Ethernet") == 0) ? PduSessionType::ETHERNET : PduSessionType::IPV4);
```

**Statement-by-statement:**
- `getAncestorPar("pduSessionType")`: reads the `pduSessionType` NED parameter declared
  on `Ue.ned` (the UE's top-level module), using the exact same `getAncestorPar()`
  idiom the pre-existing `extHostAddress` line two lines below already uses to reach a
  parameter declared on the ancestor module rather than on `LteMacUe` itself — no new
  parameter-lookup mechanism introduced.
- `strcmp(..., "Ethernet") == 0 ? ETHERNET : IPV4`: a simple string-to-enum mapping;
  defaults to `IPV4` for any other value (including the parameter's own default
  `"IPv4"`), matching `Binder::getPduSessionType()`'s own IPV4 fallback for
  consistency.
- `binder_->setPduSessionType(nodeId_, ...)`: registers the session type at exactly
  the same point in the UE's lifecycle (`initialize()`, after `nodeId_` and the UE's
  IP address are both known) that `setMacNodeId()` registers the IP↔MacNodeId
  mapping — i.e., "attach" for both concerns happens together, in one place.

**Why:** per plan scope decision 1, this is the "configure once, register at attach"
modeling of PDU session type, deliberately reusing the exact call site and idiom of
the pre-existing MacNodeId registration rather than adding a new init hook or a NAS
message exchange.

**Replaces:** nothing in the reference shim (it had no session-type concept).
