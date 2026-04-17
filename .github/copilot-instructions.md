# Meshtastic Firmware - Copilot Instructions

This document provides context and guidelines for AI assistants working with the Meshtastic firmware codebase.

## Project Overview

Meshtastic is an open-source LoRa mesh networking project for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware enables text messaging, location sharing, and telemetry over a decentralized mesh network. The project uses **C++17** as its language standard across all platforms.

### Supported Hardware Platforms

- **ESP32** (ESP32, ESP32-S3, ESP32-C3, ESP32-C6) - Most common platform
- **nRF52** (nRF52840, nRF52833) - Low power Nordic chips
- **RP2040/RP2350** - Raspberry Pi Pico variants
- **STM32WL** - STM32 with integrated LoRa
- **Linux/Portduino** - Native Linux builds (Raspberry Pi, etc.)

### Supported Radio Chips

- **SX1262/SX1268** - Sub-GHz LoRa (868/915 MHz regions)
- **SX1280** - 2.4 GHz LoRa
- **LR1110/LR1120/LR1121** - Wideband radios (sub-GHz and 2.4 GHz capable, but not simultaneously)
- **RF95** - Legacy RFM95 modules
- **LLCC68** - Low-cost LoRa

### MQTT Integration

MQTT provides a bridge between Meshtastic mesh networks and the internet, enabling nodes with network connectivity to share messages with remote meshes or external services.

#### Key Components

- **`src/mqtt/MQTT.cpp`** - Main MQTT client singleton, handles connection and message routing
- **`src/mqtt/ServiceEnvelope.cpp`** - Protobuf wrapper for mesh packets sent over MQTT
- **`moduleConfig.mqtt`** - MQTT module configuration

#### MQTT Topic Structure

Messages are published/subscribed using a hierarchical topic format:

```
{root}/{channel_id}/{gateway_id}
```

- `root` - Configurable prefix (default: `msh`)
- `channel_id` - Channel name/identifier
- `gateway_id` - Node ID of the publishing gateway

#### Configuration Defaults (from `Default.h`)

```cpp
#define default_mqtt_address "mqtt.meshtastic.org"
#define default_mqtt_username "meshdev"
#define default_mqtt_password "large4cats"
#define default_mqtt_root "msh"
#define default_mqtt_encryption_enabled true
#define default_mqtt_tls_enabled false
```

#### Key Concepts

- **Uplink** - Mesh packets sent TO the MQTT broker (controlled by `uplink_enabled` per channel)
- **Downlink** - MQTT messages received and injected INTO the mesh (controlled by `downlink_enabled` per channel)
- **Encryption** - When `encryption_enabled` is true, only encrypted packets are sent; plaintext JSON is disabled
- **ServiceEnvelope** - Protobuf wrapper containing packet + channel_id + gateway_id for routing
- **JSON Support** - Optional JSON encoding for integration with external systems (disabled on nRF52 by default)

#### PKI Messages

PKI (Public Key Infrastructure) messages have special handling:

- Accepted on a special "PKI" channel
- Allow encrypted DMs between nodes that discovered each other on downlink-enabled channels

## Project Structure

