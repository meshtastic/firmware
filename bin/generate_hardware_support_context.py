#!/usr/bin/env python3

"""Generate a markdown inventory of board-support metadata and common target-definition macros."""

from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VARIANTS_DIR = ROOT / "variants"
OUTPUT_PATH = ROOT / "docs" / "hardware-support-context.md"

PLATFORMIO_METADATA_KEYS = [
    "custom_meshtastic_hw_model",
    "custom_meshtastic_hw_model_slug",
    "custom_meshtastic_architecture",
    "custom_meshtastic_actively_supported",
    "custom_meshtastic_support_level",
    "custom_meshtastic_display_name",
    "custom_meshtastic_images",
    "custom_meshtastic_tags",
    "custom_meshtastic_requires_dfu",
    "custom_meshtastic_partition_scheme",
]

MACRO_PATTERNS = {
    "Input": [
        "BUTTON_PIN",
        "BUTTON_PIN_TOUCH",
        "ALT_BUTTON_PIN",
        "CANCEL_BUTTON_PIN",
        "ROTARY_",
        "KB_",
        "INPUTDRIVER_",
    ],
    "Radio": [
        "USE_RF95",
        "USE_SX126",
        "USE_SX128",
        "USE_LLCC68",
        "USE_LR11",
        "LORA_",
        "SX126X_",
        "SX128X_",
        "LR1121_",
        "RF95_",
    ],
    "GPS": [
        "HAS_GPS",
        "GPS_",
        "PIN_GPS_",
        "GPS_DEFAULT_NOT_PRESENT",
    ],
    "Display": [
        "HAS_SCREEN",
        "USE_TFTDISPLAY",
        "USE_TFT",
        "USE_SSD",
        "USE_SH",
        "USE_ST",
        "TFT_",
        "OLED_",
        "SCREEN_",
        "PIN_EINK_",
        "EINK_",
        "DISPLAY_",
        "LGFX_",
        "HAS_TFT",
    ],
    "I2C/SPI": [
        "I2C_",
        "SPI_",
        "PIN_SPI",
        "WIRE_",
    ],
    "Power": [
        "BATTERY_",
        "ADC_",
        "PIN_POWER",
        "USE_POWERSAVE",
        "SLEEP_TIME",
        "XPOWERS_",
        "HAS_AXP",
        "HAS_PPM",
        "HAS_BQ",
        "EXT_NOTIFY_OUT",
        "FAN_CTRL_PIN",
        "RF95_FAN_EN",
    ],
    "Connectivity/Other": [
        "HAS_WIFI",
        "HAS_BLUETOOTH",
        "HAS_ETHERNET",
        "HAS_NEOPIXEL",
        "HAS_I2S",
        "HAS_TOUCH",
        "HAS_TOUCHSCREEN",
        "HAS_NFC",
        "NFC_",
        "USE_XL9555",
        "EXPANDS_",
        "PCF",
        "RTC",
    ],
}


def classify_macro(name: str) -> str:
    for category, prefixes in MACRO_PATTERNS.items():
        for prefix in prefixes:
            if name.startswith(prefix):
                return category
    return "Other"


def parse_platformio_envs(path: Path) -> list[dict[str, object]]:
    envs: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            if current:
                envs.append(current)
            current = {"section": line[1:-1], "metadata": {}}
            continue
        if current is None or "=" not in line:
            continue
        key, value = [part.strip() for part in line.split("=", 1)]
        if key in PLATFORMIO_METADATA_KEYS:
            current["metadata"][key] = value
    if current:
        envs.append(current)
    return envs


def parse_variant_macros(path: Path) -> tuple[dict[str, str], Counter[str]]:
    defines: dict[str, str] = {}
    category_counts: Counter[str] = Counter()
    pattern = re.compile(r"^#define\s+([A-Za-z0-9_]+)(?:\s+(.*?))?\s*(?://.*)?$")
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(raw_line.strip())
        if not match:
            continue
        name = match.group(1)
        value = (match.group(2) or "1").strip()
        defines[name] = value
        category_counts[classify_macro(name)] += 1
    return defines, category_counts


