# Portduino config fixtures

Input files for `bin/test-config-check.sh`, which drives a built `meshtasticd`
binary and asserts what `meshtasticd --check` reports about each one. Every file
here is referenced by name from that script, so renaming one means editing it too.

The theme is configuration that is accepted by the YAML parser but does not mean
what it looks like it means - the failures that otherwise only show up as a radio
that never transmits.

Each file carries a comment header naming its planted fault and what the checker is
expected to say about it, so a fixture can be read on its own. One assertion,
`malformed-indent.yaml`'s reported line number, counts those header lines - editing
that file's comments means updating the expected line in the script.

These files are exempt from `trunk fmt` (see `.trunk/trunk.yaml`): prettier rejects
the duplicate keys and bad indentation that are the entire point of them.

## Valid, one per radio module family

These must each report zero errors and zero warnings, and must resolve to the
module named in the file. A silent fallback to `sim` would otherwise pass.

| File                 | Module   | Family     |
| -------------------- | -------- | ---------- |
| `module-rf95.yaml`   | `RF95`   | SX127x     |
| `module-sx1262.yaml` | `sx1262` | SX126x     |
| `module-sx1268.yaml` | `sx1268` | SX126x     |
| `module-llcc68.yaml` | `LLCC68` | SX126x     |
| `module-sx1280.yaml` | `sx1280` | SX128x     |
| `module-lr1110.yaml` | `lr1110` | LR11xx     |
| `module-lr1120.yaml` | `lr1120` | LR11xx     |
| `module-lr1121.yaml` | `lr1121` | LR11xx     |
| `module-sim.yaml`    | `sim`    | simulated  |
| `module-auto.yaml`   | `auto`   | autodetect |

`valid.yaml` is a minimal SX126x config, and `empty-sections.yaml` (`Lora:` with
no body) exists to prove the checker does not invent a finding for it.

## Module naming

Names are matched exactly and are not consistently cased - `RF95` and `LLCC68`
are upper, `sx1262` and `lr1121` lower.

| File                     | Expected                                            |
| ------------------------ | --------------------------------------------------- |
| `module-unknown.yaml`    | `sx1263` is refused, and the valid set is listed.   |
| `module-wrong-case.yaml` | `llcc68` is refused with a "did you mean" for case. |

## LR11xx rfswitch table

| File                           | Expected                                                          |
| ------------------------------ | ----------------------------------------------------------------- |
| `rfswitch-valid.yaml`          | A full seven-mode table on an `lr1121` is clean.                  |
| `rfswitch-partial.yaml`        | Legal, but the omitted modes are named - they are driven all-LOW. |
| `rfswitch-bad-pin.yaml`        | `DIO9` is a real pin name but not a switch pin on an `lr1121`.    |
| `rfswitch-row-length.yaml`     | Rows shorter and longer than the declared pin count.              |
| `rfswitch-bad-level.yaml`      | `high` and `On`: anything not exactly `HIGH` is silently LOW.     |
| `rfswitch-no-pins.yaml`        | No `pins` list, so no switch pin is ever driven.                  |
| `rfswitch-too-many-pins.yaml`  | Six pins declared; only the first five are read.                  |
| `rfswitch-not-a-map.yaml`      | `rfswitch_table` given a scalar.                                  |
| `rfswitch-unknown-mode.yaml`   | `MODE_TRANSMIT` is not a mode.                                    |
| `rfswitch-stranded-modes.yaml` | A `MODE_` row one level out, sitting under `Lora:` doing nothing. |

`rfswitch-partial.yaml` is legal but noted: the omitted modes are driven all-LOW.
`module-mismatch-lr11xx.yaml` (LR11xx with no table - cannot transmit) and
`module-mismatch-sx126x.yaml` (a table on a radio that never applies one) cover
the module/table disagreement in both directions.

## LR20x0 rfswitch table and interrupt DIO

The table is handed to an LR20x0 as well as an LR11xx, and the two parts have neither the
same modes nor the same switch pins, so which findings are correct depends on the module.