```
firmware/
├── src/                    # Main source code
│   ├── main.cpp           # Application entry point
│   ├── mesh/              # Core mesh networking
│   │   ├── NodeDB.*       # Node database management
│   │   ├── Router.*       # Packet routing
│   │   ├── Channels.*     # Channel management
│   │   ├── CryptoEngine.* # AES-CCM encryption
│   │   ├── *Interface.*   # Radio interface implementations
│   │   ├── api/           # WiFi/Ethernet server APIs (ServerAPI, PacketAPI)
│   │   ├── http/          # HTTP server (WebServer, ContentHandler)
│   │   ├── wifi/          # WiFi support (WiFiAPClient)
│   │   ├── eth/           # Ethernet support (ethClient)
│   │   ├── udp/           # UDP multicast
│   │   ├── compression/   # Message compression (unishox2)
│   │   └── generated/     # Protobuf generated code
│   ├── modules/           # Feature modules (Position, Telemetry, etc.)
│   │   └── Telemetry/     # Telemetry subsystem
│   │       └── Sensor/    # 50+ I2C sensor drivers
│   ├── gps/               # GPS handling
│   ├── graphics/          # Display drivers and UI
│   │   └── niche/         # Specialized UIs (InkHUD e-ink framework)
│   ├── platform/          # Platform-specific code (esp32, nrf52, rp2xx0, stm32wl, portduino)
│   ├── input/             # Input device handling (InputBroker, keyboards, buttons)
│   ├── detect/            # I2C hardware auto-detection (80+ device types)
│   ├── motion/            # Accelerometer drivers (BMA423, BMI270, MPU6050, etc.)
│   ├── mqtt/              # MQTT bridge client
│   ├── power/             # Power HAL
│   ├── nimble/            # BLE via NimBLE
│   ├── buzz/              # Audio/notification (buzzer, RTTTL)
│   ├── serialization/     # JSON serialization, COBS encoding
│   ├── watchdog/          # Hardware watchdog thread
│   ├── concurrency/       # Threading utilities (OSThread, Lock)
│   ├── PowerFSM.*         # Power finite state machine
│   └── Observer.h         # Observer/Observable event pattern
├── variants/              # Hardware variant definitions
│   ├── esp32/            # ESP32 variants
│   ├── esp32s3/          # ESP32-S3 variants
│   ├── esp32c3/          # ESP32-C3 variants
│   ├── esp32c6/          # ESP32-C6 variants
│   ├── nrf52840/         # nRF52 variants
│   ├── rp2040/           # RP2040/RP2350 variants
│   ├── stm32/            # STM32WL variants
│   └── native/           # Linux/Portduino variants
├── protobufs/            # Protocol buffer definitions
├── boards/               # Custom PlatformIO board definitions
├── test/                 # Unit tests (12 test suites)
└── bin/                  # Build and utility scripts
```

## Coding Conventions

### General Style

- Follow existing code style - run `trunk fmt` before commits
- Prefer `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` for logging
- Use `assert()` for invariants that should never fail
- C++17 features are available (`std::optional`, structured bindings, `if constexpr`, etc.)

### Naming Conventions

- Classes: `PascalCase` (e.g., `PositionModule`, `NodeDB`)
- Functions/Methods: `camelCase` (e.g., `sendOurPosition`, `getNodeNum`)
- Constants/Defines: `UPPER_SNAKE_CASE` (e.g., `MAX_INTERVAL`, `ONE_DAY`)
- Member variables: `camelCase` (e.g., `lastGpsSend`, `nodeDB`)
- Config defines: `USERPREFS_*` for user-configurable options

### Key Patterns

#### Module System

Modules use a three-tier class hierarchy:

1. **`MeshModule`** - Base class. Implement `wantPacket()` and `handleReceived()`. Returns `ProcessMessage::STOP` or `ProcessMessage::CONTINUE`.
2. **`SinglePortModule`** - Handles a single portnum. Simplified `wantPacket()` that checks `decoded.portnum`.
3. **`ProtobufModule<T>`** - Template for protobuf-based modules. Handles encoding/decoding automatically.

Most modules also inherit from **`OSThread`** for periodic tasks (the "mixin" pattern):

```cpp
class MyModule : public ProtobufModule<meshtastic_MyMessage>, private concurrency::OSThread
{
  public:
    MyModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MyMessage *msg) override;
    virtual meshtastic_MeshPacket *allocReply() override;       // Generate response packets
    virtual int32_t runOnce() override;                         // Periodic task (returns next interval in ms)
    virtual bool alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_MyMessage *msg); // Modify in-flight
    virtual bool wantUIFrame();                                 // Request a UI display frame
};
```

