#!/usr/bin/env python3

"""Cross-check ESP32 flash geometry across the three places it is declared.

For every ESP32 PlatformIO environment this reconciles:

  * ``upload.maximum_size`` from the resolved board manifest (the board's flash size),
  * the end of the last partition in the selected partition CSV,
  * ``custom_meshtastic_partition_scheme``, the flash-size tier published in the
    ``.mt.json`` manifest (bin/platformio-custom.py) and consumed by the flasher.

Nothing at build time reconciles them: the espressif32 builder overwrites
``upload.maximum_size`` with the app-slot size from the partition table
(``_update_max_upload_size``), so a variant pointing at a 16MB table on an 8MB
board still links and only fails when it is flashed. This script is that
reconciliation, run as a static config check before anything is built.

Board resolution mirrors the builder: the ``board_build.partitions`` project
option wins, otherwise ``build.partitions`` from the board manifest. Board
manifests are looked up in the project ``boards/`` directory first, then in the
installed platform packages under the PlatformIO core directory.

Exit status is 0 when every environment is consistent, 1 otherwise.
"""

import argparse
import glob
import io
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

from platformio.project.config import ProjectConfig

# Board manifests are fetched as data, never via 'pio pkg install', which
# exec_module()s the downloaded platform.py. Owner allowlist, not just host.
ALLOWED_MANIFEST_HOSTS = ("github.com", "codeload.github.com", "raw.githubusercontent.com")
ALLOWED_MANIFEST_OWNERS = ("meshtastic", "pioarduino", "platformio")
MANIFEST_FETCH_TIMEOUT = 120
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024

# Flash-size tiers a board can ship; used to turn a byte count into the tier that
# 'custom_meshtastic_partition_scheme' names (e.g. 8388608 -> "8MB").
FLASH_TIERS = (
    1 * 1024 * 1024,
    2 * 1024 * 1024,
    4 * 1024 * 1024,
    8 * 1024 * 1024,
    16 * 1024 * 1024,
    32 * 1024 * 1024,
    64 * 1024 * 1024,
)

# Partition table sits at 0x8000 and fills a 0x1000 sector, so an omitted first
# offset starts right after it. Same constant the builder uses.
FIRST_PARTITION_OFFSET = 0x9000


def parse_size(value):
    """Parse a partition CSV size/offset: decimal, 0x hex, or a K/M suffix."""
    if isinstance(value, int):
        return value
    value = str(value).strip()
    if value.isdigit():
        return int(value)
    if value.lower().startswith("0x"):
        return int(value, 16)
    if value and value[-1].upper() in ("K", "M"):
        base = 1024 if value[-1].upper() == "K" else 1024 * 1024
        return int(value[:-1]) * base
    raise ValueError(f"unparseable size '{value}'")


