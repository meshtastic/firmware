#!/usr/bin/env python3

"""Collect firmware binary sizes from manifest (.mt.json) files into a single report."""

import json
import os
import sys


def collect_sizes(manifest_dir):
    """Scan manifest_dir for .mt.json files and return {board: size_bytes} dict."""
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
        if bin_size is not None:
            sizes[board] = bin_size
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