Modules are registered in `src/modules/Modules.cpp` guarded by `MESHTASTIC_EXCLUDE_*` flags.

#### Observer/Observable Pattern

Event-driven communication between subsystems uses `src/Observer.h`:

```cpp
// Observable emits events
Observable<const meshtastic::Status *> newStatus;
newStatus.notifyObservers(&status);

// Observer receives events via callback
CallbackObserver<MyClass, const meshtastic::Status *> statusObserver =
    CallbackObserver<MyClass, const meshtastic::Status *>(this, &MyClass::handleStatusUpdate);
```

#### Configuration Access

- `config.*` - Device configuration (LoRa, position, power, etc.)
- `moduleConfig.*` - Module-specific configuration
- `channels.*` - Channel configuration and management
- `owner` - Device owner info
- `myNodeInfo` - Local node info

#### Default Values

Use the `Default` class helpers in `src/mesh/Default.h`:

- `Default::getConfiguredOrDefaultMs(configured, default)` - Returns ms, using default if configured is 0
- `Default::getConfiguredOrDefault(configured, default)` - Generic configured/default getter
- `Default::getConfiguredOrMinimumValue(configured, min)` - Enforces minimum values
- `Default::getConfiguredOrDefaultMsScaled(configured, default, numNodes)` - Scales based on network size

#### Thread Safety

- Use `concurrency::Lock` and `concurrency::LockGuard` for mutex protection
- Radio SPI access uses `SPILock`
- Prefer `OSThread` for background tasks

### Hardware Detection

`src/detect/ScanI2C` automatically enumerates 80+ I2C device types at boot including displays, sensors, RTCs, keyboards, PMUs, and touch controllers. This drives automatic initialization of the correct drivers.

### Graphics/UI System

Multiple display driver families in `src/graphics/`:

- **OLED**: SSD1306, SH1106, ST7567
- **TFT**: TFTDisplay (LovyanGFX-based)
- **E-Ink**: EInkDisplay2, EInkDynamicDisplay, EInkParallelDisplay

**InkHUD** (`src/graphics/niche/InkHUD/`) is an event-driven e-ink UI framework:

- Applet-based architecture — modular display tiles
- Read-only, static display optimized for minimal refreshes and low power
- Configured per-variant via `nicheGraphics.h`
- Separate PlatformIO config: `src/graphics/niche/InkHUD/PlatformioConfig.ini`

### Input System

`src/input/InputBroker` is the centralized input event dispatcher. Supports multiple input sources: buttons, keyboards (BBQ10, Cardputer, TCA8418), touch screens, rotary encoders, and matrix keyboards.

### Power Management

`src/PowerFSM.*` implements a finite state machine with states: `stateON`, `statePOWER`, `stateSERIAL`, `stateDARK`. Key events: `EVENT_PRESS`, `EVENT_WAKE_TIMER`, `EVENT_LOW_BATTERY`, `EVENT_RECEIVED_MSG`, `EVENT_SHUTDOWN`. Conditionally excluded with `MESHTASTIC_EXCLUDE_POWER_FSM` (falls back to `FakeFsm`).

### Motion Sensors

`src/motion/AccelerometerThread` provides background motion monitoring with automatic screen wake and double-tap button press detection. Supports 10+ accelerometer/gyroscope chips (BMA423, BMI270, MPU6050, LIS3DH, LSM6DS3, STK8XXX, QMA6100P, ICM20948, BMX160).

### Telemetry Sensor Library

`src/modules/Telemetry/Sensor/` contains 50+ I2C sensor drivers organized by category:

- **Power monitoring**: INA219/226/260/3221, MAX17048
- **Environmental**: BME280/680, SCD4X (CO₂), SEN5X (particulate)
- **Humidity/Temperature**: SHT3X/4X, AHT10, MCP9808, MLX90614
- **Light**: BH1750, TSL2561/2591, VEML7700, LTR390UV, OPT3001
- **Air quality**: PMSA003I, SFA30
- **Specialized**: CGRadSens (radiation), NAU7802 (weight scale)

