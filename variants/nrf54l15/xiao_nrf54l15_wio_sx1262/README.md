# Seeed Studio XIAO nRF54L15 Sense + Wio-SX1262

Meshtastic on the XIAO nRF54L15 Sense stacked with the Wio-SX1262 for XIAO
(header pinout, standalone SKU 113010003) and, optionally, the L76K GNSS
Module for Seeed Studio XIAO.

| Function | XIAO pin | nRF54 GPIO |
| --- | --- | --- |
| SX1262 SCK / MISO / MOSI | D8 / D9 / D10 | P2.01 / P2.04 / P2.02 (spi00) |
| SX1262 CS | D4 | P1.10 |
| SX1262 DIO1 / BUSY / RESET | D1 / D3 / D2 | P1.05 / P1.07 / P1.06 |
| SX1262 RF switch RX enable | D5 | P1.11 |
| L76K UART (MCU TX / MCU RX) | D6 / D7 | P2.08 / P2.07 (uart21 = Serial1) |
| L76K STANDBY | D0 | P1.04 |
| LSM6DS3 IMU (on the Sense) | D11 / D12 | i2c30, address 0x6a |
| Status LED (active low) | - | P2.00 |

Notes:

- DIO2 drives the TX side of the RF switch and DIO3 supplies the 1.8 V TCXO.
- i2c22 is disabled in the board overlay because it would otherwise claim
  D4/D5, which the radio uses; i2c30 on D11/D12 remains for the IMU and
  external sensors.
- The L76K shield shares its reset line with the radio's RESET on D2, which
  makes the GNSS command channel unreliable, so the variant sets
  GPS_SKIP_PROBE and parses the NMEA stream directly (default 9600 baud).
- Flashing is via the onboard CMSIS-DAP probe (`pyocd`, target `nrf54l`).
  The MCUboot secondary slot is reclaimed for LittleFS storage; OTA is not
  provided.