MACRO_DESCRIPTIONS: dict[str, str] = {
    # ---------- Input ----------
    "BUTTON_PIN":                    "Primary user button GPIO pin number.",
    "ALT_BUTTON_PIN":                "Secondary / alternate button pin, used on boards with two buttons.",
    "CANCEL_BUTTON_PIN":             "Button wired to cancel/back actions (e.g., T-Deck cancel key).",
    "BUTTON_NEED_PULLUP":            "Set to 1 when the button GPIO requires the internal pull-up to be enabled.",
    "BUTTON_PIN_ALT":                "Alternative spelling used by some older board families for a second button.",
    "KB_BL_PIN":                     "Keyboard backlight control pin (e.g., T-LoRa Pager keyboard).",
    "KB_INT":                        "Keyboard interrupt input pin; signals a keypress to the MCU.",
    "KB_POWERON":                    "Pin used to power-on or enable the keyboard peripheral.",
    "KB_SLAVE_ADDRESS":              "I2C address of the keyboard controller IC.",
    "INPUTDRIVER_ENCODER_TYPE":      "Selects the rotary encoder driver variant (0 = none, 1+ = specific type).",
    "INPUTDRIVER_TWO_WAY_ROCKER":    "Enables the two-way rocker input driver.",
    "INPUTDRIVER_TWO_WAY_ROCKER_RIGHT": "GPIO pin for the rightward rocker direction.",
    "INPUTDRIVER_TWO_WAY_ROCKER_LEFT":  "GPIO pin for the leftward rocker direction.",
    "INPUTDRIVER_TWO_WAY_ROCKER_BTN":   "GPIO pin for the rocker click/press direction.",
    "ROTARY_A":                      "First encoder channel pin (clock / A signal).",
    "ROTARY_B":                      "Second encoder channel pin (data / B signal).",
    # ---------- Radio ----------
    "USE_SX1262":                    "Select the SX1262 sub-GHz LoRa chip driver.",
    "USE_SX1268":                    "Select the SX1268 sub-GHz LoRa chip driver (higher power variant of SX1262).",
    "USE_SX1280":                    "Select the SX1280 2.4 GHz LoRa chip driver.",
    "USE_RF95":                      "Select the RFM95/SX1276 legacy LoRa chip driver.",
    "USE_LLCC68":                    "Select the LLCC68 low-cost LoRa chip driver.",
    "USE_LR1110":                    "Select the LR1110 wideband radio driver.",
    "USE_LR1120":                    "Select the LR1120 wideband radio driver.",
    "USE_LR1121":                    "Select the LR1121 wideband radio driver.",
    "LORA_CS":                       "LoRa radio SPI chip-select GPIO pin.",
    "LORA_RESET":                    "LoRa radio hardware-reset GPIO pin (active low).",
    "LORA_DIO0":                     "LoRa radio DIO0 interrupt pin (RF95/SX1276 done/RxDone).",
    "LORA_DIO1":                     "LoRa radio DIO1 interrupt pin; on SX126x this is the primary IRQ line.",
    "LORA_DIO2":                     "LoRa radio DIO2 pin; on RF95 used for FSK interrupt; on SX126x tx/rx control.",
    "LORA_SCK":                      "LoRa radio SPI clock pin.",
    "LORA_MISO":                     "LoRa radio SPI MISO pin.",
    "LORA_MOSI":                     "LoRa radio SPI MOSI pin.",
    "SX126X_CS":                     "SX126x chip-select pin (often same as LORA_CS).",
    "SX126X_RESET":                  "SX126x reset pin (often same as LORA_RESET).",
    "SX126X_BUSY":                   "SX126x BUSY pin; must be polled low before issuing SPI commands.",
    "SX126X_DIO1":                   "SX126x DIO1 interrupt output used for all IRQ events.",
    "SX126X_DIO3_TCXO_VOLTAGE":      "Drives the TCXO regulator via DIO3; set to the supply voltage (e.g., 1.8).",
    "SX126X_DIO2_AS_RF_SWITCH":      "Set to 1 so the driver controls the TX/RX RF switch via DIO2.",
    "LR1121_IRQ_PIN":                "LR1121 interrupt request pin.",
    "RF95_FAN_EN":                   "Enables a cooling fan for high-power RF95 installations.",
    # ---------- GPS ----------
    "HAS_GPS":                       "Set to 1 if the board has an on-board GPS receiver; 0 to disable GPS entirely.",
    "GPS_RX_PIN":                    "UART RX pin connected to the GPS module TX output.",
    "GPS_TX_PIN":                    "UART TX pin connected to the GPS module RX input.",
    "PIN_GPS_EN":                    "GPIO to power-enable or power-gate the GPS module.",
    "PIN_GPS_PPS":                   "GPS pulse-per-second input pin for timing synchronisation.",
    "PIN_GPS_STANDBY":               "Places the GPS into standby/low-power mode when driven.",
    "PIN_GPS_RESET":                 "Hardware-reset line to the GPS module.",
    "PIN_GPS_REINIT":                "Pin used to trigger GPS re-initialisation sequences.",
    "GPS_THREAD_INTERVAL":           "Millisecond poll interval for the GPS background thread.",
    "GPS_BAUDRATE":                  "UART baud rate for GPS serial communication.",
    "GPS_L76K":                      "Selects the Quectel L76K GPS driver and protocol.",
    "GPS_UBLOX":                     "Selects the u-blox GPS driver and UBX protocol.",
    "GPS_EN_ACTIVE":                 "Logic level (HIGH or LOW) that enables the GPS power pin.",
    "GPS_RESET_MODE":                "Defines the reset signal polarity or protocol for the GPS chip.",
    "GPS_DEFAULT_NOT_PRESENT":       "Compiled-in default assuming no GPS; overridden at runtime if detected.",
    # ---------- Display ----------
    "HAS_SCREEN":                    "Set to 1 if the board has any display hardware.",
    "USE_TFTDISPLAY":                "Selects the TFT LCD driver path.",
    "USE_SSD1306":                   "Selects the SSD1306 128×64 OLED driver over I2C.",
    "USE_SH1106":                    "Selects the SH1106 128×64 OLED driver.",
    "USE_ST7735":                    "Selects the ST7735 TFT SPI driver.",
    "USE_ST7789":                    "Selects the ST7789 TFT SPI driver.",
    "TFT_WIDTH":                     "Horizontal pixel resolution of the TFT display.",
    "TFT_HEIGHT":                    "Vertical pixel resolution of the TFT display.",
    "TFT_OFFSET_X":                  "Horizontal pixel offset for display alignment correction.",
    "TFT_OFFSET_Y":                  "Vertical pixel offset for display alignment correction.",
    "TFT_BL":                        "Backlight control PWM or GPIO pin for the TFT panel.",
    "SCREEN_ROTATE":                 "Non-zero value rotates the display 180° for mounted-upside-down screens.",
    "SCREEN_TRANSITION_FRAMERATE":   "Target framerate for UI animations and transitions.",
    "PIN_EINK_CS":                   "E-ink display SPI chip-select pin.",
    "PIN_EINK_BUSY":                 "E-ink display BUSY output; high when a page update is in progress.",
    "PIN_EINK_DC":                   "E-ink display data/command select pin.",
    "PIN_EINK_RES":                  "E-ink display hardware-reset pin.",
    "PIN_EINK_SCLK":                 "E-ink display SPI clock pin.",
    "PIN_EINK_MOSI":                 "E-ink display SPI MOSI pin.",
    # ---------- I2C/SPI ----------
    "I2C_SDA":                       "Primary I2C data line GPIO pin.",
    "I2C_SCL":                       "Primary I2C clock line GPIO pin.",
    "PIN_SPI_MISO":                  "Primary SPI MISO (data from peripheral) GPIO pin.",
    "PIN_SPI_MOSI":                  "Primary SPI MOSI (data to peripheral) GPIO pin.",
    "PIN_SPI_SCK":                   "Primary SPI clock GPIO pin.",
    "SPI_INTERFACES_COUNT":          "Number of hardware SPI buses available on this board.",
    "WIRE_INTERFACES_COUNT":         "Number of hardware I2C buses available on this board.",
    "PIN_SPI1_MISO":                 "Secondary SPI bus MISO pin.",
    "PIN_SPI1_MOSI":                 "Secondary SPI bus MOSI pin.",
    "PIN_SPI1_SCK":                  "Secondary SPI bus clock pin.",
    "SPI_FREQUENCY":                 "Default SPI clock frequency in Hz for this board.",
    "SPI_READ_FREQUENCY":            "Reduced SPI clock rate used for read transactions.",
    "SPI_SCK":                       "SPI clock pin alias used in older board files.",
    "SPI_MOSI":                      "SPI MOSI pin alias used in older board files.",
    "SPI_MISO":                      "SPI MISO pin alias used in older board files.",
    # ---------- Power ----------
    "BATTERY_PIN":                   "ADC input GPIO connected to the battery voltage divider.",
    "ADC_MULTIPLIER":                "Floating-point scale factor to convert raw ADC reading to battery voltage.",
    "ADC_CHANNEL":                   "ADC channel enum or number for the battery sense input.",
    "BATTERY_SENSE_RESOLUTION_BITS": "ADC resolution in bits used for battery voltage sampling.",
    "ADC_RESOLUTION":                "Board-level ADC resolution definition, referenced by other power macros.",
    "BATTERY_SENSE_RESOLUTION":      "Alias for the effective ADC resolution for battery sense.",
    "ADC_CTRL":                      "GPIO that enables or gates the ADC voltage-divider circuit.",
    "ADC_CTRL_ENABLED":              "Logic level (HIGH or LOW) that turns on the ADC control switch.",
    "ADC_ATTENUATION":               "ESP32 ADC input attenuation setting; controls measurable voltage range.",
    "BATTERY_SENSE_SAMPLES":         "Number of ADC samples to average for a stable battery reading.",
    "EXT_NOTIFY_OUT":                "GPIO output used to signal an external LED or buzzer for notifications.",
    "USE_POWERSAVE":                 "Enables aggressive power-save mode (deep sleep, reduced poll intervals).",
    "SLEEP_TIME":                    "Default light-sleep duration in milliseconds between wakeups.",
    "PIN_POWER_EN":                  "GPIO to assert to enable a board power rail or load switch.",
    "HAS_PPM":                       "Set to 1 if the board has an IP5306 or similar PPM power path IC.",
    # ---------- Connectivity/Other ----------
    "HAS_TOUCHSCREEN":               "Set to 1 for boards with a capacitive or resistive touch panel.",
    "HAS_NEOPIXEL":                  "Set to 1 if the board has addressable RGB LEDs (WS2812 / NeoPixel).",
    "PCF8563_RTC":                   "I2C address of the PCF8563 real-time clock IC.",
    "PCF85063_RTC":                  "I2C address of the PCF85063 real-time clock IC.",
    "HAS_ETHERNET":                  "Set to 1 for boards with a wired Ethernet interface.",
    "HAS_I2S":                       "Set to 1 if I2S audio output is present.",
    "NFC_INT":                       "Interrupt pin from the NFC controller IC.",
    "NFC_CS":                        "SPI chip-select for the NFC controller.",
    "USE_XL9555":                    "Enables the XL9555 16-bit I2C GPIO expander driver.",
    "EXPANDS_DRV_EN":                "GPIO expander pin used to enable the haptic driver.",
    "EXPANDS_AMP_EN":                "GPIO expander pin used to power-on the audio amplifier.",
    "EXPANDS_KB_RST":                "GPIO expander pin used to reset the keyboard controller.",
    "EXPANDS_LORA_EN":               "GPIO expander pin used to power-gate the LoRa radio.",
    "EXPANDS_GPS_EN":                "GPIO expander pin used to power-gate the GPS module.",
    "EXPANDS_NFC_EN":                "GPIO expander pin used to power-gate the NFC controller.",
    # ---------- Other ----------
    "LED_POWER":                     "GPIO for the status LED, defines the LED pin number.",
    "LED_STATE_ON":                  "Logic level (HIGH or LOW) that turns the status LED on.",
    "PIN_SERIAL1_RX":                "Secondary UART RX pin (used for accessories, GPS on some boards).",
    "PIN_SERIAL1_TX":                "Secondary UART TX pin.",
    "PIN_WIRE_SDA":                  "Arduino-framework I2C SDA pin alias (nRF52 / RP2040 style).",
    "PIN_WIRE_SCL":                  "Arduino-framework I2C SCL pin alias.",
    "PIN_LED1":                      "First LED GPIO pin in the nRF52 Arduino BSP pin table.",
    "VARIANT_MCK":                   "Crystal oscillator frequency in Hz for nRF52 variant clock configuration.",
    "PINS_COUNT":                    "Total number of GPIO pins defined in the Arduino BSP variant table.",
    "NUM_DIGITAL_PINS":              "Count of digital-capable pins in the BSP variant table.",
    "NUM_ANALOG_INPUTS":             "Count of analog-input pins in the BSP variant table.",
    "NUM_ANALOG_OUTPUTS":            "Count of analog-output (DAC) pins in the BSP variant table.",
    "LED_BLUE":                      "GPIO number of the blue status LED (typical on nRF52 and RP2040 boards).",
    "USE_LFXO":                      "Instructs the nRF52 BSP to use the low-frequency crystal oscillator.",
    "BUTTON_NEED_PULLUP":            "Enables internal pull-up on the button GPIO (duplicate entry for clarity).",
}