### API/Networking

`src/mesh/api/` provides a template-based `ServerAPI` for client communication over WiFi (`WiFiServerAPI`) and Ethernet (`ethServerAPI`). Default port: **4403**. HTTP server in `src/mesh/http/`. JSON serialization in `src/serialization/MeshPacketSerializer`.

### Hardware Variants

Each hardware variant has:

- `variant.h` - Pin definitions and hardware capabilities
- `platformio.ini` - Build configuration
- Optional: `pins_arduino.h`, `rfswitch.h`, `nicheGraphics.h` (for InkHUD variants)

Key defines in variant.h:

```cpp
#define USE_SX1262          // Radio chip selection
#define HAS_GPS 1           // Hardware capabilities
#define HAS_SCREEN 1        // Display present
#define LORA_CS 36          // Pin assignments
#define SX126X_DIO1 14      // Radio-specific pins
```

### Protobuf Messages

- Defined in `protobufs/meshtastic/*.proto` (~32 proto files)
- Generated code in `src/mesh/generated/meshtastic/`
- Regenerate with `bin/regen-protos.sh`
- Message types prefixed with `meshtastic_`
- Nanopb `.options` files control field sizes and encoding

### Conditional Compilation

```cpp
#if !MESHTASTIC_EXCLUDE_GPS        // Feature exclusion
#if !MESHTASTIC_EXCLUDE_WIFI       // Network feature exclusion
#if !MESHTASTIC_EXCLUDE_BLUETOOTH  // BLE exclusion
#if !MESHTASTIC_EXCLUDE_POWER_FSM  // Power FSM exclusion
#ifdef ARCH_ESP32                   // Architecture-specific
#ifdef ARCH_NRF52                   // Nordic platform
#ifdef ARCH_RP2040                  // Raspberry Pi Pico
#ifdef ARCH_PORTDUINO               // Linux native
#if defined(USE_SX1262)            // Radio-specific
#ifdef HAS_SCREEN                   // Hardware capability
#if USERPREFS_EVENT_MODE           // User preferences
```

## Build System

Uses **PlatformIO** with custom scripts:

- `bin/platformio-pre.py` - Pre-build script
- `bin/platformio-custom.py` - Custom build logic, manifest generation

Build commands:

```bash
pio run -e tbeam              # Build specific target
pio run -e tbeam -t upload    # Build and upload
pio run -e native             # Build native/Linux version
```

### Build Manifest

`bin/platformio-custom.py` emits a build manifest with metadata:

- `hasMui`, `hasInkHud` - UI capability flags (overridable via `custom_meshtastic_has_mui`, `custom_meshtastic_has_ink_hud`)
- Architecture normalization (e.g., `esp32s3` → `esp32-s3` for API compatibility)

## Common Tasks

### Adding a New Module

1. Create `src/modules/MyModule.cpp` and `.h`
2. Inherit from appropriate base class (`MeshModule`, `SinglePortModule`, or `ProtobufModule<T>`)
3. Mix in `concurrency::OSThread` if periodic work is needed
4. Register in `src/modules/Modules.cpp` guarded by `#if !MESHTASTIC_EXCLUDE_MYMODULE`
5. Add protobuf messages if needed in `protobufs/meshtastic/`
6. Add test suite in `test/test_mymodule/` if applicable

### Adding a New Hardware Variant

1. Create directory under `variants/<arch>/<name>/`
2. Add `variant.h` with pin definitions and hardware capability defines
3. Add `platformio.ini` with build config — use `extends` to reference common base (e.g., `esp32s3_base`)
4. Set `custom_meshtastic_support_level = 1` (PR builds) or `2` (merge builds)
5. For e-ink displays, add `nicheGraphics.h` for InkHUD configuration

