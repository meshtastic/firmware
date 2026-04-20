# New Meshtastic Module

Guide for developing a new Meshtastic firmware module.

## Module Hierarchy

Choose the appropriate base class:

1. **`MeshModule`** — Raw base class. Override `wantPacket()` and `handleReceived()`. Returns `ProcessMessage::STOP` or `ProcessMessage::CONTINUE`.
2. **`SinglePortModule`** — Handles a single `meshtastic_PortNum`. Constructor takes `(name, portNum)`. Simplified `wantPacket()` checking `decoded.portnum`. Use `allocDataPacket()` to create outgoing packets.
3. **`ProtobufModule<T>`** — Template for protobuf-encoded modules. Constructor takes `(name, portNum, fields)`. Override `handleReceivedProtobuf()`. Use `allocDataProtobuf(payload)` to create outgoing packets.

Most modules also mix in `concurrency::OSThread` for periodic background tasks.

## Implementation Pattern

```cpp
// src/modules/MyModule.h
#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

class MyModule : public ProtobufModule<meshtastic_MyMessage>, private concurrency::OSThread
{
  public:
    MyModule();

  protected:
    // Process incoming protobuf packet. Return true to stop further processing.
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MyMessage *msg) override;

    // Generate response packet (optional)
    virtual meshtastic_MeshPacket *allocReply() override;

    // Periodic task — return next run interval in ms, or disable()
    virtual int32_t runOnce() override;

    // Modify packet in-flight before delivery (optional)
    virtual bool alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_MyMessage *msg);

    // Request a UI display frame (optional)
    virtual bool wantUIFrame();
};
```

## Registration

Register in `src/modules/Modules.cpp` inside `setupModules()`:

```cpp
#if !MESHTASTIC_EXCLUDE_MYMODULE
    new MyModule();
#endif
```

If other code needs to reference the module instance:

```cpp
#if !MESHTASTIC_EXCLUDE_MYMODULE
    myModule = new MyModule();
#endif
```

And declare the global in the header:

```cpp
extern MyModule *myModule;
```

Some modules also conditionally instantiate based on `moduleConfig`:

```cpp
#if !MESHTASTIC_EXCLUDE_MYMODULE
    if (moduleConfig.has_my_module && moduleConfig.my_module.enabled) {
        new MyModule();
    }
#endif
```

## Conditional Compilation

Add a `MESHTASTIC_EXCLUDE_MYMODULE` guard. This allows the module to be excluded from constrained builds. The flag name must follow the pattern: `MESHTASTIC_EXCLUDE_` + uppercase module name.

## Protobuf Messages (if needed)

1. Define messages in `protobufs/meshtastic/` (e.g., `mymodule.proto`)
2. Add a `.options` file for nanopb field size constraints
3. Regenerate with `bin/regen-protos.sh`
4. Generated code appears in `src/mesh/generated/meshtastic/`
5. Assign a `meshtastic_PortNum` if the module uses a new port number

## Timing and Defaults

Use `Default` class helpers for configurable intervals:

```cpp
int32_t MyModule::runOnce()
{
    uint32_t interval = Default::getConfiguredOrDefaultMs(moduleConfig.my_module.update_interval,
                                                          default_my_module_interval);
    // ... do work ...
    return interval;
}
```

On public/default channels, enforce minimums with `Default::getConfiguredOrMinimumValue()`.

## Observer Pattern

Subscribe to system events:

```cpp
CallbackObserver<MyModule, const meshtastic::Status *> statusObserver =
    CallbackObserver<MyModule, const meshtastic::Status *>(this, &MyModule::handleStatusUpdate);
```

## Testing

Add test suite in `test/test_mymodule/`:

```text
test/
└── test_mymodule/
    └── test_main.cpp
```

Run with: `pio test -e native`

## Checklist

- [ ] Header and implementation files in `src/modules/`
- [ ] Inherit from appropriate base class (MeshModule / SinglePortModule / ProtobufModule)
- [ ] Mix in OSThread if periodic work is needed
- [ ] Register in `src/modules/Modules.cpp` with `MESHTASTIC_EXCLUDE_` guard
- [ ] Add protobuf definitions if needed (`protobufs/meshtastic/`)
- [ ] Use `Default::getConfiguredOrDefaultMs()` for timing
- [ ] Respect bandwidth limits on public channels
- [ ] Add test suite in `test/`
