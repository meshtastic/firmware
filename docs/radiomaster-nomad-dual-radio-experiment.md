# RadioMaster Nomad dual-radio experiment

This branch is an experimental time-division prototype for the two LR1121s in the RadioMaster Nomad. It is not a production
Gemini implementation.

## Test layout

| Path     | Logical channel | Modem                                    | Frequency                    | Maximum test power |
| -------- | --------------- | ---------------------------------------- | ---------------------------- | ------------------ |
| LR1121 1 | 0               | Configured primary, initially MediumFast | Configured primary frequency | 10 dBm             |
| LR1121 2 | 1               | ShortTurbo, SF7/BW500/CR 4/5             | 926.750 MHz (US slot 50)     | 10 dBm             |

The second path is deliberately fixed to US ShortTurbo for bring-up. Logical channel 1 must be configured with the name and PSK
used by the ShortTurbo peer. The composite interface selects LR1121 2 from the encrypted channel-1 hash and marks received
packets as `TRANSPORT_LORA_ALT1`. Secondary transmission is disabled unless the primary region is exactly `US`.

The review build excludes MeshBeacon because its global radio-switch state is not dual-radio aware. Both LR1121 paths can
transmit, but the coordinator permits only one transmitter at a time and the build clamps the requested output to 10 dBm.

## What Meshtastic channels can and cannot do

A Meshtastic channel is a cryptographic namespace. It does not own a modem preset, frequency, or radio. The decoded
`MeshPacket.channel` value is a local channel-table index; encryption replaces it with an 8-bit name/PSK hash.

This experiment maps channel 1 to the secondary radio, so normal traffic on each logical channel can use a different physical
mesh. Existing routing preserves the packet's logical channel and can relay a packet on the same radio. It does not translate
channel 0 traffic into channel 1 traffic.

A channel-to-channel bridge would need to decrypt, clone, change the channel, and re-encrypt selected packets. That changes the
security boundary and needs an explicit loop policy. PKI direct messages are a separate problem because their radio header uses
channel zero. Transparent channel translation, PKI forwarding, and traceroute across the two security domains are therefore out
of scope for the first RF test.

## RF safety model

The two radios may receive together, but only one may transmit. Before either LR1121 transmits, the peer must enter standby so
its receive path is disconnected. APC2 is a shared PA-bias resource and must be zero before either radio returns to receive.

The allowed state transitions are:

```text
BOTH_RX -> BOTH_IDLE -> TX_RADIO_1 -> BOTH_IDLE -> BOTH_RX
BOTH_RX -> BOTH_IDLE -> TX_RADIO_2 -> BOTH_IDLE -> BOTH_RX
```

Simultaneous TX and one-radio-TX/one-radio-RX are prohibited in this prototype. The build clamps both paths to 10 dBm.

This follows the conservative mLRS behavior, which idles the unused LR1121 before a single-path transmission. ExpressLRS Gemini
can transmit on both radios together because both paths are deliberately in TX; that does not establish that asynchronous
same-band TX/RX is safe.

The LR1121 normal sub-GHz input limit is 0 dBm and its absolute maximum RF input is +10 dBm. A neighboring 30 dBm transmitter
therefore needs more than 30 dB of end-to-end isolation, including margin, to leave the receiver within its operating limit.
RadioMaster does not publish Nomad antenna-port isolation, RF-switch isolation, or external PA/LNA reverse-power limits.

Primary references:

- [RadioMaster Nomad product page](https://radiomasterrc.com/products/nomad-dual-1-watt-gemini-xrossband-expresslrs-module)
- [ExpressLRS Gemini documentation](https://www.expresslrs.org/software/gemini/)
- [ExpressLRS unused-radio idle sequencing](https://github.com/ExpressLRS/ExpressLRS/blob/a9d4a9cb5b5687c4c9d7e9e7fbdf44ad93651da6/src/lib/LR1121Driver/LR1121.cpp#L603-L660)
- [mLRS transmit-one, idle-other sequencing](https://github.com/olliw42/mLRS/blob/3f7b53fcfe42bc4f883f6d61b7819aaf213081d8/mLRS/Common/common.h#L343-L357)
- [Semtech LR1121 product page and Rev 2.1 datasheet](https://www.semtech.com/products/wireless-rf/lora-connect/lr1121)

## Staged validation

1. Confirm both LR1121s initialize and remain in receive with APC2 at zero.
2. Use two 50-ohm loads rated for at least 2 W before deliberate RF testing.
3. Confirm channel 0 receive on the primary radio and channel 1 receive on the ShortTurbo radio.
4. Send one nominal 10 dBm packet every ten seconds, one radio at a time, with the fan on.
5. Verify the peer enters standby before APC2 rises and neither radio returns to receive until APC2 is zero.
6. Validate a same-channel relay on each physical mesh.
7. Consider decoded-only channel translation only after the two independent paths pass.
8. Do not raise power until conducted power, current, temperature, harmonics, port coupling, and front-end ratings have been
   checked.

Do not test simultaneous 1 W operation from this branch.

## Hardware result

The prototype was exercised on a RadioMaster Nomad on 2026-07-22 with both antennas attached and both paths limited to a
nominal 10 dBm:

- A clean `radiomaster_nomad_gemini` build and factory flash completed successfully.
- Both LR1121s initialized: the primary at 913.125 MHz with MediumFast and the secondary at 926.750 MHz with ShortTurbo.
- The primary decoded live MediumFast telemetry and node-information packets and populated a 100-node mesh database.
- A primary-radio traceroute completed to `!4703f8ab`, including a three-hop outbound path and a two-hop return path.
- A channel-1 text packet entered the secondary queue and the LR1121 driver reported TX-done. With no second device sharing the
  new ShortTurbo PSK, its retries correctly ended with `MAX_RETRANSMIT`; this proves the local firmware and driver path, not
  calibrated radiated output or peer reception.
- Wi-Fi joined the configured 2.4 GHz network, obtained DHCP, answered ICMP, accepted a TCP API connection on port 4403, and
  completed configuration exchanges.
- `lora.pa_fan_disabled` was written and read back in both states. The final state is `false`, so firmware commands the fan on.
  The Nomad exposes no fan tachometer, so this does not verify electrical GPIO output or physical rotation.

The host's Meshtastic Python CLI was older than the firmware protobufs and intermittently printed heartbeat and enum-compatibility
errors after successful operations. The firmware remained reachable and retained the requested configuration.

No calibrated RF-output, spectral, thermal, or antenna-isolation measurement was performed. The 10 dBm value is the configured
total output through the provisional Nomad PA table, not a power-meter result.