### Adding a New Telemetry Sensor

1. Create driver in `src/modules/Telemetry/Sensor/` following existing sensor pattern
2. Register I2C address in `src/detect/ScanI2C` for auto-detection
3. Integrate with the appropriate telemetry module (Environment, Health, Power, AirQuality)
4. Add proto fields in `protobufs/meshtastic/telemetry.proto` if new data types are needed

### Modifying Configuration Defaults

- Check `src/mesh/Default.h` for default value defines
- Check `src/mesh/NodeDB.cpp` for initialization logic
- Consider `isDefaultChannel()` checks for public channel restrictions

## Important Considerations

### Traffic Management

The mesh network has limited bandwidth. When modifying broadcast intervals:

- Respect minimum intervals on default/public channels
- Use `Default::getConfiguredOrMinimumValue()` to enforce minimums
- Consider `numOnlineNodes` scaling for congestion control

### Power Management

Many devices are battery-powered:

- Use `IF_ROUTER(routerVal, normalVal)` for role-based defaults
- Check `config.power.is_power_saving` for power-saving modes
- Implement proper `sleep()` methods in radio interfaces

### Channel Security

- `channels.isDefaultChannel(index)` - Check if using default/public settings
- Default channels get stricter rate limits to prevent abuse
- Private channels may have relaxed limits

## GitHub Actions CI/CD

The project uses GitHub Actions extensively for CI/CD. Key workflows are in `.github/workflows/`:

### Core CI Workflows

- **`main_matrix.yml`** - Main CI pipeline, runs on push to `master`/`develop` and PRs
  - Uses `bin/generate_ci_matrix.py` to dynamically generate build targets
  - Builds all supported hardware variants
  - PRs build a subset (`--level pr`) for faster feedback

- **`trunk_check.yml`** - Code quality checks on PRs
  - Runs Trunk.io for linting and formatting
  - Must pass before merge

- **`tests.yml`** - End-to-end and hardware tests
  - Runs daily on schedule
  - Includes native tests and hardware-in-the-loop testing

- **`test_native.yml`** - Native platform unit tests
  - Runs `pio test -e native`

### Release Workflows

- **`release_channels.yml`** - Triggered on GitHub release publish
  - Builds Docker images
  - Packages for PPA (Ubuntu), OBS (openSUSE), and COPR (Fedora)
  - Handles Alpha/Beta/Stable release channels

- **`nightly.yml`** - Nightly builds from develop branch

- **`docker_build.yml`** / **`docker_manifest.yml`** - Docker image builds

### Build Matrix Generation

The CI uses `bin/generate_ci_matrix.py` to dynamically select which targets to build:

```bash
# Generate full build matrix
./bin/generate_ci_matrix.py all

# Generate PR-level matrix (subset for faster builds)
./bin/generate_ci_matrix.py all --level pr
```

Variants can specify their support level in `platformio.ini`:

- `custom_meshtastic_support_level = 1` - Actively supported, built on every PR
- `custom_meshtastic_support_level = 2` - Supported, built on merge to main branches
- `board_level = extra` - Extra builds, only on full releases

### Running Workflows Locally

Most workflows can be triggered manually via `workflow_dispatch` for testing.

## Testing

Unit tests in `test/` directory with 12 test suites:

- `test_crypto/` - Cryptography
- `test_mqtt/` - MQTT integration
- `test_radio/` - Radio interface
- `test_mesh_module/` - Module framework
- `test_meshpacket_serializer/` - Packet serialization
- `test_transmit_history/` - Retransmission tracking
- `test_atak/` - ATAK integration
- `test_default/` - Default configuration
- `test_http_content_handler/` - HTTP handling
- `test_serial/` - Serial communication

Run with: `pio test -e native`

Simulation testing: `bin/test-simulator.sh`

## Resources

- [Documentation](https://meshtastic.org/docs/)
