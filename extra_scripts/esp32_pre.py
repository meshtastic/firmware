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