def shorten(value: str, limit: int = 60) -> str:
    compact = " ".join(value.split())
    return compact if len(compact) <= limit else compact[: limit - 3] + "..."


# Architecture families known to rely heavily on BSP or base-environment defaults rather
# than declaring every field locally. Used in the Inherited Defaults Note section.
_BSP_DEFAULT_FAMILIES: dict[str, list[str]] = {
    "nrf52840": [
        "VARIANT_MCK — nRF52 BSP clock constant (e.g., 64000000ul); always inherited from BSP unless overridden.",
        "USE_LFXO — low-frequency crystal oscillator selection; declared locally only when the board uses LFXO rather than the RC oscillator.",
        "PIN_SPI_* / PIN_SPI1_* — SPI bus pin numbers come from the BSP variant table; boards override only when the LoRa radio or display uses non-default SPI routing.",
        "WIRE_INTERFACES_COUNT / SPI_INTERFACES_COUNT — bus count comes from BSP; explicitly set only when the board deviates.",
        "LED_BLUE / PIN_LED1 / PINS_COUNT / NUM_DIGITAL_PINS — standard BSP pin-table entries inherited from the nRF52 Arduino core.",
    ],
    "rp2040": [
        "PIN_SPI_* — primary SPI pins come from the RP2040 Arduino BSP; most boards declare them explicitly, but the defaults align with the Pico pin assignments.",
        "NUM_DIGITAL_PINS / NUM_ANALOG_INPUTS — Arduino BSP counts; rarely overridden locally.",
    ],
    "stm32": [
        "Radio and pin assignments for STM32WL targets are largely internal to the WL SoC and declared via STM32 HAL/BSP headers; variant.h files are minimal.",
        "USE_STM32WLx is typically the only explicit define; all other radio config comes from the BSP.",
    ],
    "native": [
        "The native/Portduino target uses runtime configuration rather than compile-time pin defines; variant.h only sets display and GPS stubs.",
    ],
}