| File                                 | Expected                                                                        |
| ------------------------------------ | ------------------------------------------------------------------------------- |
| `rfswitch-lr2021.yaml`               | Clean. `MODE_RX_HF` is a real mode here, and `IRQ_DIO_NUM` keeps the IRQ clear. |
| `rfswitch-lr2021-irq-collision.yaml` | `IRQ_DIO_NUM: 5` names a pin the table also drives as a switch line.            |
| `rfswitch-lr2021-irq-default.yaml`   | The same collision reached by omitting the key: the radio default is DIO5.      |
| `rfswitch-lr2021-irq-clear.yaml`     | **False-positive guard** - DIO5 as the IRQ, table on DIO6/7/8, is clean.        |
| `rfswitch-lr2021-irq-all-low.yaml`   | DIO5 listed in `pins` but driven LOW everywhere: still a collision.             |
| `rfswitch-lr2021-no-table.yaml`      | An LR20x0 with no table cannot transmit, same as an LR11xx without one.         |
| `rfswitch-lr2021-wrong-mode.yaml`    | `MODE_TX_HP` and `MODE_GNSS` are LR11xx modes an LR20x0 does not have.          |

`begin()` needs only SPI and BUSY, so a radio whose interrupt lands on a switch pin still
reports init success and then never receives a packet.

Why `-irq-all-low` is a fault and `-irq-clear` is not: `LR2021::config()` (from `begin()`)
points the IRQ DIO at `FUNCTION_IRQ`, then `setRfSwitchTable()` calls `setDioFunction(...,
FUNCTION_RF_SWITCH)` for every non-NC pin in the list whatever the levels are, and nothing
re-asserts the IRQ function afterwards. Listing the pin is what breaks it, not driving it.

`rfswitch-lr2021.yaml` also carries `LR2021_MAX_POWER` and `LR2021_MAX_POWER_HF` in a case
that must stay at zero warnings, so dropping either from the checker's schema fails it.

## TCXO probing (`Lora.TCXO_OPTIONAL`)

A variant declares "a TCXO may or may not be fitted" at compile time with `TCXO_OPTIONAL`.
A Portduino carrier cannot: the same `meshtasticd` binary runs on hardware populated either
way, so the statement arrives as YAML and is answered at runtime.

| File                             | Expected                                                                |
| -------------------------------- | ----------------------------------------------------------------------- |
| `tcxo-optional.yaml`             | Clean. No Vref given, so the TCXO attempt uses the 1.6 V radio default. |
| `tcxo-optional-sx1262.yaml`      | Clean. Another family, explicit Vref, which is the one reported.        |
| `tcxo-optional-unsupported.yaml` | An SX128x has no TCXO reference to probe for, so the key is inert.      |

With the probe asked for and no voltage given, the driver tries the radio default rather
than skipping the TCXO attempt - otherwise there is nothing to fall back _from_.

## PA gain table (`TX_GAIN_LORA`)

Two shapes are accepted and they fail differently. A list is read element-by-element
with `.as<int>()` and NO fallback, so one bad entry throws and meshtasticd will not
start. A bare scalar is read as `.as<int>(0)` and merely falls back to 0. The table
is `uint16_t[22]`, so extra points are dropped and out-of-range values wrap.

| File                         | Expected                                                                  |
| ---------------------------- | ------------------------------------------------------------------------- |
| `txgain-scalar.yaml`         | **Clean, and a regression guard** - an earlier checker called this fatal. |
| `value-type-fatal-list.yaml` | A non-numeric list entry: throws, so meshtasticd will not start.          |
| `txgain-out-of-range.yaml`   | `-5` and `70000` wrap to a different gain than written.                   |
| `txgain-too-many.yaml`       | 25 points; everything past the 22nd is dropped.                           |

## Value types, ranges and units

| File                      | Expected                                                                                                                                                |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `value-type-fatal.yaml`   | `Logging.AsciiLogs` is the other no-fallback read: a bad value stops meshtasticd starting.                                                              |
| `value-type-silent.yaml`  | Wrong-typed values where the read has a default: silently replaced, so the setting does nothing.                                                        |
| `tcxo-millivolts.yaml`    | `DIO3_TCXO_VOLTAGE` is in VOLTS and multiplied by 1000, so `1800` silently asks for 1800V. Write `1.8`.                                                 |
| `port-out-of-range.yaml`  | `APIPort` outside 1024-65535 is silently ignored; `Webserver.Port` has no guard at all.                                                                 |
| `statusmessage-long.yaml` | Copied into a `char[80]`, so it is safe but silently shortened to 79 characters.                                                                        |
| `configdir-missing.yaml`  | **Crash regression guard** - an unreadable `ConfigDirectory` used to abort meshtasticd (and `--check`) with SIGABRT via an uncaught `filesystem_error`. |

## MAC address

