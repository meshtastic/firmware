# Meshtastic Firmware - Copilot Instructions

This document provides context and guidelines for AI assistants working with the Meshtastic firmware codebase.

## Project Overview

Meshtastic is an open-source LoRa mesh networking project for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware enables text messaging, location sharing, and telemetry over a decentralized mesh network.

### Supported Hardware Platforms

- **ESP32** (ESP32, ESP32-S3, ESP32-C3) - Most common platform
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
#define default_mqtt_address "mqtt.mess.host"
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
│   │   ├── *Interface.*   # Radio interface implementations
│   │   └── generated/     # Protobuf generated code
│   ├── modules/           # Feature modules (Position, Telemetry, etc.)
│   ├── gps/               # GPS handling
│   ├── graphics/          # Display drivers and UI
│   ├── platform/          # Platform-specific code
│   ├── input/             # Input device handling
│   └── concurrency/       # Threading utilities
├── variants/              # Hardware variant definitions
│   ├── esp32/            # ESP32 variants
│   ├── esp32s3/          # ESP32-S3 variants
│   ├── nrf52/            # nRF52 variants
│   └── rp2xxx/           # RP2040/RP2350 variants
├── protobufs/            # Protocol buffer definitions
├── boards/               # Custom PlatformIO board definitions
└── bin/                  # Build and utility scripts
```

## Coding Conventions

### General Style

- Follow existing code style - run `trunk fmt` before commits
- Prefer `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` for logging
- Use `assert()` for invariants that should never fail

### Naming Conventions

- Classes: `PascalCase` (e.g., `PositionModule`, `NodeDB`)
- Functions/Methods: `camelCase` (e.g., `sendOurPosition`, `getNodeNum`)
- Constants/Defines: `UPPER_SNAKE_CASE` (e.g., `MAX_INTERVAL`, `ONE_DAY`)
- Member variables: `camelCase` (e.g., `lastGpsSend`, `nodeDB`)
- Config defines: `USERPREFS_*` for user-configurable options

### Key Patterns

#### Module System

Modules inherit from `MeshModule` or `ProtobufModule<T>` and implement:

- `handleReceivedProtobuf()` - Process incoming packets
- `allocReply()` - Generate response packets
- `runOnce()` - Periodic task execution (returns next run interval in ms)

```cpp
class MyModule : public ProtobufModule<meshtastic_MyMessage>
{
  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MyMessage *msg) override;
    virtual int32_t runOnce() override;
};
```

#### Configuration Access

- `config.*` - Device configuration (LoRa, position, power, etc.)
- `moduleConfig.*` - Module-specific configuration
- `channels.*` - Channel configuration and management

#### Default Values

Use the `Default` class helpers in `src/mesh/Default.h`:

- `Default::getConfiguredOrDefaultMs(configured, default)` - Returns ms, using default if configured is 0
- `Default::getConfiguredOrMinimumValue(configured, min)` - Enforces minimum values
- `Default::getConfiguredOrDefaultMsScaled(configured, default, numNodes)` - Scales based on network size

#### Thread Safety

- Use `concurrency::Lock` for mutex protection
- Radio SPI access uses `SPILock`
- Prefer `OSThread` for background tasks

### Hardware Variants

Each hardware variant has:

- `variant.h` - Pin definitions and hardware capabilities
- `platformio.ini` - Build configuration
- Optional: `pins_arduino.h`, `rfswitch.h`

Key defines in variant.h:

```cpp
#define USE_SX1262          // Radio chip selection
#define HAS_GPS 1           // Hardware capabilities
#define LORA_CS 36          // Pin assignments
#define SX126X_DIO1 14      // Radio-specific pins
```

### Protobuf Messages

- Defined in `protobufs/meshtastic/*.proto`
- Generated code in `src/mesh/generated/`
- Regenerate with `bin/regen-protos.sh`
- Message types prefixed with `meshtastic_`

### Conditional Compilation

```cpp
#if !MESHTASTIC_EXCLUDE_GPS        // Feature exclusion
#ifdef ARCH_ESP32                   // Architecture-specific
#if defined(USE_SX1262)            // Radio-specific
#ifdef HAS_SCREEN                   // Hardware capability
#if USERPREFS_EVENT_MODE           // User preferences
```

## Build System

Uses **PlatformIO** with custom scripts:

- `bin/platformio-pre.py` - Pre-build script
- `bin/platformio-custom.py` - Custom build logic

Build commands:

```bash
pio run -e tbeam              # Build specific target
pio run -e tbeam -t upload    # Build and upload
pio run -e native             # Build native/Linux version
```

## Common Tasks

### Adding a New Module

1. Create `src/modules/MyModule.cpp` and `.h`
2. Inherit from appropriate base class
3. Register in `src/modules/Modules.cpp`
4. Add protobuf messages if needed in `protobufs/`

### Adding a New Hardware Variant

1. Create directory under `variants/<arch>/<name>/`
2. Add `variant.h` with pin definitions
3. Add `platformio.ini` with build config
4. Reference common configs with `extends`

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

- Unit tests in `test/` directory
- Run with `pio test -e native`
- Use `bin/test-simulator.sh` for simulation testing

## Resources

- [Documentation](https://meshtastic.org/docs/)