def collect_inventory() -> dict[str, object]:
    architectures: defaultdict[str, list[dict[str, object]]] = defaultdict(list)
    category_frequency: defaultdict[str, Counter[str]] = defaultdict(Counter)
    total_variant_dirs = 0
    total_envs = 0
    no_variant_h: defaultdict[str, int] = defaultdict(int)  # arch -> count of dirs with no variant.h
    has_metadata: int = 0  # env count that has at least one custom_meshtastic_* key

    for platformio_path in sorted(VARIANTS_DIR.glob("**/platformio.ini")):
        variant_dir = platformio_path.parent
        total_variant_dirs += 1
        variant_path = variant_dir / "variant.h"
        board_level_arch_raw = variant_dir.parts[-2] if len(variant_dir.parts) >= 2 else "unknown"
        if not variant_path.exists():
            no_variant_h[board_level_arch_raw] += 1
            continue

        envs = parse_platformio_envs(platformio_path)
        defines, category_counts = parse_variant_macros(variant_path)
        total_envs += len(envs)

        board_level_arch = board_level_arch_raw
        for env in envs:
            section = str(env["section"])
            if not section.startswith("env:"):
                continue
            metadata = dict(env["metadata"])
            if metadata:
                has_metadata += 1
            architecture = str(metadata.get("custom_meshtastic_architecture") or board_level_arch)
            if architecture == "esp32s3":
                architecture = "esp32-s3"
            if architecture == "esp32c3":
                architecture = "esp32-c3"
            if architecture == "esp32c6":
                architecture = "esp32-c6"

            entry = {
                "environment": section.split(":", 1)[1],
                "variant_dir": str(variant_dir.relative_to(ROOT)),
                "metadata": metadata,
                "defines": defines,
                "category_counts": category_counts,
            }
            architectures[architecture].append(entry)

            for name in defines:
                category = classify_macro(name)
                category_frequency[category][name] += 1

    return {
        "architectures": architectures,
        "category_frequency": category_frequency,
        "total_variant_dirs": total_variant_dirs,
        "total_envs": total_envs,
        "no_variant_h": dict(no_variant_h),
        "has_metadata": has_metadata,
    }


