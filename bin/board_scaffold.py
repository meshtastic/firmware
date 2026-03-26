#!/usr/bin/env python3
"""Scaffold board-support files from a validated intake assessment."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from board_intake import (
    BoardIntakeRequest,
    EvidenceGap,
    IntakeAssessment,
    assess_intake,
    load_hardware_context,
    render_assessment_markdown,
)

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = ROOT / "generated" / "hardware-support"

ARCH_BASE_ENV = {
    "esp32": "esp32_base",
    "esp32-s3": "esp32s3_base",
    "esp32-c3": "esp32c3_base",
    "esp32-c6": "esp32c6_base",
    "esp32s2": "esp32s2_base",
    "nrf52840": "nrf52_base",
    "rp2040": "rp2040_base",
    "rp2350": "rp2350_base",
    "stm32": "stm32_base",
    "native": "native_base",
}

ARCH_VARIANT_ROOT = {
    "esp32": "esp32",
    "esp32-s3": "esp32s3",
    "esp32-c3": "esp32c3",
    "esp32-c6": "esp32c6",
    "esp32s2": "esp32s2",
    "nrf52840": "nrf52840",
    "rp2040": "rp2040",
    "rp2350": "rp2350",
    "stm32": "stm32",
    "native": "native",
}

PLACEHOLDER_DEFINES = {
    "status": [
        "#define LED_PIN  // TODO: verify status LED pin if present",
    ],
    "input": [
        "#define BUTTON_PIN  // TODO: verify user button pin",
    ],
    "display": [
        "#define HAS_SCREEN 1",
        "#define USE_SSD1306 // TODO: verify display controller",
        "#define I2C_SCL  // TODO: verify display I2C clock pin",
        "#define I2C_SDA  // TODO: verify display I2C data pin",
    ],
    "GPS": [
        "#define GPS_RX_PIN  // TODO: verify GPS RX pin or remove if GPS absent",
        "#define GPS_TX_PIN  // TODO: verify GPS TX pin or remove if GPS absent",
    ],
    "power": [
        "#define BATTERY_PIN  // TODO: verify battery sense pin",
        "#define ADC_MULTIPLIER  // TODO: verify voltage divider ratio",
    ],
    "radio": [
        "#define USE_SX1262 // TODO: verify radio chip selection from schematic",
        "#define SX126X_CS  // TODO: verify radio chip-select pin",
        "#define SX126X_BUSY  // TODO: verify radio busy pin",
        "#define SX126X_DIO1  // TODO: verify radio IRQ pin",
        "#define SX126X_RESET  // TODO: verify radio reset pin",
        "#define LORA_SCK  // TODO: verify radio SPI clock pin",
        "#define LORA_MISO  // TODO: verify radio SPI MISO pin",
        "#define LORA_MOSI  // TODO: verify radio SPI MOSI pin",
    ],
}

DEFINE_PATTERN = re.compile(r"^#define\s+([A-Za-z0-9_]+)(?:\s+(.*?))?\s*(?://.*)?$")

CATEGORY_MACROS = {
    "status": ["LED_POWER", "LED_PIN", "LED_STATE_ON", "VEXT_ENABLE"],
    "input": ["BUTTON_PIN", "BUTTON_NEED_PULLUP", "ROTARY_A", "ROTARY_B"],
    "display": [
        "HAS_SCREEN",
        "USE_SSD1306",
        "USE_SH1106",
        "USE_TFTDISPLAY",
        "USE_ST7789",
        "I2C_SCL",
        "I2C_SDA",
        "I2C_SCL1",
        "I2C_SDA1",
        "TFT_HEIGHT",
        "TFT_WIDTH",
        "TFT_OFFSET_X",
        "TFT_OFFSET_Y",
        "SCREEN_ROTATE",
        "SCREEN_TRANSITION_FRAMERATE",
    ],
    "GPS": [
        "HAS_GPS",
        "GPS_RX_PIN",
        "GPS_TX_PIN",
        "GPS_BAUDRATE",
        "PIN_GPS_PPS",
        "GPS_THREAD_INTERVAL",
    ],
    "power": [
        "ADC_CTRL",
        "ADC_CTRL_ENABLED",
        "BATTERY_PIN",
        "ADC_CHANNEL",
        "ADC_ATTENUATION",
        "ADC_MULTIPLIER",
        "USE_POWERSAVE",
        "SLEEP_TIME",
    ],
    "radio": [
        "USE_SX1262",
        "USE_SX1268",
        "USE_LR1121",
        "USE_SX1280",
        "LORA_DIO0",
        "LORA_RESET",
        "LORA_DIO1",
        "LORA_DIO2",
        "LORA_SCK",
        "LORA_MISO",
        "LORA_MOSI",
        "LORA_CS",
        "SX126X_CS",
        "SX126X_DIO1",
        "SX126X_BUSY",
        "SX126X_RESET",
        "SX126X_DIO2_AS_RF_SWITCH",
        "SX126X_DIO3_TCXO_VOLTAGE",
        "LR1121_IRQ_PIN",
        "LR1121_NRESET_PIN",
        "LR1121_BUSY_PIN",
        "LR1121_SPI_NSS_PIN",
        "LR1121_SPI_SCK_PIN",
        "LR1121_SPI_MOSI_PIN",
        "LR1121_SPI_MISO_PIN",
        "LR11X0_DIO3_TCXO_VOLTAGE",
        "LR11X0_DIO_AS_RF_SWITCH",
    ],
}


def slugify_variant_dir(req: BoardIntakeRequest) -> str:
    return req.environment_name.replace("_", "-").lower()


def target_variant_dir(req: BoardIntakeRequest) -> Path:
    arch_root = ARCH_VARIANT_ROOT.get(req.architecture, req.architecture)
    return Path("variants") / arch_root / slugify_variant_dir(req)


def pick_pattern(assessment: IntakeAssessment) -> dict | None:
    return assessment.matched_patterns[0] if assessment.matched_patterns else None


def source_basis_lines(assessment: IntakeAssessment, context: dict) -> list[str]:
    lines = [
        f"// Intake environment: {assessment.request.environment_name}",
        f"// Intake architecture: {assessment.request.architecture}",
    ]
    pattern = pick_pattern(assessment)
    if pattern:
        lines.append(
            "// Repository pattern basis: "
            f"{pattern.get('environment', '')} -> {pattern.get('variant_dir', '')}"
        )
    lines.append(
        f"// Context source: {context.get('context_path', 'docs/hardware-support-context.md')}"
    )
    return lines


def parse_variant_defines(variant_path: Path) -> dict[str, str]:
    defines: dict[str, str] = {}
    if not variant_path.exists():
        return defines

    for raw_line in variant_path.read_text(encoding="utf-8").splitlines():
        match = DEFINE_PATTERN.match(raw_line.strip())
        if not match:
            continue
        name = match.group(1)
        value = (match.group(2) or "1").strip()
        defines[name] = value
    return defines


def infer_category_lines(pattern_variant_path: Path, category: str) -> list[str]:
    defines = parse_variant_defines(pattern_variant_path)
    lines: list[str] = []
    for macro_name in CATEGORY_MACROS[category]:
        value = defines.get(macro_name)
        if value is None:
            continue
        if value == "1":
            lines.append(f"#define {macro_name}")
        else:
            lines.append(f"#define {macro_name} {value}")

    if lines:
        return lines
    return PLACEHOLDER_DEFINES[category]


# T028


def generate_variant_h(assessment: IntakeAssessment, context: dict) -> str:
    req = assessment.request
    lines: list[str] = []
    lines.extend(source_basis_lines(assessment, context))
    lines.append("")
    lines.append("#pragma once")
    lines.append("")

    pattern = pick_pattern(assessment)
    pattern_variant_path = None
    if pattern and pattern.get("variant_dir"):
        pattern_variant_path = (ROOT / str(pattern["variant_dir"]) / "variant.h").resolve()

    for category in ("status", "input", "display", "GPS", "power", "radio"):
        lines.append(f"// {category}")
        if pattern_variant_path is not None:
            lines.extend(infer_category_lines(pattern_variant_path, category))
        else:
            lines.extend(PLACEHOLDER_DEFINES[category])
        lines.append("")

    lines.append("// Board identity")
    macro_name = req.hardware_model_slug or req.environment_name.upper().replace(
        "-", "_"
    )
    lines.append(f"#define {macro_name} 1")
    lines.append("")

    return "\n".join(lines).rstrip() + "\n"


# T029


def generate_platformio_env(assessment: IntakeAssessment) -> str:
    req = assessment.request
    extends = ARCH_BASE_ENV.get(req.architecture, f"{req.architecture}_base")
    variant_dir = target_variant_dir(req)
    build_define = (
        req.hardware_model_slug or req.environment_name.upper().replace("-", "_")
    ).replace(" ", "_")

    lines = [
        f"[env:{req.environment_name}]",
        f"custom_meshtastic_hw_model = {req.hardware_model}",
        f"custom_meshtastic_hw_model_slug = {req.hardware_model_slug or 'TODO_SET_HW_MODEL_SLUG'}",
        f"custom_meshtastic_architecture = {req.architecture}",
        f"custom_meshtastic_actively_supported = {str(req.actively_supported).lower() if req.actively_supported is not None else 'TODO_SET_ACTIVELY_SUPPORTED'}",
        f"custom_meshtastic_support_level = {req.support_level or 'TODO_SET_SUPPORT_LEVEL'}",
        f"custom_meshtastic_display_name = {req.display_name}",
        "custom_meshtastic_images = TODO_SET_IMAGES",
        "custom_meshtastic_tags = TODO_SET_TAGS",
        "custom_meshtastic_requires_dfu = TODO_SET_REQUIRES_DFU",
        "custom_meshtastic_partition_scheme = TODO_SET_PARTITION_SCHEME",
        "",
        "board = TODO_SET_PLATFORMIO_BOARD",
        f"extends = {extends}",
        "build_flags =",
        f"  ${{{extends}.build_flags}}",
        f"  -D {build_define}",
        f"  -I {variant_dir.as_posix()}",
    ]
    return "\n".join(lines).rstrip() + "\n"


# T030


def annotate_unresolved(content: str, gaps: list[EvidenceGap], is_ini: bool = False) -> str:
    annotations = [
        gap
        for gap in gaps
        if not gap.blocking
        and gap.category in {"radio", "display", "input", "GPS", "power", "metadata"}
    ]
    if not annotations:
        return content

    lines = content.splitlines()
    todo_lines = [f"; TODO: verify — {gap.description}" for gap in annotations]
    
    if is_ini:
        # For INI files, insert TODOs after the first section header
        if lines and lines[0].startswith("["):
            return "\n".join(lines[:1] + todo_lines + [""] + lines[1:]) + "\n"
    else:
        # For .h files, prepend TODOs with C++ comment syntax
        todo_lines = [f"// TODO: verify — {gap.description}" for gap in annotations]
        return "\n".join(todo_lines + [""] + lines) + "\n"
    
    return content


# T031


def scaffold_board(
    assessment: IntakeAssessment, context: dict, output_dir: Path
) -> dict[str, Path]:
    req = assessment.request
    variant_dir = output_dir / target_variant_dir(req)
    variant_dir.mkdir(parents=True, exist_ok=True)

    variant_h_path = variant_dir / "variant.h"
    platformio_path = variant_dir / "platformio.ini"

    variant_h_content = annotate_unresolved(
        generate_variant_h(assessment, context), assessment.evidence_gaps, is_ini=False
    )
    platformio_content = annotate_unresolved(
        generate_platformio_env(assessment), assessment.evidence_gaps, is_ini=True
    )

    variant_h_path.write_text(variant_h_content, encoding="utf-8")
    platformio_path.write_text(platformio_content, encoding="utf-8")

    generated = {
        "variant_h": variant_h_path,
        "platformio": platformio_path,
    }

    if req.architecture in {"esp32", "esp32-s3", "esp32-c3", "esp32-c6"}:
        variant_cpp_path = variant_dir / "variant.cpp"
        variant_cpp_content = annotate_unresolved(
            "\n".join(
                [
                    '#include "variant.h"',
                    "",
                    "// Optional board-specific initialization hooks go here.",
                    "// Leave this file out if the board does not need custom startup behavior.",
                ]
            )
            + "\n",
            assessment.evidence_gaps,
        )
        variant_cpp_path.write_text(variant_cpp_content, encoding="utf-8")
        generated["variant_cpp"] = variant_cpp_path

    return generated


# T032 + T033


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate board-support scaffold files from an intake JSON file."
    )
    parser.add_argument("intake", help="Path to intake JSON")
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_ROOT),
        help="Directory where scaffold files will be written",
    )
    args = parser.parse_args()

    intake_path = Path(args.intake)
    request = BoardIntakeRequest.from_json(intake_path)
    context = load_hardware_context()
    context["context_path"] = "docs/hardware-support-context.md"
    assessment = assess_intake(request, context)

    if not assessment.scaffold_ready:
        print(render_assessment_markdown(assessment))
        sys.exit(1)

    generated = scaffold_board(assessment, context, Path(args.output_dir))
    print("Generated scaffold files:")
    for name, path in generated.items():
        print(f"- {name}: {path}")


if __name__ == "__main__":
    main()
