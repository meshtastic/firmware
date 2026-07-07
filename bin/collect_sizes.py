#!/usr/bin/env python3

"""Collect firmware binary sizes from manifest (.mt.json) files into a single report.

Output schema (consumed by bin/size_report.py):
    {"<env>": {"flash_bytes": <int>, "ram_bytes": <int>}}

flash_bytes is the size of the main firmware image (.bin); for targets whose
packaged artifacts include no raw .bin (e.g. nRF52) it falls back to the
flash_bytes value (ELF text + data) emitted into the manifest by
bin/platformio-custom.py. ram_bytes is the static RAM footprint (.data + .bss)
emitted into the manifest by bin/platformio-custom.py; either metric is
omitted for manifests that predate it, and size_report.py renders those as
"n/a".
"""

import json
import os
import sys


def collect_sizes(manifest_dir):
    """Scan manifest_dir for .mt.json files and return {board: sizes_dict}."""
    sizes = {}
    for fname in sorted(os.listdir(manifest_dir)):
        if not fname.endswith(".mt.json"):
            continue
        path = os.path.join(manifest_dir, fname)
        with open(path) as f:
            data = json.load(f)
        board = data.get("platformioTarget", fname.replace(".mt.json", ""))
        # Find the main firmware .bin size (largest .bin, excluding OTA/littlefs/bleota)
        bin_size = None
        for entry in data.get("files", []):
            name = entry.get("name", "")
            if name.startswith("firmware-") and name.endswith(".bin"):
                bin_size = entry["bytes"]
                break
        # Fallback: any .bin that isn't ota/littlefs/bleota
        if bin_size is None:
            for entry in data.get("files", []):
                name = entry.get("name", "")
                if name.endswith(".bin") and not any(
                    x in name for x in ["littlefs", "bleota", "ota"]
                ):
                    bin_size = entry["bytes"]
                    break
        # Fallback: flash footprint emitted into the manifest (ELF text + data),
        # for targets that package no raw .bin (e.g. nRF52 hex/uf2/DFU zip)
        if bin_size is None:
            flash_bytes = data.get("flash_bytes")
            if isinstance(flash_bytes, int) and not isinstance(flash_bytes, bool):
                bin_size = flash_bytes
        entry = {}
        if bin_size is not None:
            entry["flash_bytes"] = bin_size
        ram_bytes = data.get("ram_bytes")
        if isinstance(ram_bytes, int) and not isinstance(ram_bytes, bool):
            entry["ram_bytes"] = ram_bytes
        if entry:
            sizes[board] = entry
        else:
            print(
                f"WARNING: no size-bearing firmware artifact found for '{board}' ({fname})",
                file=sys.stderr,
            )
    return sizes


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <manifest_dir> <output.json>", file=sys.stderr)
        sys.exit(1)

    manifest_dir = sys.argv[1]
    output_path = sys.argv[2]

    sizes = collect_sizes(manifest_dir)
    with open(output_path, "w") as f:
        json.dump(sizes, f, indent=2, sort_keys=True)

    print(f"Collected sizes for {len(sizes)} targets -> {output_path}")
