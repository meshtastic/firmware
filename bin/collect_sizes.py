#!/usr/bin/env python3

"""Collect firmware binary sizes from manifest (.mt.json) files into a single report.

Output schema (consumed by bin/size_report.py):
    {"<env>": {"flash_bytes": <int>, "ram_bytes": <int>}}

flash_bytes is the size of the main firmware image (.bin), falling back to the
OTA package size for targets that only publish firmware-*-ota.zip in their
manifest. ram_bytes is the static RAM footprint (.data + .bss) emitted into the
manifest by bin/platformio-custom.py; it is omitted for manifests that predate
it, and size_report.py renders those as "n/a".
"""

import json
import os
import sys


def collect_sizes(manifest_dir):
    """Scan manifest_dir for .mt.json files and return {board: sizes_dict}."""
    sizes = {}
    size_paths = {}
    manifest_paths = []
    for root, _, files in os.walk(manifest_dir):
        for fname in files:
            if fname.endswith(".mt.json"):
                manifest_paths.append(os.path.join(root, fname))

    for path in sorted(manifest_paths):
        fname = os.path.basename(path)
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
        # nRF52 release manifests publish an OTA package instead of a raw .bin
        # entry. It contains the app binary and is the only flash-sized value
        # available to the manifest-only size gate.
        if bin_size is None:
            for entry in data.get("files", []):
                name = entry.get("name", "")
                if name.startswith("firmware-") and name.endswith("-ota.zip"):
                    bin_size = entry["bytes"]
                    break
        if bin_size is not None:
            entry = {"flash_bytes": bin_size}
            ram_bytes = data.get("ram_bytes")
            if isinstance(ram_bytes, int) and not isinstance(ram_bytes, bool):
                entry["ram_bytes"] = ram_bytes
            if board in sizes:
                print(
                    f"warning: duplicate manifest for board '{board}', "
                    f"overwriting {size_paths[board]} with {path}",
                    file=sys.stderr,
                )
            sizes[board] = entry
            size_paths[board] = path
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