def render_markdown(inventory: dict[str, object]) -> str:
    architectures = inventory["architectures"]
    category_frequency = inventory["category_frequency"]
    lines: list[str] = []

    lines.append("# Hardware Support Context")
    lines.append("")
    lines.append("This document inventories the board-support inputs and common target-definition fields")
    lines.append("currently used in the Meshtastic firmware repository. It is intended as reusable context")
    lines.append("for adding new hardware support without re-discovering naming patterns, metadata keys, and")
    lines.append("frequently used pin or capability macros from scratch.")
    lines.append("")
    lines.append("## Scope")
    lines.append("")
    lines.append(f"- Variant directories scanned: {inventory['total_variant_dirs']}")
    lines.append(f"- PlatformIO environments summarized: {inventory['total_envs']}")
    lines.append("- Sources: `variants/**/platformio.ini` and `variants/**/variant.h`")
    lines.append("- Notes: This inventory reflects explicit per-variant declarations. Some boards also inherit")
    lines.append("  defaults from architecture headers or shared base environments, which must still be checked")
    lines.append("  before creating new hardware support.")
    lines.append("")
    lines.append("## Repository Metadata Inputs")
    lines.append("")
    lines.append("The following `custom_meshtastic_*` metadata keys are already used across board environments:")
    lines.append("")
    for key in PLATFORMIO_METADATA_KEYS:
        lines.append(f"- `{key}`")
    lines.append("")
    lines.append("## Common Target-Definition Categories")
    lines.append("")
    for category in ["Input", "Radio", "GPS", "Display", "I2C/SPI", "Power", "Connectivity/Other", "Other"]:
        counter = category_frequency.get(category, Counter())
        if not counter:
            continue
        lines.append(f"### {category}")
        lines.append("")
        lines.append("| Macro | Used in | Description |")
        lines.append("| --- | --- | --- |")
        for name, count in counter.most_common(15):
            description = MACRO_DESCRIPTIONS.get(name, "")
            lines.append(f"| `{name}` | {count} variants | {description} |")
        lines.append("")

    lines.append("## Architecture and Environment Inventory")
    lines.append("")
    for architecture in sorted(architectures):
        entries = sorted(architectures[architecture], key=lambda item: item["environment"])
        lines.append(f"### {architecture}")
        lines.append("")
        lines.append("| Environment | Display Name | HW Model | HW Slug | Variant Dir | Common Categories |")
        lines.append("| --- | --- | --- | --- | --- | --- |")
        for entry in entries:
            metadata = entry["metadata"]
            category_counts = entry["category_counts"]
            categories = []
            for category in ["Display", "Radio", "Input", "GPS", "Power", "Connectivity/Other"]:
                count = category_counts.get(category, 0)
                if count:
                    categories.append(f"{category}:{count}")
            category_summary = ", ".join(categories) if categories else "none"
            lines.append(
                "| {environment} | {display_name} | {hw_model} | {hw_slug} | {variant_dir} | {categories} |".format(
                    environment=entry["environment"],
                    display_name=metadata.get("custom_meshtastic_display_name", ""),
                    hw_model=metadata.get("custom_meshtastic_hw_model", ""),
                    hw_slug=metadata.get("custom_meshtastic_hw_model_slug", ""),
                    variant_dir=entry["variant_dir"],
                    categories=category_summary,
                )
            )
        lines.append("")

    lines.append("## Representative Board Examples")
    lines.append("")
    for architecture in sorted(architectures):
        entries = sorted(architectures[architecture], key=lambda item: item["environment"])
        if not entries:
            continue
        sample = entries[0]
        lines.append(f"### {architecture}: `{sample['environment']}`")
        lines.append("")
        lines.append(f"- Variant directory: `{sample['variant_dir']}`")
        metadata = sample["metadata"]
        for key in PLATFORMIO_METADATA_KEYS:
            value = metadata.get(key)
            if value:
                lines.append(f"- `{key}`: `{value}`")
        defines = sample["defines"]
        for category in ["Input", "Radio", "GPS", "Display", "I2C/SPI", "Power", "Connectivity/Other"]:
            selected = [name for name in defines if classify_macro(name) == category][:8]
            if not selected:
                continue
            lines.append(f"- {category} examples:")
            for name in selected:
                lines.append(f"  - `{name}` = `{shorten(defines[name])}`")
        lines.append("")

    lines.append("## Intake Guidance For New Hardware")
    lines.append("")
    lines.append("When using this context to add a new board, collect these inputs before generating files:")
    lines.append("")
    lines.append("- PlatformIO environment name")
    lines.append("- `custom_meshtastic_hw_model` and `custom_meshtastic_hw_model_slug`")
    lines.append("- Display name and architecture")
    lines.append("- Whether the board is actively supported and its support level")
    lines.append("- Partition scheme, DFU requirement, and image/tag metadata if applicable")
    lines.append("- Radio chip family and complete radio pin group")
    lines.append("- Input/button/rotary/keyboard pins")
    lines.append("- Display interface pins and driver-related macros")
    lines.append("- GPS, power-management, I2C, SPI, storage, and auxiliary peripheral definitions")
    lines.append("- Any board-specific initialization that requires `variant.cpp` or extra variant hooks")
    lines.append("")
    lines.append("## Inherited Defaults Note")
    lines.append("")
    lines.append(
        "Some architecture families rely on BSP (Board Support Package) or base-environment defaults "
        "rather than declaring every pin or capability macro explicitly in `variant.h`. "
        "When adding a new board for one of these families, check the relevant BSP headers before "
        "assuming a missing define means a feature is absent."
    )
    lines.append("")
    no_variant_h = inventory.get("no_variant_h", {})
    if no_variant_h:
        lines.append(
            f"Directories scanned that had no `variant.h` "
            f"(relying entirely on BSP/base-environment): "
            + ", ".join(f"{arch}: {count}" for arch, count in sorted(no_variant_h.items()))
        )
        lines.append("")
    for family, notes in _BSP_DEFAULT_FAMILIES.items():
        lines.append(f"### {family}")
        lines.append("")
        for note in notes:
            lines.append(f"- {note}")
        lines.append("")
    lines.append("## Cautions")
    lines.append("")
    lines.append("- Some boards rely on architecture defaults rather than declaring every field locally.")
    lines.append("- Some board families expose multiple environments or display variants that share one hardware model.")
    lines.append("- Source materials such as schematics still need human verification before new pin mappings are trusted.")
    lines.append("- This document is a starting context artifact, not proof that a new board definition is safe to merge.")
    lines.append("")
    return "\n".join(lines)