def parse_partitions(csv_path):
    """Parse a partition CSV into [(name, offset, size)], resolving omitted offsets.

    Mirrors _parse_partitions() in the espressif32 builder: app partitions align
    to 0x10000, everything else to 4 bytes.
    """
    rows = []
    next_offset = FIRST_PARTITION_OFFSET
    with open(csv_path, encoding="utf-8") as fp:
        for lineno, line in enumerate(fp, start=1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [t.strip() for t in line.split(",")]
            if len(tokens) < 5:
                continue
            bound = 0x10000 if tokens[1] in ("0", "app") else 4
            calculated_offset = (next_offset + bound - 1) & ~(bound - 1)
            try:
                offset = parse_size(tokens[3]) if tokens[3] else calculated_offset
                size = parse_size(tokens[4])
            except ValueError as exc:
                raise ValueError(f"{csv_path}:{lineno}: {exc}") from exc
            rows.append((tokens[0], offset, size))
            next_offset = offset + size
    if not rows:
        raise ValueError(f"{csv_path}: no partition entries")
    return rows


def flash_tier(size_bytes):
    """Smallest standard flash size that can hold 'size_bytes', or None."""
    for tier in FLASH_TIERS:
        if size_bytes <= tier:
            return tier
    return None


def format_tier(size_bytes):
    return f"{size_bytes // (1024 * 1024)}MB"


def parse_scheme(scheme):
    """Parse a 'custom_meshtastic_partition_scheme' value ('8MB') into bytes."""
    match = re.fullmatch(r"\s*(\d+)\s*([KM])B?\s*", scheme, re.IGNORECASE)
    if not match:
        return None
    base = 1024 if match.group(2).upper() == "K" else 1024 * 1024
    return int(match.group(1)) * base


def find_board_manifest(board_id, project_dir, core_dir, fetched_dir=None):
    """Locate a board manifest: project boards/, then fetched, then installed platforms."""
    local = os.path.join(project_dir, "boards", f"{board_id}.json")
    if os.path.isfile(local):
        return local
    if fetched_dir:
        fetched = os.path.join(fetched_dir, f"{board_id}.json")
        if os.path.isfile(fetched):
            return fetched
    # Only espressif32*, so a same-named board in another installed platform can never
    # answer; its versions unpack side by side, and sorted() keeps the pick deterministic.
    pattern = os.path.join(core_dir, "platforms", "espressif32*", "boards", f"{board_id}.json")
    installed = sorted(glob.glob(pattern))
    return installed[0] if installed else None


def check_manifest_url(url):
    """Reject a platform archive URL that is not an allowlisted https location.

    The URL is read out of the checkout, so on a fork PR it is attacker-controlled.
    Restricting it to repositories owned by the projects this firmware actually
    tracks keeps a PR from redirecting the fetch at content it authored.
    """
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https":
        return f"'{url}' is not https"
    if parsed.hostname not in ALLOWED_MANIFEST_HOSTS:
        return f"host '{parsed.hostname}' is not one of {', '.join(ALLOWED_MANIFEST_HOSTS)}"
    owner = parsed.path.lstrip("/").split("/")[0]
    if owner not in ALLOWED_MANIFEST_OWNERS:
        return f"owner '{owner}' is not one of {', '.join(ALLOWED_MANIFEST_OWNERS)}"
    return None


class AllowlistedRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Re-check the allowlist on each redirect target; the default opener follows
    redirects blind, and this fetch does redirect (github.com to codeload.github.com)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        problem = check_manifest_url(newurl)
        if problem:
            raise urllib.error.HTTPError(newurl, code, f"refusing redirect: {problem}", headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def fetch_board_manifests(url, dest_dir):
    """Download a platform archive and extract only its boards/*.json, as data.

    Nothing from the archive is imported or executed, and every file is written to
    dest_dir under its basename alone, so a crafted member name cannot escape it.
    """
    problem = check_manifest_url(url)
    if problem:
        raise ValueError(f"refusing to fetch board manifests: {problem}")

    # check_manifest_url() gates the initial URL and every redirect target, so file://
    # and untrusted hosts cannot reach the fetch.
    request = urllib.request.Request(url, headers={"User-Agent": "meshtastic-firmware-ci"})  # noqa: S310
    opener = urllib.request.build_opener(AllowlistedRedirectHandler)
    # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
    with opener.open(request, timeout=MANIFEST_FETCH_TIMEOUT) as response:
        payload = response.read(MAX_ARCHIVE_BYTES + 1)
    if len(payload) > MAX_ARCHIVE_BYTES:
        raise ValueError(f"archive at {url} exceeds {MAX_ARCHIVE_BYTES} bytes")

    os.makedirs(dest_dir, exist_ok=True)
    written = 0
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for entry in archive.infolist():
            parts = entry.filename.split("/")
            if entry.is_dir() or len(parts) < 2 or parts[-2] != "boards":
                continue
            if not parts[-1].endswith(".json") or entry.file_size > MAX_MANIFEST_BYTES:
                continue
            # basename only: never join a path taken from the archive.
            target = os.path.join(dest_dir, os.path.basename(parts[-1]))
            with archive.open(entry) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst, length=64 * 1024)
            written += 1
    if not written:
        raise ValueError(f"archive at {url} contained no boards/*.json")
    return written


def board_option(manifest, dotted_key):
    """Read a dotted key ('build.partitions') out of a board manifest."""
    node = manifest
    for part in dotted_key.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


def esp32_envs(cfg, only_env=None):
    """Yield (env_name, platform_dir) for ESP32 environments.

    Platform comes from the '-I variants/<dir>' build flag, the same signal
    bin/generate_ci_matrix.py uses to bucket environments.
    """
    for env_name in cfg.envs():
        if only_env and env_name != only_env:
            continue
        platform = None
        for flag in cfg.get(f"env:{env_name}", "build_flags", default=[]):
            match = re.search(r"-I\s?variants/([^/\s]+)", flag)
            if match:
                platform = match.group(1)
                break
        if platform and platform.startswith("esp32"):
            yield env_name, platform


def check_env(cfg, env_name, project_dir, core_dir, fetched_dir=None):
    """Return a list of error strings for one environment (empty when consistent)."""
    errors = []
    section = f"env:{env_name}"

    board_id = cfg.get(section, "board", default=None)
    if not board_id:
        return [f"{env_name}: no 'board' set, cannot resolve flash size"]

    manifest_path = find_board_manifest(board_id, project_dir, core_dir, fetched_dir)
    if not manifest_path:
        return [
            (
                f"{env_name}: board '{board_id}' not found in {project_dir}/boards or any "
                f"installed platform under {core_dir}/platforms (install the platform, or pass "
                "--fetch-board-manifests as CI does)"
            )
        ]
    with open(manifest_path, encoding="utf-8") as fp:
        manifest = json.load(fp)

    # PlatformIO merges 'board_*' project options into the board manifest, so an
    # env-level board_upload.maximum_size is part of the resolved board config.
    override = cfg.get(section, "board_upload.maximum_size", default=None)
    if override is not None:
        try:
            maximum_size = parse_size(override)
        except ValueError:
            return [f"{env_name}: board_upload.maximum_size = '{override}' is not a size"]
        max_size_source = "board_upload.maximum_size"
    else:
        maximum_size = board_option(manifest, "upload.maximum_size")
        max_size_source = f"board '{board_id}'"
        if not isinstance(maximum_size, int):
            return [f"{env_name}: board '{board_id}' ({manifest_path}) has no integer upload.maximum_size"]

    # Same precedence as the builder: the project option is merged into the board
    # manifest, so board_build.partitions wins over the manifest's build.partitions.
    csv_name = cfg.get(section, "board_build.partitions", default=None) or board_option(
        manifest, "build.partitions"
    )
    if not csv_name:
        # build.arduino.partitions is *not* read by the pioarduino builder; a board
        # that only sets it silently falls back to the framework's single-app table.
        arduino_only = board_option(manifest, "build.arduino.partitions")
        hint = (
            f" (board '{board_id}' sets build.arduino.partitions = {arduino_only}, which the "
            "builder ignores; set board_build.partitions in the env instead)"
            if arduino_only
            else ""
        )
        return [f"{env_name}: no partition table selected{hint}"]

    csv_path = csv_name if os.path.isabs(csv_name) else os.path.join(project_dir, csv_name)
    if not os.path.isfile(csv_path):
        # The builder would fall back to an IDF-shipped table, not checked out here.
        # Keep every variant's table in the repo so its geometry is reviewable.
        return [f"{env_name}: partition table '{csv_name}' not found at {csv_path}"]

    try:
        rows = parse_partitions(csv_path)
    except ValueError as exc:
        return [f"{env_name}: {exc}"]

    last_name, last_offset, last_size = rows[-1]
    table_end = max(offset + size for _, offset, size in rows)

    # 1. The table must fit inside the flash the resolved board declares.
    if table_end > maximum_size:
        # board_upload.flash_size does not carry over to upload.maximum_size, so an
        # env that only bumps flash_size leaves a board config contradicting itself.
        flash_size = cfg.get(section, "board_upload.flash_size", default=None)
        hint = (
            f"; the env sets board_upload.flash_size = {flash_size} but not "
            "board_upload.maximum_size, which does not carry over"
            if flash_size and override is None
            else ""
        )
        errors.append(
            f"{env_name}: partition table {csv_name} ends at {table_end:#x} "
            f"(last entry '{last_name}' at {last_offset:#x} + {last_size:#x}) but "
            f"{max_size_source} declares upload.maximum_size = {maximum_size:#x} "
            f"({maximum_size} bytes) - the table overflows the board's flash{hint}"
        )

    # 2. The published partition scheme must name the board's flash tier.
    scheme = cfg.get(section, "custom_meshtastic_partition_scheme", default=None)
    if scheme:
        scheme_bytes = parse_scheme(scheme)
        tier = flash_tier(maximum_size)
        if scheme_bytes is None:
            errors.append(
                f"{env_name}: custom_meshtastic_partition_scheme = '{scheme}' is not a "
                "flash size like '8MB'"
            )
        elif tier is None:
            errors.append(
                f"{env_name}: {max_size_source} upload.maximum_size = {maximum_size} "
                "does not fall in any known flash-size tier"
            )
        elif scheme_bytes != tier:
            errors.append(
                f"{env_name}: custom_meshtastic_partition_scheme = '{scheme}' but "
                f"{max_size_source} declares upload.maximum_size = {maximum_size:#x} "
                f"({maximum_size} bytes, {format_tier(tier)} flash) - expected "
                f"'{format_tier(tier)}'"
            )

    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--env", help="check a single PlatformIO environment instead of every ESP32 one")
    parser.add_argument("-v", "--verbose", action="store_true", help="print each environment that checks out")
    parser.add_argument(
        "--fetch-board-manifests",
        metavar="DIR",
        help="download the boards/*.json of the pinned ESP32 platform into DIR and use them "
        "for lookup, for a checkout that has no platform installed (this is what CI does). "
        "Board JSON is read as data only: nothing from the archive is imported or executed",
    )
    args = parser.parse_args()

    cfg = ProjectConfig.get_instance()
    project_dir = os.path.dirname(os.path.abspath(cfg.path))
    core_dir = cfg.get("platformio", "core_dir")

    envs = list(esp32_envs(cfg, args.env))

    fetched_dir = args.fetch_board_manifests
    if fetched_dir:
        specs = []
        for env_name, _ in envs:
            spec = (cfg.get(f"env:{env_name}", "platform", default="") or "").strip()
            if spec and spec not in specs:
                specs.append(spec)
        for spec in specs:
            if not spec.startswith("https://"):
                print(f"Skipping non-URL platform spec '{spec}'; relying on installed platforms")
                continue
            try:
                count = fetch_board_manifests(spec, fetched_dir)
            except (ValueError, OSError, zipfile.BadZipFile) as exc:
                print(f"::error title=Partition geometry::{exc}", file=sys.stderr)
                return 1
            print(f"Fetched {count} board manifests from {spec}")

    if args.env and not envs:
        # Not an ESP32 env (or not an env at all): nothing to reconcile.
        print(f"{args.env}: not an ESP32 environment, skipping partition geometry check")
        return 0
    if not envs:
        print("Error: no ESP32 environments found", file=sys.stderr)
        return 1

    errors = []
    for env_name, platform in envs:
        env_errors = check_env(cfg, env_name, project_dir, core_dir, fetched_dir)
        errors.extend(env_errors)
        if args.verbose and not env_errors:
            print(f"  ok  {env_name} ({platform})")

    if errors:
        for error in errors:
            # GitHub Actions renders '::error::' lines as annotations; harmless locally.
            print(f"::error title=Partition geometry::{error}", file=sys.stderr)
        print(
            f"\n{len(errors)} flash geometry problem(s) across {len(envs)} ESP32 environment(s).",
            file=sys.stderr,
        )
        return 1

    print(f"Flash geometry consistent across {len(envs)} ESP32 environment(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
