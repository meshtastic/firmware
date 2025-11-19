# Meshtastic Firmware Development Guide

## Project Architecture

This is the **Meshtastic firmware** - an ESP32/nRF52/RP2040-based LoRa mesh networking project. Key architectural concepts:

### Modular Component System
- **Modules** (`src/modules/`): Self-contained features like GPS, WiFi, text messaging
- **Conditional compilation**: Use `MESHTASTIC_EXCLUDE_*` flags to disable features per variant
- **Platform abstraction**: Architecture-specific code in `arch/*/` and `platform/*/`
- **Variant system**: Hardware-specific configs in `variants/*/` with their own `platformio.ini`

### Build System (PlatformIO)
- Main config: `platformio.ini` with `default_envs = heltec-v3-custom`
- Environment hierarchy: `esp32_base → esp32s3_base → device_variant`
- Build commands: `pio run -e [environment]`, `pio run --target upload -e [environment]`
- Custom scripts: `bin/platformio-custom.py` adds build-time variables

### Module Development Patterns

**Module Registration** (in `src/modules/Modules.cpp`):
```cpp
#if !MESHTASTIC_EXCLUDE_MYMODULE
#include "modules/MyModule.h"
void setupModules() {
    if (moduleConfig.has_my_module && moduleConfig.my_module.enabled) {
        myModule = new MyModule();
    }
}
#endif
```

**Module Base Classes**:
- `MeshModule`: Core mesh packet handling
- `SinglePortModule`: Single protobuf port number
- `ProtobufModule`: Handles specific protobuf message types

### Critical Development Workflows

**Adding New Hardware Variant**:
1. Create `variants/[arch]/[variant-name]/` directory
2. Add `platformio.ini` with build flags and pin definitions
3. Define hardware in `variant.h` 
4. Add conditional compilation guards: `#if defined(VARIANT_my_variant)`

**Debugging**:
- Monitor: `pio device monitor -e [environment]`
- Log levels: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`
- Memory debugging: Uncomment `#-DDEBUG_HEAP=1` in platformio.ini

**Custom Module Development**:
- Inherit from appropriate base class (`SinglePortModule` most common)
- Implement `handleReceived()` and `runOnce()` 
- Add conditional compilation with `MESHTASTIC_EXCLUDE_*` pattern
- Register in `Modules.cpp` setupModules()

## Project-Specific Conventions

### Preprocessor Patterns
- Feature exclusion: `#if !MESHTASTIC_EXCLUDE_FEATURE`
- Architecture checks: `#ifdef ARCH_ESP32` 
- Hardware variants: `#if defined(VARIANT_heltec_v3_custom)`
- Platform capabilities: `#if HAS_WIFI`, `#if HAS_BLUETOOTH`

### Key Directories
- `src/modules/`: All mesh network modules and features
- `src/mesh/`: Core mesh networking, routing, and protocol handling
- `src/graphics/`: Display drivers and UI components
- `arch/`: Platform-specific implementations (ESP32, nRF52, etc.)
- `variants/`: Hardware-specific pin definitions and configurations

### Configuration Management
- Configs stored in protobuf format (`src/mesh/generated/meshtastic/`)
- Module configs: `moduleConfig.has_my_module` pattern for conditional features
- Device configs: `config.device.*` for device-wide settings
- Persistent storage in flash via NodeDB

### Threading Model
- Main modules run as OSThread instances (`concurrency/OSThread.h`)
- Periodic tasks use `concurrency/Periodic.h`
- Thread-safe messaging via queues and observers
- Power management integrates with PowerFSM state machine

## Integration Points

### Radio Interface
- Multiple radio drivers: `SX1262Interface`, `RF95Interface`, etc.
- All extend `RadioInterface` base class
- Packet routing through `FloodingRouter` and `ReliableRouter`

### Protocol Buffers
- Generated classes in `src/mesh/generated/meshtastic/`
- Custom message types require updating `.proto` files
- Use nanopb library for embedded protobuf handling

### External Dependencies
- **LovyanGFX**: Graphics library for displays (used in custom variants)
- **RadioLib**: Radio driver abstraction layer
- **ArduinoThread**: Threading primitives
- Hardware libs pulled via PlatformIO lib_deps in variant configs

## CustomUI Module Guidelines

### Important Constraints
- **ONLY modify code within `src/modules/CustomUI/`** - this is a specialized extension module
- **Never modify core Meshtastic files** unless absolutely critical and approved
- CustomUI is built for `heltec-v3-custom` variant with internal display disabled (`HAS_SCREEN=0`)
- Uses external ST7789 display via LovyanGFX library with modular screen-based architecture

### CustomUI Architecture
- **Modular initializers**: `init/InitDisplay.h`, `init/InitKeypad.h` handle hardware setup
- **Screen-based UI**: `screens/HomeScreen.h`, `screens/WiFiListScreen.h`, etc. manage display logic
- **Base classes**: Inherit from `InitBase` for initializers, `BaseScreen` for screens
- **Compilation guard**: All CustomUI code wrapped in `#if defined(VARIANT_heltec_v3_custom)`

### Change Management Workflow

**BEFORE making ANY changes:**
1. **Code Flow Analysis**: Trace through the relevant code paths and dependencies
2. **Root Cause Analysis (RCA)**: Identify the actual problem and potential side effects
3. **Approach Proposal**: Present multiple solution options with pros/cons
4. **User Confirmation**: Wait for explicit approval before editing files
5. **Surgical Implementation**: Make minimal, targeted changes only

**Example Analysis Pattern:**
```
🔍 ANALYSIS:
- Issue: [describe problem]
- Code flow: [trace relevant functions/files]
- Root cause: [actual underlying issue]
- Impact: [what else might be affected]

💡 PROPOSED APPROACHES:
1. Option A: [approach + pros/cons]
2. Option B: [approach + pros/cons]

📋 IMPLEMENTATION PLAN:
- Files to modify: [specific files]
- Changes needed: [specific modifications]
- Testing approach: [verification method]
```

## Development Tips

- **Memory constraints**: ESP32 has limited heap - prefer stack allocation
- **Power efficiency**: Most modules implement sleep/wake patterns  
- **Testing**: Use simulator variants for development (`portduino` target)
- **Custom variants**: See `variants/esp32s3/heltec-v3-custom/` as reference implementation
- **CustomUI debugging**: Use `LOG_INFO("🔧 CUSTOM UI: ...")` pattern for module-specific logging