def print_validate_summary(inventory: dict[str, object]) -> None:
    """Print a validation summary to stdout without writing the output file."""
    total_dirs = inventory["total_variant_dirs"]
    total_envs = inventory["total_envs"]
    has_metadata = inventory["has_metadata"]
    no_variant_h = inventory.get("no_variant_h", {})
    no_variant_h_total = sum(no_variant_h.values())
    architectures = inventory["architectures"]
    arch_count = len(architectures)

    print("Hardware Support Context — Validation Summary")
    print("=" * 48)
    print(f"Variant directories scanned : {total_dirs}")
    print(f"Directories with no variant.h (BSP-only) : {no_variant_h_total}")
    if no_variant_h:
        for arch, count in sorted(no_variant_h.items()):
            print(f"  {arch}: {count}")
    print(f"PlatformIO environments found : {total_envs}")
    print(f"Environments with custom_meshtastic_* metadata : {has_metadata}")
    print(f"Architecture families represented : {arch_count}")
    print("")
    print("Architecture families:")
    for arch in sorted(architectures):
        entries = architectures[arch]
        print(f"  {arch}: {len(entries)} environment(s)")
    print("")
    print("BSP-default families with special notes:")
    for family in _BSP_DEFAULT_FAMILIES:
        marker = "YES" if family in {a.split("-")[0] for a in architectures} else "not in scan"
        print(f"  {family}: {marker}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate hardware support context markdown")
    parser.add_argument("--output", default=str(OUTPUT_PATH), help="Output markdown path")
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Print a validation summary (variant counts, BSP-only dirs, metadata coverage) without writing output",
    )
    args = parser.parse_args()

    inventory = collect_inventory()

    if args.validate:
        print_validate_summary(inventory)
        return

    markdown = render_markdown(inventory)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(markdown + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()