The MAC no longer determines NodeNum - that comes from the public key - but a MAC
that fails to apply still falls through to the BlueZ and LoRa-serial fallbacks, and
if those yield nothing meshtasticd exits with "Blank MAC Address not allowed!".

| File                      | Expected                                                                                                                                    |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `mac-conflict.yaml`       | Both `MACAddress` and `MACAddressSource`; meshtasticd refuses.                                                                              |
| `mac-malformed.yaml`      | `AA:BB:CC` is under 12 hex digits, so it is silently dropped.                                                                               |
| `mac-source-missing.yaml` | Names an interface with no `/sys/class/net/<n>/address`. Warning, not an error: it is machine-dependent and may be checked on another host. |

## CH341 USB-SPI adapters

`spidev: ch341` is a different hardware model, not a variant of the same one. The Lora
pins become indexes on the adapter and are driven by the usermode USB driver -
`portduinoSetup()` skips `initGPIOPin()` for every one of them - so nothing is claimed
from a gpiochip. This is also the only shape that works on Windows and macOS, which have
no gpiochip, `gpiodetect` or `gpioinfo` to check anything against.

| File                  | Expected                                                                      |
| --------------------- | ----------------------------------------------------------------------------- |
| `usb-ch341.yaml`      | Clean, and the report lists adapter pins rather than resolved gpiochip lines. |
| `ch341-gpiochip.yaml` | A gpiochip and line mapping alongside `ch341`: read, stored, and never used.  |

## Structure

| File                      | Expected                                                                                                                                               |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `duplicate-key.yaml`      | yaml-cpp keeps the FIRST duplicate, so the later value is lost.                                                                                        |
| `nonmap-section.yaml`     | `Lora: invalid` - a known section whose body cannot be read.                                                                                           |
| `unknown-section.yaml`    | A top-level section meshtasticd never reads.                                                                                                           |
| `stranded-key.yaml`       | `spidev` left at the top level instead of inside a section.                                                                                            |
| `top-level-list.yaml`     | Document root is a sequence.                                                                                                                           |
| `empty-file.yaml`         | No content: comments only, which parse to a null document. A warning, not an error.                                                                    |
| `malformed-indent.yaml`   | Will not parse; the report must still name the file and line.                                                                                          |
| `pin-unknown-subkey.yaml` | A pin mapping accepts only `pin`, `gpiochip` and `line`.                                                                                               |
| `pin-unreadable.yaml`     | A non-numeric pin resolves to -1 and trips an assertion at startup.                                                                                    |
| `hub75-unknown-key.yaml`  | An unknown `Display.HUB75` option. On a build without rgbmatrix this also reports the missing HUB75 support, so the test asserts only the unknown key. |

## Across a config directory

`configd-conflict/` is a whole tree: a `config.yaml` pointing at a `config.d/`
holding two more `Lora:` sections. It covers the trap that a key not repeated in
the last-loaded file is reset to its default - here `config.yaml` sets
`DIO3_TCXO_VOLTAGE: 1800` and the effective configuration ends up without it.
The load order within `config.d/` comes from the filesystem, so the report warns
rather than assuming alphabetical order.

`rfswitch-last-wins/` covers `Lora.rfswitch_table` across two `config.d/` files. It
follows the same "last file loaded wins" rule as every other `Lora:` key - the loader
resets a table's pins and mode rows before applying a replacement, so an earlier
file's `MODE_RX` setting cannot leak through a later file that omits it. `--check`
reports this as the standard cross-file-overlap info, not a special-cased error.

`rfswitch-replace/` pins the replacement itself rather than the diagnostic. Which of
two `config.d/` files wins is up to the filesystem, so the fixture above cannot assert
the effective table by value; here the losing table sits in `config.yaml`, which is
always loaded before `config.d/`, and the winner is therefore deterministic. The loser
is the wider of the two - four pins and three mode rows, all `HIGH` - so any carryover
appears as a surviving pin, a surviving mode row, or a `HIGH` that should be `LOW`.
`--output-yaml` is what reports it: the `--check` report says no more than `set`.

## Running these as a normal boot

`malformed-indent.yaml`, `nonmap-section.yaml`, `module-unknown.yaml`,
`mac-conflict.yaml` and `hub75-unknown-key.yaml` are also run _without_ `--check`,
where each must be rejected with a non-zero exit. That is the guard on `--check`
mode not having quietly made the normal path permissive. No other fixture is run
that way: a config meshtasticd accepts makes it boot a node and block.
