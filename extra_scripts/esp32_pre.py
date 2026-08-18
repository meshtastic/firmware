#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import json
import sys
from os.path import isfile

Import("env")


# From https://github.com/platformio/platform-espressif32/blob/develop/builder/main.py
def _parse_size(value):
    if isinstance(value, int):
        return value
    elif value.isdigit():
        return int(value)
    elif value.startswith("0x"):
        return int(value, 16)
    elif value[-1].upper() in ("K", "M"):
        base = 1024 if value[-1].upper() == "K" else 1024 * 1024
        return int(value[:-1]) * base
    return value


def _parse_partitions(env):
    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    if not isfile(partitions_csv):
        sys.stderr.write(
            "Could not find the file %s with partitions " "table.\n" % partitions_csv
        )
        env.Exit(1)
        return

    result = []
    # The first offset is 0x9000 because partition table is flashed to 0x8000 and
    # occupies an entire flash sector, which size is 0x1000
    next_offset = 0x9000
    with open(partitions_csv) as fp:
        for line in fp.readlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [t.strip() for t in line.split(",")]
            if len(tokens) < 5:
                continue

            bound = 0x10000 if tokens[1] in ("0", "app") else 4
            calculated_offset = (next_offset + bound - 1) & ~(bound - 1)
            partition = {
                "name": tokens[0],
                "type": tokens[1],
                "subtype": tokens[2],
                "offset": tokens[3] or calculated_offset,
                "size": tokens[4],
                "flags": tokens[5] if len(tokens) > 5 else None,
            }
            result.append(partition)
            next_offset = _parse_size(partition["offset"]) + _parse_size(
                partition["size"]
            )

    return result


def mtjson_esp32_part(target, source, env):
    part = _parse_partitions(env)
    pj = json.dumps(part)
    # print(f"JSON_PARTITIONS: {pj}")
    # Dump json string to 'custom_mtjson_part' variable to use later when writing the manifest
    env.Replace(custom_mtjson_part=pj)


env.AddPreAction("mtjson", mtjson_esp32_part)


# ---------------------------------------------------------------------------
# HybridCompile cache-poisoning workaround (pioarduino platform-espressif32)
#
# The platform decides whether the precompiled Arduino IDF libs can be reused
# by hashing only the custom_sdkconfig text + mcu
# (builder/frameworks/arduino.py::matching_custom_sdkconfig). The board JSON's
# PSRAM configuration (BOARD_HAS_PSRAM / memory_type) is not part of that
# hash, and nearly all our S3 variants share esp32s3_base's identical
# custom_sdkconfig. Building a PSRAM env (e.g. heltec-v4) followed by a
# no-PSRAM env (e.g. heltec-v3) therefore skips the framework-libs recompile
# and links CONFIG_SPIRAM=y libs into the no-PSRAM firmware, which boot-loops
# with "quad_psram: PSRAM chip is not connected".
#
# Workaround: before the platform's arduino.py SConscript reads the option,
# append a comment line derived from the board's PSRAM/memory configuration to
# this env's custom_sdkconfig. The comment is inert in kconfig but changes the
# md5, forcing the IDF-libs recompile whenever the PSRAM class changes. The
# derivation mirrors espidf.py::generate_board_specific_config().
#
# The marker must stay lowercase: arduino.py greps custom_sdkconfig for the
# substrings "PSRAM" and "CONFIG_SPIRAM=y" (has_psram_config) and would
# otherwise treat every board as having PSRAM.
# ---------------------------------------------------------------------------

SDKCONFIG_CACHE_KEY = "meshtastic_hybridcompile_cache_key"


def spiram_cache_class(env):
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")

    # memory_type: platformio.ini override > build.arduino.memory_type > build.memory_type
    memory_type = None
    try:
        memory_type = env.GetProjectOption("board_build.memory_type", None)
    except Exception:
        pass
    if not memory_type:
        build_section = board.get("build", {})
        memory_type = build_section.get("arduino", {}).get(
            "memory_type"
        ) or build_section.get("memory_type")

    extra_flags = board.get("build.extra_flags", "")
    if isinstance(extra_flags, str):
        has_psram = "PSRAM" in extra_flags
    else:
        has_psram = any("PSRAM" in str(flag) for flag in extra_flags)
    if not has_psram:
        if memory_type and (
            "opi" in memory_type.lower() or "psram" in memory_type.lower()
        ):
            has_psram = True
        elif "psram_type" in board.get("build", {}):
            has_psram = True

    if has_psram:
        psram_type = None
        try:
            psram_type = env.GetProjectOption("board_build.psram_type", None)
        except Exception:
            pass
        if not psram_type and memory_type and len(memory_type.split("_")) == 2:
            psram_type = memory_type.split("_")[1]
        if not psram_type:
            psram_type = board.get("build.psram_type", "") or (
                "hex" if mcu == "esp32p4" else "qio"
            )
        psram_type = psram_type.lower()
        if psram_type == "opi" and mcu == "esp32s3":
            spiram = "oct"
        elif psram_type == "hex":
            spiram = "hex"
        else:
            spiram = "quad"
    else:
        spiram = "none"

    return "memory_type=%s spiram=%s" % ((memory_type or "default").lower(), spiram)


def tag_sdkconfig_cache_key(env):
    config = env.GetProjectConfig()
    section = "env:" + env["PIOENV"]
    if not config.has_option(section, "custom_sdkconfig"):
        return
    current = env.GetProjectOption("custom_sdkconfig")
    if SDKCONFIG_CACHE_KEY in current:
        return
    marker = "# %s: %s" % (SDKCONFIG_CACHE_KEY, spiram_cache_class(env))
    config.set(section, "custom_sdkconfig", current.rstrip("\n") + "\n" + marker)


tag_sdkconfig_cache_key(env)
