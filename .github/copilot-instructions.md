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
- **macOS native** - Headless `meshtasticd` on Apple Silicon / x86_64; see `variants/native/portduino/platformio.ini` for Homebrew prereqs + CH341 LoRa setup

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

## Encryption & Key Management

Meshtastic packets on the air are typically encrypted one of two ways: the **per-channel symmetric** layer (AES-CTR with a shared PSK) for broadcasts and channel traffic, and the **per-peer PKI** layer (X25519 ECDH → AES-256-CCM) for direct messages and remote admin. A channel with a 0-byte PSK (or Ham mode, which wipes PSKs) transmits cleartext — see the size table below. Both are implemented in `src/mesh/CryptoEngine.cpp`; the send/receive dispatch lives in `src/mesh/Router.cpp`; admin authorization lives in `src/modules/AdminModule.cpp`.

### High-level model

- **Channels** are symmetric rooms: anyone with the PSK can read any message on the channel. Channel 0 is the "primary" channel and ships with the short-form default PSK on factory devices, forming the public mesh most users join. (The LoRa modem preset `LONG_FAST` lives on `config.lora.modem_preset` and is an independent field — don't conflate "channel 0 default PSK" with the modem preset name.)
- **DMs** addressed to a single node require PKI so that other holders of the channel PSK can't read them. Outside Ham mode, Meshtastic does not fall back to channel-symmetric encryption when the destination public key is unknown.
- **Remote admin** is a DM carrying an `AdminMessage`. The receiver only acts on it if the sender's public key is on its allowlist (`config.security.admin_key[0..2]`).
- **Ham mode** (`owner.is_licensed=true`, where `owner` is the local `meshtastic_User` record) disables PKI entirely and sends cleartext — FCC Part 97 prohibits encryption on amateur bands.
- **No ratchet, no session.** Every packet is encrypted from scratch — a stateless design that matches the high-loss, store-and-forward nature of LoRa.

### Symmetric channel encryption (AES-CTR)

`CryptoEngine::encryptPacket` / `decrypt` / `encryptAESCtr` in `src/mesh/CryptoEngine.cpp`.

- **Cipher**: AES-CTR, AES-128 or AES-256 depending on key length. Same routine in both directions (CTR is a stream cipher, so encrypt == decrypt).
- **Key**: `ChannelSettings.psk` bytes. Size semantics:
  - **0 bytes** → no encryption, cleartext on the air
  - **1 byte** → short-form index into the well-known `defaultpsk[]` in `src/mesh/Channels.h`. Index 0 = cleartext; 1 = defaultpsk unchanged; 2..255 = defaultpsk with its last byte incremented by (index − 1). This is what the CLI's `--ch-set psk default` produces.
  - **16 bytes** → raw AES-128 key
  - **32 bytes** → raw AES-256 key
  - **2..15 bytes** → zero-padded to 16 and used as AES-128 (with a warn log); **17..31 bytes** → zero-padded to 32 and used as AES-256 (with a warn log). Defensive fallback for malformed PSK input, not something to rely on.
- **Nonce (128 bit)**: `packet_id` (u64 LE) ‖ `from_node` (u32 LE) ‖ `block_counter` (u32, starts at 0). Built in `CryptoEngine::initNonce`.
- **No AEAD**: channel packets carry no MAC, so the channel-hash byte is not an integrity or authenticity check. `Channels::getHash` is a 1-byte XOR-derived hint over the channel name bytes and PSK bytes that helps receivers pick a candidate channel/PSK for decryption. Because it is only a small hint and collisions are easy to find, it should be described purely as a PSK-selection aid, not as a security filter an attacker cannot bypass.
- **Channel 0 is special in one way only**: it's the channel the Router attempts PKI decryption on before falling through to AES-CTR. Non-zero channels always go straight to AES-CTR.

### PKI encryption for DMs (X25519 ECDH + AES-256-CCM)

`CryptoEngine::encryptCurve25519` / `decryptCurve25519` in `src/mesh/CryptoEngine.cpp`.

- **Keypair**: Curve25519 (aka X25519), 32-byte public + 32-byte private. Stored in `config.security.public_key` / `private_key`; the public half is mirrored into `owner.public_key` so it rides along in NodeInfo broadcasts and propagates through the mesh like any other identity field.
- **Key generation** (`generateKeyPair`): stirs `HardwareRNG::fill()` (64 B from platform TRNG when available), the 16-byte `myNodeInfo.device_id`, and a call to `random()` into the rweather/Crypto library's software RNG, then `Curve25519::dh1`. `regeneratePublicKey` recomputes the public half from a known private (used when restoring from backup).
- **Keygen entry points**: at boot, `NodeDB` calls `generateKeyPair` (or `regeneratePublicKey` when a stored private key is present and passes a low-entropy check) **directly** when `!owner.is_licensed` and `config.lora.region != UNSET`. `ensurePkiKeys` wraps the same logic for runtime/admin flows — it's the path `AdminModule::handleSetConfig` runs when first assigning a valid region or when security config is written; **do not assume it's the universal boot-time gate**, because the NodeDB path bypasses it.
- **Handshake**: `Curve25519::dh2(local_private, remote_public) → 32-byte shared secret → SHA-256 → 32-byte AES-256 key`. Recomputed per packet. The SHA-256 step is effectively a KDF over the raw ECDH output.
- **Cipher**: AES-256-CCM via `aes_ccm_ae` / `aes_ccm_ad` (`src/mesh/aes-ccm.cpp`). MAC length (the `M` parameter) is **8 bytes**. No AAD — the MAC covers ciphertext only.
- **Nonce (13 bytes / 104 bit)**: `aes_ccm_ae`/`aes_ccm_ad` use a 13-byte CCM nonce (`L = 2` is hardcoded in `src/mesh/aes-ccm.cpp`), not a 16-byte nonce. For PKI packets, `CryptoEngine::initNonce(fromNode, packetNum, extraNonce)` starts from the usual packet-derived nonce material, then overwrites nonce bytes `4..7` with a fresh 32-bit `extraNonce = random()`. The effective nonce bytes are therefore: bytes `0..3` = `packet_id`, bytes `4..7` = transmitted `extraNonce`, bytes `8..11` = `from_node`, byte `12` = `0x00`. The receiver reconstructs the same 13-byte nonce from the packet metadata plus the appended `extraNonce`.
- **Wire overhead**: 12 bytes appended to the ciphertext = 8-byte MAC ‖ 4-byte extraNonce. Defined as `MESHTASTIC_PKC_OVERHEAD = 12` in `src/mesh/RadioInterface.h`. Only the 4-byte `extraNonce` is sent; the rest of the 13-byte CCM nonce is reconstructed from packet fields as described above. The Router's send path checks this overhead against `MAX_LORA_PAYLOAD_LEN` before committing to PKI.
- **Send selection** (`Router::send`): the sender enters the PKI path when **all** hold — we're the originator AND not Ham mode AND not Portduino simradio AND not on the `serial`/`gpio` channels (unless the packet is already marked `pki_encrypted`) AND `config.security.private_key.size == 32` AND destination is a single node (not broadcast) AND the portnum isn't infrastructure. `TRACEROUTE_APP`, `NODEINFO_APP`, `ROUTING_APP`, and `POSITION_APP` are routed through channel encryption even when DMed (these need to be readable by relaying peers). Once on the PKI path, if the destination's public key isn't in our NodeDB the send **fails** with `PKI_SEND_FAIL_PUBLIC_KEY` — it does not silently fall back to channel encryption. If the client explicitly set `pki_encrypted=true` and any condition blocks PKI, the send fails with `PKI_FAILED`.
- **Receive selection** (`Router::perhapsDecode`): try PKI decrypt first when `channel == 0` AND `isToUs(p)` AND not broadcast AND both peers have public keys in NodeDB AND `rawSize > MESHTASTIC_PKC_OVERHEAD`. On success the packet gets `pki_encrypted=true` stamped and the sender's public key copied into `p->public_key` for downstream authorization.

### Remote admin authorization

Implemented in `src/modules/AdminModule.cpp` → `handleReceivedProtobuf`. The authorization check runs in this order:

1. **Response messages** — if `messageIsResponse(r)` is true (the payload is a response to one of our earlier admin requests), it's accepted without any further check. The in-file comment flags this as a known-untightened gap: a stricter implementation would remember which `public_key` we last queried and reject responses that don't match.
2. **Local admin** — `mp.from == 0` (phone app over BLE, serial CLI, internal module); never travels over the air. **Rejected** if `config.security.is_managed` is true, because managed devices expect admin to arrive over the air through an authorized remote path.
3. **Legacy admin channel (deprecated)** — the packet arrived on a channel named literally `"admin"`. Gated by `config.security.admin_channel_enabled`; returns `NOT_AUTHORIZED` if the flag is false. Kept for backward compatibility; new deployments should use PKI admin.
4. **PKI admin (preferred for remote)** — `mp.pki_encrypted == true` AND `mp.public_key` matches one of `config.security.admin_key[0..2]` (up to three authorized 32-byte Curve25519 public keys, typically copied from the admin node's own `user.public_key`).
5. **Fallthrough** → `NOT_AUTHORIZED`.

On top of authorization, any remote admin message that **mutates** state (not a request, not a response) also has to pass a session-key check (`checkPassKey`): the client must first pull a fresh 8-byte `session_passkey` via `get_admin_session_key_request`, then echo that passkey back in the mutating message. The device rotates the passkey after 150 s and rejects values older than 300 s — a narrow anti-replay window on top of the PKI layer.

`config.security.is_managed = true` disables **local** admin writes (`mp.from == 0` is rejected). It does not by itself force every admin action through PKI — the legacy `"admin"` channel still authorizes remote admin when `config.security.admin_channel_enabled == true`. The AdminModule refuses to persist `is_managed=true` unless at least one `admin_key` is populated — a deliberate guard against operators locking themselves out.

### Key-rotation hazards (actions that invalidate peers)

- **`factory_reset_device`** (the "full" variant, calls `NodeDB::factoryReset(eraseBleBonds=true)`) → **wipes** the X25519 private key; a fresh keypair is generated on the next region-set. Every existing peer holds the old public key, so DMs to this node silently fail PKI decrypt until every peer re-exchanges NodeInfo.
- **`factory_reset_config`** (the "partial" variant, calls `NodeDB::factoryReset()` with `eraseBleBonds=false`) → **preserves** the X25519 private key in `installDefaultConfig(preserveKey=true)`; the public key is zeroed and gets rebuilt from the preserved private key on the next boot via the NodeDB path's `regeneratePublicKey` call. Identity is preserved and the mesh does not need to re-exchange keys.
- **`region=UNSET → valid region`** → `ensurePkiKeys` runs inside the same `handleSetConfig` path; missing keys get generated at that moment.
- **Ham mode transitions** — entering Ham mode (`user.is_licensed=true`) runs `Channels::ensureLicensedOperation`, which **wipes every channel PSK** (all traffic becomes cleartext) and disables the legacy admin channel. The X25519 private key is preserved on the device but not used because `Router::send` skips PKI when `owner.is_licensed` is true. Leaving Ham mode re-enables PKI with the preserved keypair but does not restore the wiped channel PSKs — the operator has to re-set them.
- **Channel 0 PSK change** → every peer must re-learn the channel hash; cached NodeInfo becomes temporarily unreachable until the next broadcast.
- **`security.private_key` blanked via admin** → regenerates both halves (unless in Ham mode) and propagates the new public key via NodeInfo.

## Project Structure

```
firmware/
├── src/                    # Main source code
│   ├── main.cpp           # Application entry point
│   ├── mesh/              # Core mesh networking
│   │   ├── NodeDB.*       # Node database management
│   │   ├── Router.*       # Packet routing
│   │   ├── Channels.*     # Channel management
│   │   ├── CryptoEngine.* # AES-CTR (channels) + X25519 ECDH→AES-256-CCM (PKI for DMs/admin)
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

## Agent Tooling Baseline

Mirror counterpart: `AGENTS.md` under **Agent Tooling Baseline**.

To reduce avoidable agent mistakes, assume these tools are available (or install them before significant repo work):

- **Required CLI basics**: `bash`, `git`, `find`, `grep`, `sed`, `awk`, `xargs`
- **Strongly recommended**: `rg` (ripgrep) for fast file/text search, `jq` for JSON processing
- **Build/test tools**: `python3`, `pip`, virtualenv (`python3 -m venv`), `platformio` (`pio`)
- **Containerized native testing**: `docker` (fallback for non-Linux hosts; macOS can also build natively via `pio run -e native-macos`)

Fallback expectations for agents:

- If `rg` is unavailable, use `find` + `grep` instead of failing.
- For native tests on hosts without Linux deps, prefer `./bin/test-native-docker.sh`.
- The simulator helper script is `./bin/test-simulator.sh`.

Uses **PlatformIO** with custom scripts:

- `bin/platformio-pre.py` - Pre-build script
- `bin/platformio-custom.py` - Custom build logic, manifest generation

Build commands:

```bash
pio run -e tbeam              # Build specific target
pio run -e tbeam -t upload    # Build and upload
pio run -e native             # Build native/Linux version
pio run -e native-macos       # Build headless macOS meshtasticd (Homebrew prereqs in variants/native/portduino/platformio.ini)
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

### Native unit tests (C++)

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

Quick entry point for new test modules: `test/README.md` (native unit-test authoring guide, skeleton, pitfalls, and setup checklist).

### Hardware-in-the-loop tests (`mcp-server/tests/`)

Separate pytest suite that exercises real USB-connected Meshtastic devices. See the **MCP Server & Hardware Test Harness** section below for invocation, tier layout, and agent usage rules.

## MCP Server & Hardware Test Harness

The `mcp-server/` directory houses a firmware-aware [MCP](https://modelcontextprotocol.io/) server plus a pytest-based integration suite. AI agents that speak MCP get a well-defined tool surface for flashing, configuring, and inspecting physical Meshtastic devices — use it instead of hand-rolling `pio` or `meshtastic --port` calls where possible. `mcp-server/README.md` is the operator-facing setup doc; this section is the agent-facing usage contract.

The repo registers the server via `.mcp.json` at the repo root — Claude Code picks it up automatically once `mcp-server/.venv/` is built (`cd mcp-server && python3 -m venv .venv && .venv/bin/pip install -e '.[test]'`).

### When to use which surface

| Goal                                              | Tool                                                                                                             |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Find a connected device                           | `mcp__meshtastic__list_devices`                                                                                  |
| Read a live node's config/state                   | `mcp__meshtastic__device_info`, `list_nodes`, `get_config`                                                       |
| Mutate a device (owner, region, channels, reboot) | `set_owner`, `set_config`, `set_channel_url`, `reboot`, `shutdown`, `factory_reset` — all require `confirm=True` |
| Flash firmware to a variant                       | `pio_flash` (any arch) or `erase_and_flash` (ESP32 factory install)                                              |
| Stream serial logs while debugging                | `serial_open` → `serial_read` loop → `serial_close`                                                              |
| Administer `userPrefs.jsonc` build-time constants | `userprefs_get`, `userprefs_set`, `userprefs_reset`, `userprefs_manifest`                                        |
| Run the regression suite                          | `./mcp-server/run-tests.sh` (or `/test` slash command)                                                           |
| Diagnose a specific device                        | `/diagnose [role]` slash command (read-only)                                                                     |
| Triage a flaky test                               | `/repro <node-id> [count]` slash command                                                                         |

**One MCP call per port at a time.** `SerialInterface` holds an exclusive OS-level lock on the serial port for its lifetime. If a `serial_*` session is open on `/dev/cu.usbmodem101`, calling `device_info` on the same port will fail fast pointing at the active session. Sequence calls: open → read/mutate → close, then next device. Never parallelize tool calls on the same port.

### MCP tool surface (43 tools)

Grouped by purpose. Full argument shapes in `mcp-server/README.md`; a few high-value signatures are called out here.

- **Discovery & metadata**: `list_devices`, `list_boards`, `get_board`
- **Build & flash**: `build`, `clean`, `pio_flash`, `erase_and_flash` (ESP32 only), `update_flash` (ESP32 OTA), `touch_1200bps`
- **Serial sessions** (long-running, 10k-line ring buffer): `serial_open`, `serial_read`, `serial_list`, `serial_close`
- **Device reads**: `device_info`, `list_nodes`
- **Device writes**: `set_owner`, `get_config`, `set_config`, `get_channel_url`, `set_channel_url`, `send_text`, `send_input_event` (inject a button/key press via the firmware's InputBroker), `set_debug_log_api`; destructive/power-state writes require `confirm=True`: `reboot`, `shutdown`, `factory_reset`
- **userPrefs admin** (build-time constants, not runtime config): `userprefs_get`, `userprefs_set`, `userprefs_reset`, `userprefs_manifest`, `userprefs_testing_profile`
- **Vendor escape hatches**: `esptool_chip_info`, `esptool_erase_flash`, `esptool_raw`, `nrfutil_dfu`, `nrfutil_raw`, `picotool_info`, `picotool_load`, `picotool_raw`
- **USB power control** (via `uhubctl`, per-port PPPS toggle): `uhubctl_list` (read-only), `uhubctl_power(action='on'|'off', confirm=True)`, `uhubctl_cycle(delay_s, confirm=True)`. Target by raw `(location, port)` or by `role` (`"nrf52"`, `"esp32s3"`); role lookup checks `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` + `_PORT_<ROLE>` env vars first, falls back to VID auto-detection.
- **Observability** (UI tier + operator ad-hoc): `capture_screen(role, ocr=True)` — grabs a USB-webcam frame of the device OLED and optionally OCRs it. Requires `mcp-server[ui]` extras (`opencv-python-headless`, `easyocr`) and `MESHTASTIC_UI_CAMERA_DEVICE_<ROLE>` env var; falls through to a 1×1 black PNG `NullBackend` when unconfigured.

`confirm=True` is a tool-level gate on top of whatever permission prompt your MCP host shows. **Don't bypass it** by asking the host to auto-approve — it exists specifically because MCP hosts sometimes remember "always allow this tool" and that's dangerous for `factory_reset`, `erase_and_flash`, `uhubctl_power(action='off')`, and `uhubctl_cycle`.

**TCP / native-host nodes.** Setting `MESHTASTIC_MCP_TCP_HOST=<host[:port]>` makes `list_devices` surface a `meshtasticd` daemon (e.g. the `native-macos` build) as a synthetic `tcp://host:port` entry, and `connect()` routes through `meshtastic.tcp_interface.TCPInterface` instead of `SerialInterface`. Every read/write/admin tool that flows through `connect()` works against the daemon transparently. USB-only tools (`pio_flash`, `erase_and_flash`, `update_flash`, `touch_1200bps`, `serial_open`, `esptool_*`, `nrfutil_*`, `picotool_*`) raise a clear `ConnectionError` when handed a `tcp://` port; `pio_flash` against a `native*` env raises a `FlashError` (no upload step — use `build` and run the binary directly). The pytest harness still assumes USB-attached devices per role; TCP-aware fixtures are deferred. See `mcp-server/README.md` § "TCP / native-host nodes".

### Hardware test suite (`mcp-server/run-tests.sh`)

The wrapper auto-detects connected devices (VID → role map: `0x239A` → `nrf52`, `0x303A`/`0x10C4` → `esp32s3`), maps each role to a PlatformIO env (`nrf52` → `rak4631`, `esp32s3` → `heltec-v3`, overridable via `MESHTASTIC_MCP_ENV_<ROLE>`), then invokes pytest. Zero pre-flight config needed from the operator.

Suite tiers (collected + run in this order via `pytest_collection_modifyitems`):

1. `tests/unit/` — pure Python (boards parse, pio wrapper, userPrefs parse, testing profile, uhubctl parser). No hardware.
2. `tests/test_00_bake.py` — flashes each detected device with current `userPrefs.jsonc` merged with the session's test profile. Has its own skip-if-already-baked check comparing region + primary channel to the session profile; skips cheaply on warm devices.
3. `tests/mesh/` — multi-device mesh: bidirectional send, broadcast delivery, direct-with-ACK, mesh formation within 60s. Parametrized `[nrf52->esp32s3]` and `[esp32s3->nrf52]`. Includes `test_peer_offline_recovery` which uses uhubctl to physically power off one peer mid-conversation (requires uhubctl; skips without).
4. `tests/telemetry/` — `DEVICE_METRICS_APP` broadcast timing.
5. `tests/monitor/` — boot-log panic check.
6. `tests/recovery/` — `uhubctl` power-cycle round-trip + NVS persistence across hard reset. Requires `uhubctl` installed and a PPPS-capable hub; entire tier auto-skips otherwise.
7. `tests/ui/` — input-broker-driven screen navigation with camera + OCR evidence.
8. `tests/fleet/` — PSK seed session isolation.
9. `tests/admin/` — channel URL roundtrip, owner persistence across reboot.
10. `tests/provisioning/` — region + modem + slot bake, admin key presence, `UNSET` region blocks TX, userPrefs survive factory reset.

Invocation patterns:

```bash
./mcp-server/run-tests.sh                                        # full suite (auto-bake-if-needed)
./mcp-server/run-tests.sh --force-bake                           # reflash before testing
./mcp-server/run-tests.sh --assume-baked                         # skip bake (caller vouches for device state)
./mcp-server/run-tests.sh tests/mesh                             # one tier
./mcp-server/run-tests.sh tests/mesh/test_direct_with_ack.py     # one file
./mcp-server/run-tests.sh -k telemetry                           # name filter
```

**No hardware detected?** The wrapper auto-narrows to `tests/unit/` only and prints `detected hub : (none)` in the pre-flight header. Agents interpreting the output should call this out explicitly — a 52-test green run without hardware is qualitatively different from a 12-unit-test green run.

**Artifacts every run produces:**

- `mcp-server/tests/report.html` — self-contained pytest-html. Each test gets a `Meshtastic debug` section with the tail of firmware log + device state dump. **Open this first** on failures; it's the canonical evidence source.
- `mcp-server/tests/junit.xml` — CI-parseable.
- `mcp-server/tests/reportlog.jsonl` — pytest-reportlog stream (`$report_type` keyed JSONL). Consumed by the live TUI.
- `mcp-server/tests/fwlog.jsonl` — firmware log mirror from the `meshtastic.log.line` pubsub topic. Populated by the `_firmware_log_stream` autouse session fixture.

### Live TUI (`meshtastic-mcp-test-tui`)

A Textual-based live view that wraps `run-tests.sh`. Tails reportlog for per-test state, streams firmware logs, polls device state at startup + post-run (gated out of the active run because `hub_devices` holds exclusive port locks). Key bindings:

| Key | Action                                                                                                       |
| --- | ------------------------------------------------------------------------------------------------------------ |
| `r` | re-run focused test (leaf → that node id; internal node → directory or `-k`)                                 |
| `f` | filter tree by substring                                                                                     |
| `d` | failure detail modal (pulls `longrepr` + captured stdout from the reportlog)                                 |
| `g` | export reproducer bundle (tar.gz with README, test_report.json, time-filtered fwlog, devices.json, env.json) |
| `l` | toggle firmware log pane                                                                                     |
| `x` | tool coverage modal                                                                                          |
| `c` | cross-run history sparkline                                                                                  |
| `q` | quit (SIGINT → SIGTERM → SIGKILL escalation, 5-s windows each)                                               |

Launch:

```bash
cd mcp-server
.venv/bin/meshtastic-mcp-test-tui                 # full suite
.venv/bin/meshtastic-mcp-test-tui tests/mesh      # args pass through to pytest
```

The plain CLI stays primary; the TUI is for operators who want a live dashboard. Both consume the same `run-tests.sh`.

### Slash commands (Claude Code + Copilot)

Three AI-assisted workflows wrap the test harness. Claude Code operators get `/test`, `/diagnose`, `/repro`; Copilot operators get `/mcp-test`, `/mcp-diagnose`, `/mcp-repro`. Bodies:

- `.claude/commands/{test,diagnose,repro}.md`
- `.github/prompts/mcp-{test,diagnose,repro}.prompt.md`

`.claude/commands/README.md` is the index.

House rules for agents running these prompts:

- **Interpret failures, don't just echo them.** Pull firmware log tails from `report.html` and classify each failure as transient / environmental / regression. Use the exact format in `.claude/commands/test.md`.
- **No destructive writes without operator approval.** Any skill that could reflash, factory-reset, or reboot a device must describe the action and stop. The operator authorizes.
- **Sequential MCP calls per port.** See above.
- **"Unknown" is a valid classification.** If evidence doesn't support a root cause, say so and list what would disambiguate. Do not invent.

### Key fixtures (test authors + agents debugging)

`mcp-server/tests/conftest.py` provides:

- **`_session_userprefs`** (autouse session) — snapshots `userPrefs.jsonc` at session start, merges the session test profile via `userprefs.merge_active(test_profile)`, restores at teardown. Four layers of safety: pytest teardown + `atexit` + sidecar file (`userPrefs.jsonc.mcp-session-bak`) + startup self-heal in `run-tests.sh`. **Do not edit `userPrefs.jsonc` from inside a test.**
- **`_firmware_log_stream`** (autouse session) — subscribes to `meshtastic.log.line` pubsub on every connected `SerialInterface` and mirrors lines to `tests/fwlog.jsonl`. Drives the TUI firmware-log pane.
- **`_debug_log_buffer`** (autouse per-test) — captures last 200 firmware log lines + device state for attachment to the pytest-html `Meshtastic debug` section on failure.
- **`hub_devices`** (session) — `dict[role, SerialInterface]` with session-long exclusive port locks. Reason the TUI's device poller is gated to startup + post-run only.
- **`baked_mesh`** — parametrized mesh-pair fixture; depends on `test_00_bake`. `pytest_generate_tests` in `conftest.py` auto-generates `[nrf52->esp32s3]` and `[esp32s3->nrf52]` variants.
- **`test_profile`** — session-scoped dict: region, primary channel, admin key, PSK seed. Derived from `MESHTASTIC_MCP_SEED` (defaults to `mcp-<user>-<host>`).

### Firmware integration points tied to the test harness

Two firmware changes exist specifically so the test harness works reliably. **Keep these in mind when touching related code.**

- **`src/mesh/StreamAPI.cpp` + `StreamAPI.h`** — `emitLogRecord` uses a dedicated `fromRadioScratchLog` + `txBufLog` pair and a `concurrency::Lock streamLock`. Before this fix, `debug_log_api_enabled=true` would tear `FromRadio` protobufs on the serial transport because `emitTxBuffer` and `emitLogRecord` shared a single scratch buffer. The conftest enables the log stream session-wide; without this fix the device would corrupt its own FromRadio replies mid-session.
- **`src/mesh/PhoneAPI.cpp`** — `ToRadio` `Heartbeat(nonce=1)` triggers `nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, true, 0, true)` for serial clients, mirroring the pre-existing behavior for TCP/UDP clients in `PacketAPI.cpp`. The mesh tests rely on this to force a NodeInfo broadcast right after connect so the peer discovers them before the test's first assertion.

If you're modifying `StreamAPI`, `PhoneAPI`, `NodeInfoModule`, or `userPrefs` flow, run `./mcp-server/run-tests.sh` at minimum before asking for review.

### Recovery playbooks

| Symptom                                                                           | First check                                                   | Fix                                                                                                                                                                                                                              |
| --------------------------------------------------------------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `userPrefs.jsonc` dirty after test run                                            | `git status --porcelain userPrefs.jsonc`                      | If non-empty, re-run `./mcp-server/run-tests.sh` once — the pre-flight self-heal restores from sidecar. If still dirty, `git checkout userPrefs.jsonc`.                                                                          |
| Port busy / wedged CP2102 on macOS                                                | `lsof /dev/cu.usbserial-0001`                                 | Kill the holder. USB replug if the kernel still reports busy. Often a stale `pio device monitor` or zombie `meshtastic_mcp` process.                                                                                             |
| nRF52 appears unresponsive                                                        | `list_devices` shows VID `0x239A` but `device_info` times out | `touch_1200bps(port=...)` drops it into the DFU bootloader → `pio_flash` re-installs.                                                                                                                                            |
| Device fully wedged (Guru Meditation, frozen CDC, no DFU)                         | `list_devices` shows the VID but every admin call times out   | `uhubctl_cycle(role="nrf52", confirm=True)` hard-power-cycles the port via USB hub PPPS. `baked_single`'s auto-recovery hook does this once automatically if uhubctl is installed. Falls back to physical replug if no PPPS hub. |
| Multiple MCP server processes                                                     | `ps aux \| grep meshtastic_mcp` shows >1                      | Kill all but the one your MCP host spawned. Zombies hold ports and break tests.                                                                                                                                                  |
| Mesh formation fails, one side sees peer but other doesn't                        | `/diagnose` (or `list_nodes` on both sides)                   | Asymmetric NodeInfo. `test_direct_with_ack` has a heal path; `/repro` it a few times. If persistent, both devices' clocks may be out of sync with their NodeInfo cooldown.                                                       |
| "role not present on hub" in skip reasons                                         | `list_devices`                                                | Expected if a device is unplugged. Reconnect before re-running the tier.                                                                                                                                                         |
| Entire `tests/recovery/` tier skipped                                             | `command -v uhubctl`                                          | Expected if `uhubctl` isn't on PATH. Install via `brew install uhubctl` (macOS) or `apt install uhubctl` (Debian/Ubuntu). Also skips if no hub advertises PPPS.                                                                  |
| Entire `tests/ui/` tier skipped ("firmware not baked with USERPREFS_UI_TEST_LOG") | reportlog.jsonl for the skip reason                           | Re-run with `--force-bake` so the UI-log macro gets compiled into the fresh firmware. First run after the Round-3 landing always re-bakes.                                                                                       |
| `tests/ui/` runs but captures are all 1×1 black PNGs                              | `MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3`                         | Env var not set → `NullBackend`. Point a USB webcam at the heltec-v3 OLED and set the device index; `.venv/bin/python -c "import cv2; [print(i, cv2.VideoCapture(i).read()[0]) for i in range(5)]"` discovers it.                |
| Tests fail only on first attempt then pass on rerun                               | —                                                             | State leak from a prior session. Run with `--force-bake` to reset to a known state.                                                                                                                                              |

### Never do these without asking

- `factory_reset` — wipes node identity; regenerates PKI keypair. Mesh peers will reject old DMs until re-exchange. Legitimate only when the operator explicitly wants it.
- `erase_and_flash` — full chip erase; destroys all on-device state.
- `esptool_erase_flash` / `esptool_raw` write/erase — bypasses pio's safety chain.
- `set_config` on `lora.region` — changes regulatory domain; requires physical-location context the operator has and the agent doesn't.
- `reboot` / `shutdown` mid-test — breaks fixture invariants.
- `push -f`, `rebase -i`, `reset --hard`, or any history-rewriting git operation.
- Clicking computer-use tools on web links in Mail/Messages/PDFs — open URLs via the claude-in-chrome MCP so the extension's link-safety checks apply.

## Resources

- [Documentation](https://meshtastic.org/docs/)
