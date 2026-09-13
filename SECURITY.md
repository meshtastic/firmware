# Security Policy

## Supported Versions

| Firmware Version | Supported          |
| ---------------- | ------------------ |
| 2.8.x            | :white_check_mark: |
| <= 2.7.x         | :x:                |

## Reporting a Vulnerability

We support the private reporting of potential security vulnerabilities. Please go to the Security tab to file a report with a description of the potential vulnerability and reproduction scripts (preferred) or steps, and our developers will review.

Before filing, please read the Security Model below. Behavior whose only precondition is local API access to a node, or possession of a channel's pre-shared key, is intended by design and is not considered a vulnerability.

## Security Model

Meshtastic is an off-grid mesh protocol that runs on constrained microcontrollers within a 256 byte LoRa packet limit. These constraints shape its security design and rule out the heavier schemes used by IP-based protocols. This section summarizes what the firmware protects, the assumptions it rests on, and its known limits. Fuller write-ups are in the documentation:

- Encryption overview: https://meshtastic.org/docs/overview/encryption/
- Technical reference: https://meshtastic.org/docs/development/reference/encryption-technical/
- Known limitations and future work: https://meshtastic.org/docs/about/overview/encryption/limitations/

### Cryptographic mechanisms

- Channels are encrypted with a pre-shared key (PSK) using AES256-CTR. Channel traffic is encrypted but not authenticated, so anyone holding the PSK can read channel messages and can send messages as any node on that channel.
- Direct messages and admin messages use public key cryptography (x25519 key exchange with AES-CCM), providing confidentiality, authentication, and integrity between nodes on 2.5.0 or newer that have exchanged keys.
- Admin sessions use short-lived session IDs to limit replay of control messages.

### Local trust boundary

A client connected to a node over Bluetooth, USB serial, WiFi, or Ethernet has full local API access. From that connection it can read decrypted traffic, send messages as the node, change configuration (subject to managed mode), and read the node's private key for backup. This is intended behavior. The firmware trusts the local link the same way a phone or laptop trusts a directly attached device, and anything within reach of that connection (a shared LAN, a USB cable to an untrusted host, a paired phone) should be treated as part of the node itself.

#### What the serial setting does, and what it does not

`security.serial_enabled = false` does two things. It stops the serial console from accepting API frames, and it suppresses log output on that port.

Suppressing that output is a real protection worth understanding: the Bluetooth pairing PIN is written to the log. On nRF52 the configured PIN is logged at setup and again when a pairing attempt begins; on ESP32 the passkey is logged for the user to read off the console. Anyone who can watch the serial port can therefore read the PIN and pair over Bluetooth. Disabling the serial console closes that disclosure, which matters when a node's USB or UART is reachable, for example a board with an exposed header, a node plugged into a host you do not control, or a gateway whose console output is captured to a log.

What it does not do:

- It does not restrict local API access. Bluetooth, WiFi, and Ethernet never consult this setting. A node with the serial console disabled and Bluetooth enabled, the default on most boards, still offers full local API access, including the private-key read described above, to anyone who can pair with it.
- It does not protect a node from someone holding it. The setting is stored configuration, not a lock. Physical possession allows re-enabling it, factory resetting, or reflashing, and on a device without at-rest encryption the private key can be read out of flash directly.

Disable it to keep the pairing PIN and other log output off an exposed port. To restrict what a connected client may do, or to protect stored keys, use lockdown.

#### Lockdown mode

Lockdown is the mechanism that actually restricts local access, and it is what to use when a node is physically exposed and its configuration and keys must survive that. Rather than trusting every local connection, it requires a passphrase before a connection may administer the node or read sensitive configuration, encrypts stored configuration at rest, and can permanently disable the debug port.

When lockdown is provisioned, the node gains:

- **Per-connection authentication and redaction.** An unauthenticated local client is not trusted. Admin payloads other than the unlock itself are dropped, and sensitive configuration is redacted: the client receives an empty security config, with no private key, no admin keys, and no channel PSKs.
- **Encrypted storage at rest.** Stored configuration, including the private key, is encrypted with AES-128-CTR and authenticated with HMAC-SHA256, so reading the flash does not yield the key.
- **APPROTECT.** On nRF52 silicon that supports it, the debug port is burned off, closing SWD as a route to memory. This is one-way and is not reversed by disabling lockdown.
- **Optional session limits.** Builds may cap how long an unlocked session lasts, bounding exposure if a node is taken while unlocked.

Availability and setup:

- Lockdown is **nRF52 only** and is **opt-in at build time**. No released variant ships with it enabled, so it requires a firmware build with `-DMESHTASTIC_ENABLE_LOCKDOWN=1`. Flash-constrained nRF52 variants may be unable to fit it.
- Once built in, whether it is active is decided at runtime. A node that has never been provisioned behaves exactly like stock firmware, with plaintext storage, no redaction, and normal logging.
- The operator provisions and removes it from the client app. Removing it decrypts storage back to plaintext and reboots, with the exception of APPROTECT, which cannot be undone.

### Node identity (Trust On First Use)

There is no central authority to sign node keys. The first public key a node hears for a given node number is the one it binds to that node number, a Trust On First Use (TOFU) model that is a hard requirement of a decentralized mesh. Clients and firmware reduce the impact of this by keeping favorited nodes from rolling out of the node database and by flagging public-key changes in the client UI.

Firmware 2.8.X adds XEdDSA packet signing to further secure node identity claims and the authenticity of subsequent messages. It reuses each node's existing x25519 key pair to produce signatures, so a receiver can verify that a packet came from the holder of the bound key. Once a node has been seen signing, unsigned packets claiming that identity can be rejected.

### Known limitations

- No perfect forward secrecy. Traffic captured today can be decrypted later if a key is compromised, for example through a lost node or a mishandled channel key.
- Channel messages are not authenticated, as noted above. Although as of 2.8, channel messages will be xedDSA signed as a means of verification that is non-breaking.
- Setting WiFi credentials, or performing any other local administration, on an ESP32 over an untrusted network exposes that traffic, including the credentials, to the network. Provision and administer nodes over a trusted channel instead: Bluetooth, USB serial, or remote admin over the mesh. There is no current roadmap item to secure local administration over untrusted WiFi, though it may be addressed in a future release.
- Lockdown, described above, is the only mechanism that restricts local API access or protects stored keys, and it exists on nRF52 only. On every other platform, including all ESP32 targets, a node's configuration and private key are readable by anyone who can connect to it locally or take possession of it. Site a node accordingly.
