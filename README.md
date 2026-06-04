<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")

## WireGuard VPN (Experimental)

The firmware now includes an experimental WireGuard VPN client.

**How to enable:**
- Edit your `platformio.ini` variant configuration to add: `-DHAS_WIREGUARD_VPN=1`

**Configuration:**
- Configure server details and keys on the device with `bin/wireguard-config.py` or another client that supports `ModuleConfig.wireguard`. Production firmware should leave WireGuard defaults blank and disabled.

Import a standard single-peer WireGuard config file:

```bash
python bin/wireguard-config.py --port COM7 set --config wg0.conf --enable
```

Or use the simple desktop GUI:

```bash
python bin/wireguard-gui.py
```

The GUI lets you select a serial port, browse for a WireGuard `.conf`, push it to the device, confirm the readback, and poll basic tunnel health.

Or set fields directly:

```bash
python bin/wireguard-config.py --port COM7 set \
  --enable \
  --address 10.0.0.2 \
  --server-addr wg.example.net \
  --server-port 51820 \
  --private-key CLIENT_PRIVATE_KEY \
  --public-key SERVER_PUBLIC_KEY
```

Read runtime status:

```bash
python bin/wireguard-config.py --port COM7 get
```

Disable automatic startup without erasing saved keys:

```bash
python bin/wireguard-config.py --port COM7 disable
```

The config importer uses `Interface.Address`, `Interface.PrivateKey`, `Peer.PublicKey`, `Peer.PresharedKey`, and `Peer.Endpoint`. CLI flags override imported values. Private and preshared keys are redacted from output unless `--show-secrets` is passed.

**How it works:**
- Uses the [`wireguard-esp32`](https://github.com/juvinski/wireguard-esp32) library.
- When enabled and fully configured, the VPN tunnel starts automatically when WiFi or Ethernet is available and NTP time is synced. It stops when the network disconnects.

- See [`src/mesh/wireguard/WireGuard_ReadMe.md`] for developer documentation.

> **Note:** This feature is experimental and full tunnel functionality may not be stable yet.
