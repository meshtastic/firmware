# src/crypto

Hardware-rooted cryptography and secure-element drivers.

`CryptoEngine` (software channel / PKI crypto) currently still lives under `src/mesh/` for historical reasons and is expected to migrate here in a future cleanup. New hardware-crypto code should land in this folder.

## NXP SE050 secure element driver

`Se050Client.{h,cpp}` is a host-side client for the NXP SE050 IoT Applet over T=1-over-I2C. It exposes a single singleton pointer (`se050::client`) initialized during `setup()` after the I2C scan if the chip is detected and its applet selects successfully.

### Hardware

|                |                                                    |
| -------------- | -------------------------------------------------- |
| Bus            | I2C (Qwiic), 3.3V                                  |
| Address        | 0x48                                               |
| Detection      | `ScanI2CTwoWire` probes with a T=1 S(RESYNC) frame |
| Verified chip  | NXP SE050E2HQ1/Z01Z3Z (OEF A921)                   |
| Verified board | [Muzi Works Base Duo](https://muzi.works)          |

Boards with an FT6336U touchscreen also sit at 0x48; the probe distinguishes by checking the T=1 NAD byte (`0xA5` → SE050, anything else → fall through to the existing touchscreen detection).

### Driver surface (`se050::Client`)

Built in layers on top of raw T=1:

| Layer                   | Methods                                                                                |
| ----------------------- | -------------------------------------------------------------------------------------- |
| Transport (T=1 framing) | `transceive`                                                                           |
| Applet selection        | `begin` (SELECT AID + `VersionInfo` parse)                                             |
| Identity                | `getUID`, `getCachedUID`                                                               |
| RNG                     | `getRandom` (chip TRNG, no session required)                                           |
| SCP03 session           | `openPlatformScp03`, `sendSecure`                                                      |
| EC crypto               | `createECCurve`, `writeECKeyGen`, `writeECKeyGenWithPolicy`, `readECPub`, `ecdhX25519` |
| Object metadata         | `objectExists`, `getObjectInfo`, `readObjectWithAttestation`                           |
| Housekeeping            | `deleteObject`, `isReady`, `isSecureSession`                                           |

### Host entropy for SCP03 host challenge

The SCP03 host challenge is sourced from `HardwareRNG::fill()` XOR'd with the SE050's own NIST SP 800-90B certified TRNG. The XOR of two independent entropy sources stays unpredictable as long as at least one does, which covers the case where `HardwareRNG::fill()` silently falls back to `std::random_device` on a platform without a dedicated hardware case.

| Platform  | `HardwareRNG::fill()` source                                    |
| --------- | --------------------------------------------------------------- |
| ESP32     | `esp_fill_random()`                                             |
| nRF52     | Nordic SDK `nRFCrypto.Random.generate()`                        |
| RP2040    | `rp2040.hwrand32()`                                             |
| Portduino | `getrandom()` syscall                                           |
| Other     | `std::random_device` (XOR with SE050 TRNG keeps challenge safe) |

`HardwareRNG::fill()` never calls Arduino's `randomSeed()`, so the ESP32 convention of deliberately not seeding the global PRNG is preserved.

### SCP03 static keys

Sessions open at **security level 0x33** (full C-DEC + C-MAC + R-ENC + R-MAC) per GP 2.3 Amendment D. `openPlatformScp03()` takes three 16-byte keys (ENC, MAC, DEK) from the caller.

> **Development vs production.** NXP publishes default static keys for OEF A921 in AN12436 §A. These are world-known developer keys. Production deployments **must** rotate them per-device before shipping.

### Key-object IDs

NXP reserves the `0x7F*` range for system objects (AN12413). Caller-created objects should live in the low range (e.g. `0x00001000` and up).

| Object ID    | Purpose                                        |
| ------------ | ---------------------------------------------- |
| `0x7FFF0206` | Pre-provisioned chip UID (read-only, 18 bytes) |

### Object policies and attestation

SE05x object policies are applied when an object is created; they are not a standalone mutable setting on an existing object. Use `writeECKeyGenWithPolicy()` for Meshtastic-owned X25519 keys that need constrained key agreement, read/delete, or attestation rights. If a policy needs to change, delete the caller-owned object and recreate it with the new policy.

`readObjectWithAttestation()` is read-only from the secure element's perspective. It returns caller-requested object data when the object's read policy allows it, plus chip ID, attributes, timestamp, and signature fields for verifying that the response came from the SE050.

### SCP03 session state recovery

`sendSecure()` advances the host MCV (MAC chaining value) as part of building each command. If an I2C-layer failure prevents the card from processing the command, the host and card MCV states diverge and every subsequent secure APDU fails until a new session is opened.

Recovery policy: on any failure path inside `sendSecure()`, the pre-command MCV is restored and `scp.active` is cleared. Callers detect the invalidation via `isSecureSession()` and re-open the session before their next secure APDU.

### Curve25519 endianness

The SE050 stores Montgomery-curve X coordinates in **big-endian**; RFC 7748 (software Curve25519 / X25519) uses **little-endian**. `ecdhX25519()` takes `peerPub` and returns `shared` in the chip's native big-endian format, and `readECPub()` returns the raw bytes from the chip. Callers that interop with software Curve25519 implementations (`rweather`, authority-side tooling, etc.) must reverse the 32-byte blocks at the boundary.

### Boot flow

1. `ScanI2CTwoWire` detects `0x48` as `NXP_SE050`.
2. `main()` constructs `se050::Client` as a function-static object.
3. `begin()` runs S(RESYNC) then SELECTs the IoT Applet and parses the `VersionInfo` response.
4. `getUID()` reads the chip UID (cached for zero-cost later access).
5. `se050::client` is published globally. Null if any of the above failed.

Consumers open an SCP03 session later, during their own initialization.
