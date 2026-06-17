# Security Policy

## Supported Versions

| Firmware Version | Supported          |
| ---------------- | ------------------ |
| 2.7.x            | :white_check_mark: |
| <= 2.6.x         | :x:                |

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

### Node identity (Trust On First Use)

There is no central authority to sign node keys. The first public key a node hears for a given node number is the one it binds to that node number, a Trust On First Use (TOFU) model that is a hard requirement of a decentralized mesh. Clients and firmware reduce the impact of this by keeping favorited nodes from rolling out of the node database and by flagging public-key changes in the client UI.

Firmware 2.8.X adds XEdDSA packet signing to further secure node identity claims and the authenticity of subsequent messages. It reuses each node's existing x25519 key pair to produce signatures, so a receiver can verify that a packet came from the holder of the bound key. Once a node has been seen signing, unsigned packets claiming that identity can be rejected.

### Known limitations

- No perfect forward secrecy. Traffic captured today can be decrypted later if a key is compromised, for example through a lost node or a mishandled channel key.
- Channel messages are not authenticated, as noted above. Although as of 2.8, channel messages will be xedDSA signed as a means of verification that is non-breaking.
- Setting WiFi credentials, or performing any other local administration, on an ESP32 over an untrusted network exposes that traffic, including the credentials, to the network. Provision and administer nodes over a trusted channel instead: Bluetooth, USB serial, or remote admin over the mesh. There is no current roadmap item to secure local administration over untrusted WiFi, though it may be addressed in a future release.
