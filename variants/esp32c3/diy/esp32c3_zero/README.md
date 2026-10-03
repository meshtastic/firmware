# Waveshare ESP32-C3-Zero with Ai-Thinker Ra-02

This variant connects a [Waveshare ESP32-C3-Zero](https://docs.waveshare.com/ESP32-C3-Zero) to an Ai-Thinker Ra-02
(SX1278) LoRa module.

## Wiring

| Ra-02 pin | ESP32-C3-Zero pin |
| --------- | ----------------- |
| 3.3V      | 3V3               |
| GND       | GND               |
| SCK       | GPIO4             |
| MISO      | GPIO5             |
| MOSI      | GPIO6             |
| NSS       | GPIO7             |
| RESET     | GPIO8             |
| DIO0      | GPIO3             |
| DIO1      | Not connected     |
| DIO2      | Not connected     |

The Ra-02 is a 3.3 V device. Do not connect its VCC or signal pins to 5 V. Attach a suitable antenna before powering
the module; transmitting without an antenna can damage its RF output stage.

The SX1278-based Ra-02 operates from 410 to 525 MHz. Configure Meshtastic for a legal regional band within that
range; this module cannot operate on 868 or 915 MHz networks.

## Optional peripherals

| Peripheral  | ESP32-C3-Zero pin |
| ----------- | ----------------- |
| I2C SDA     | GPIO1             |
| I2C SCL     | GPIO0             |
| GPS RX      | GPIO20            |
| GPS TX      | GPIO21            |
| BOOT button | GPIO9             |

Connect the GPS module's TX output to GPIO20 and its RX input to GPIO21.

## Build

From the firmware repository root, run:

```sh
pio run -e esp32c3_zero
```

Native USB CDC is enabled for flashing and serial logging through the board's USB-C connector.
