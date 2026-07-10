# 24 — THE root cause: TsnEtherApp.ned was missing @class(TsnEtherApp)

## The single line that was defeating everything

`src/apps/l2/TsnEtherApp.ned` declared:

```ned
simple TsnEtherApp extends L2App
{
    parameters:
        int vid = default(1);
        int pcp = default(0);
        @display("i=block/app2");
}
```

with **no `@class` property**. In OMNeT++, a `simple` module's C++ backing
class is given by its `@class` property, which **defaults to the module's own
NED name only for a base module**. When a simple module *extends* another,
`@class` is **inherited from the parent**. So `simple TsnEtherApp extends
L2App` inherited `@class(L2App)` — meaning the NED type `TsnEtherApp` was
backed by the **C++ class `L2App`**, not `TsnEtherApp`.

## Why this hid for so long

- `l2App[0].typename = "TsnEtherApp"` created a module of NED type
  `TsnEtherApp`, but its C++ object was a plain `L2App`.
- Therefore `L2App::buildFrame()` ran (packet named `EthFrame`, tagged
  `Protocol::ethernetMac`), and `TsnEtherApp::buildFrame()` /
  `TsnEtherApp::initialize()` **never executed even once**.
- Every fix attempted in `TsnEtherApp.cc` across entries 16-23 (the unified
  frame, the FCS placeholder, the `ieee8022llc` tag, etc.) was therefore
  **dead code** — it lived in a class that was never instantiated. The
  `L2App` base kept producing a frame tagged `ethernetMac`, and
  `EthernetEncapsulation` kept throwing "Unknown protocol: ethernetmac".
- The OMNeT++ event banner `** Event #50 ... server.l2App[0] (TsnEtherApp,
  id=35)` shows the **NED type name** (`TsnEtherApp`), which actively masked
  the fact that the underlying C++ class was `L2App`. This is why the logs
  looked correct.

## How it was finally caught

Running the simulation via Cmdenv and inspecting the actual packet name in
the error: `undisposed object: ... server.tsnEncap.EthFrame`. The packet was
named **`EthFrame`** — a string that appears *only* in `L2App::buildFrame()`
(`new Packet("EthFrame")`), never in `TsnEtherApp::buildFrame()` (which uses
`"TsnEthFrame"`). That single-word discrepancy proved the base class was
running, which pointed straight at the `@class` inheritance.

## The fix

```ned
simple TsnEtherApp extends L2App
{
    parameters:
        @class(TsnEtherApp);   // REQUIRED -- otherwise inherits @class(L2App)
        int vid = default(1);
        int pcp = default(0);
        @display("i=block/app2");
}
```

Pure NED change — no recompilation needed (NED is parsed at runtime). After
adding it, the very next run built `TsnEthFrame` (proving
`TsnEtherApp::buildFrame()` now executes), the "Unknown protocol" error
disappeared entirely, and the frame flowed all the way from the server
through `tsnSwitch`, the UPF, the GTP-U tunnel, and into the gNB's PDCP
layer -- surfacing the *next* (genuinely different) bug at
`NRTxPdcpEntity::deliverPdcpPdu` (see entry 25).

## Lesson

Any `simple X extends Y` in this project that needs its own C++ class MUST
declare `@class(X)` explicitly. `L2App` itself is fine (it is a base module,
so its `@class` defaults to `L2App`); only the derived `TsnEtherApp` needed
the explicit property.
