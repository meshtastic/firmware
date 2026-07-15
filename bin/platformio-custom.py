#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import json
import re
import subprocess
import sys
from datetime import datetime
from os.path import exists, join
from typing import Dict

from readprops import readProps

Import("env")
platform = env.PioPlatform()
progname = env.get("PROGNAME")
lfsbin = f"{progname.replace('firmware-', 'littlefs-')}.bin"
manifest_ran = False


def infer_architecture(board_cfg):
    try:
        mcu = board_cfg.get("build.mcu") if board_cfg else None
    except KeyError:
        mcu = None
    except Exception:
        mcu = None
    if not mcu:
        return None
    mcu_l = str(mcu).lower()
    if "esp32s3" in mcu_l:
        return "esp32-s3"
    if "esp32c6" in mcu_l:
        return "esp32-c6"
    if "esp32c3" in mcu_l:
        return "esp32-c3"
    if "esp32" in mcu_l:
        return "esp32"
    if "rp2040" in mcu_l:
        return "rp2040"
    if "rp2350" in mcu_l:
        return "rp2350"
    if "nrf52" in mcu_l or "nrf52840" in mcu_l:
        return "nrf52840"
    if "stm32" in mcu_l:
        return "stm32"
    return None


def run_size_tool(env, flag, purpose):
    """Run the toolchain size tool against the built ELF and return its output.

    Shared plumbing for compute_ram_bytes / compute_flash_bytes. Returns None
    when the ELF is missing or the tool fails; the manifest then simply omits
    the metric and downstream size reports show "n/a".
    """
    elf = env.File(env.subst("$BUILD_DIR/${PROGNAME}.elf"))
    if not elf.exists():
        return None
    size_tool = env.subst("$SIZETOOL") or "size"
    try:
        return subprocess.check_output(
            [size_tool, flag, elf.get_abspath()],
            env=env["ENV"],
            universal_newlines=True,
        )
    except Exception as exc:
        print(f"mtjson: skipping {purpose} ({size_tool} failed: {exc})")
        return None


def compute_ram_bytes(env):
    """Static RAM usage (.data + .bss) of the ELF, via the toolchain size tool.

    Deliberately excludes heap/stack placeholder sections (e.g. the nRF52 .heap
    section): on nRF52840 the heap arena is the linker gap after .bss, so static
    RAM growth shrinks the usable heap 1:1 - which is exactly why we track it.
    Returns None when the value cannot be determined; the manifest then simply
    omits ram_bytes and downstream size reports show "n/a".
    """
    output = run_size_tool(env, "-A", "ram_bytes")
    if output is None:
        return None
    ram = 0
    found = False
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[0]
        # Main-SRAM static sections: .data/.bss, platform-prefixed variants (e.g.
        # ESP32 .dram0.data/.dram0.bss), and RISC-V small-data .sdata/.sbss.
        # ESP-IDF .rtc.* sections live outside the heap-competing SRAM; .heap and
        # .tdata never match.
        if name.startswith(".rtc"):
            continue
        if (
            name in (".data", ".bss", ".sdata", ".sbss")
            or name.endswith(".data")
            or name.endswith(".bss")
        ):
            try:
                ram += int(parts[1])
                found = True
            except ValueError:
                continue
    if not found:
        print("mtjson: skipping ram_bytes (no RAM sections matched `size -A` output)")
    return ram if found else None


def compute_flash_bytes(env):
    """Flash footprint (text + data) of the ELF via the toolchain size tool.

    Approximates the flashed app image for targets whose packaged artifacts do
    not include a raw .bin (e.g. nRF52: hex/uf2/DFU zip only); consumed by
    bin/collect_sizes.py as a fallback flash measurement for the size report
    and the bin/ram_budgets.json budget gate. Returns None when the value
    cannot be determined; the manifest then simply omits flash_bytes.
    """
    output = run_size_tool(env, "-B", "flash_bytes")
    if output is None:
        return None
    for line in output.splitlines():
        # Strip thousands separators some `size` builds/locales emit (e.g. "123,456")
        # so the numeric check below doesn't reject an otherwise-valid data line.
        parts = [p.replace(",", "") for p in line.split()]
        if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit():
            return int(parts[0]) + int(parts[1])
    # Unlike a tool-invocation failure (caught, and logged, in run_size_tool), this is a
    # successful run whose output just didn't match the expected `size -B` shape - flag it
    # instead of silently omitting flash_bytes, since that's indistinguishable downstream
    # from "this target has no fallback needed" (see the CI failure this comment documents:
    # every nRF52840 board's flash_bytes silently went missing with no diagnostic at all).
    print(f"mtjson: skipping flash_bytes (unrecognized `size -B` output): {output!r}")
    return None


def manifest_gather(source, target, env):
    global manifest_ran
    if manifest_ran:
        return
    # Skip manifest generation if we cannot determine architecture (host/native builds)
    board_arch = infer_architecture(env.BoardConfig())
    if not board_arch:
        print(
            f"Skipping mtjson generation for unknown architecture (env={env.get('PIOENV')})"
        )
        manifest_ran = True
        return
    manifest_ran = True
    out = []
    board_platform = env.BoardConfig().get("platform")
    board_mcu = env.BoardConfig().get("build.mcu").lower()
    needs_ota_suffix = board_platform == "nordicnrf52"

    # Mapping of bin files to their target partition names
    # Maps the filename pattern to the partition name where it should be flashed
    partition_map = {
        f"{progname}.bin": "app0",  # primary application slot (app0 / OTA_0)
        lfsbin: "spiffs",  # filesystem image flashed to spiffs
    }

    check_paths = [
        progname,
        f"{progname}.elf",
        f"{progname}.bin",
        f"{progname}.factory.bin",
        f"{progname}.hex",
        f"{progname}.merged.hex",
        f"{progname}.uf2",
        f"{progname}.factory.uf2",
        f"{progname}.zip",
        lfsbin,
        f"mt-{board_mcu}-ota.bin",
        "bleota-c3.bin",
        "bleota-s3.bin",
        "bleota.bin",
    ]
    for p in check_paths:
        f = env.File(env.subst(f"$BUILD_DIR/{p}"))
        if f.exists():
            manifest_name = p
            if needs_ota_suffix and p == f"{progname}.zip":
                manifest_name = f"{progname}-ota.zip"
            d = {
                "name": manifest_name,
                "md5": f.get_content_hash(),  # Returns MD5 hash
                "bytes": f.get_size(),  # Returns file size in bytes
            }
            # Add part_name if this file represents a partition that should be flashed
            if p in partition_map:
                d["part_name"] = partition_map[p]
            out.append(d)
            print(d)
    manifest_write(out, env, compute_ram_bytes(env), compute_flash_bytes(env))


def manifest_write(files, env, ram_bytes=None, flash_bytes=None):
    # Defensive: also skip manifest writing if we cannot determine architecture
    def get_project_option(name):
        try:
            return env.GetProjectOption(name)
        except Exception:
            return None

    def get_project_option_any(names):
        for name in names:
            val = get_project_option(name)
            if val is not None:
                return val
        return None

    def as_bool(val):
        return str(val).strip().lower() in ("1", "true", "yes", "on")

    def as_int(val):
        try:
            return int(str(val), 10)
        except (TypeError, ValueError):
            return None

    def as_list(val):
        return [item.strip() for item in str(val).split(",") if item.strip()]

    manifest = {
        "version": verObj["long"],
        "build_epoch": build_epoch,
        "platformioTarget": env.get("PIOENV"),
        "mcu": env.get("BOARD_MCU"),
        "repo": repo_owner,
        "files": files,
        "has_mui": False,
        "has_inkhud": False,
    }
    # Static RAM footprint (.data + .bss); consumed by bin/collect_sizes.py for
    # the CI size report and the bin/ram_budgets.json budget gate.
    if ram_bytes is not None:
        manifest["ram_bytes"] = ram_bytes
    # Flash footprint (text + data); fallback flash measurement for targets
    # whose packaged artifacts include no raw .bin (e.g. nRF52).
    if flash_bytes is not None:
        manifest["flash_bytes"] = flash_bytes
    # Board's declared capacity, the same numbers PlatformIO's own post-build RAM:/Flash:
    # bars use - lets bin/size_report.py draw an equivalent bar for the terminal (--format text).
    try:
        board_cfg = env.BoardConfig()
    except Exception:
        board_cfg = None
    max_ram_bytes = (
        board_cfg.get("upload.maximum_ram_size", None) if board_cfg else None
    )
    max_flash_bytes = board_cfg.get("upload.maximum_size", None) if board_cfg else None
    if isinstance(max_ram_bytes, int) and not isinstance(max_ram_bytes, bool):
        manifest["max_ram_bytes"] = max_ram_bytes
    if isinstance(max_flash_bytes, int) and not isinstance(max_flash_bytes, bool):
        manifest["max_flash_bytes"] = max_flash_bytes
    # Get partition table (generated in esp32_pre.py) if it exists
    if env.get("custom_mtjson_part"):
        # custom_mtjson_part is a JSON string, convert it back to a dict
        pj = json.loads(env.get("custom_mtjson_part"))
        manifest["part"] = pj
    # Enable has_mui for TFT builds
    if ("HAS_TFT", 1) in env.get("CPPDEFINES", []):
        manifest["has_mui"] = True
    if "MESHTASTIC_INCLUDE_INKHUD" in env.get("CPPDEFINES", []):
        manifest["has_inkhud"] = True

    pioenv = env.get("PIOENV")
    device_meta = {}
    device_meta_fields = [
        ("hwModel", ["custom_meshtastic_hw_model"], as_int),
        ("hwModelSlug", ["custom_meshtastic_hw_model_slug"], str),
        ("architecture", ["custom_meshtastic_architecture"], str),
        ("activelySupported", ["custom_meshtastic_actively_supported"], as_bool),
        ("displayName", ["custom_meshtastic_display_name"], str),
        ("supportLevel", ["custom_meshtastic_support_level"], as_int),
        ("images", ["custom_meshtastic_images"], as_list),
        ("tags", ["custom_meshtastic_tags"], as_list),
        ("requiresDfu", ["custom_meshtastic_requires_dfu"], as_bool),
        ("partitionScheme", ["custom_meshtastic_partition_scheme"], str),
        ("url", ["custom_meshtastic_url"], str),
        ("key", ["custom_meshtastic_key"], str),
        ("variant", ["custom_meshtastic_variant"], str),
    ]

    for manifest_key, option_keys, caster in device_meta_fields:
        raw_val = get_project_option_any(option_keys)
        if raw_val is None:
            continue
        parsed = caster(raw_val) if callable(caster) else raw_val
        if parsed is not None and parsed != "":
            device_meta[manifest_key] = parsed

    # Determine architecture once; if we can't infer it, skip manifest generation
    board_arch = device_meta.get("architecture") or infer_architecture(
        env.BoardConfig()
    )
    if not board_arch:
        print(
            f"Skipping mtjson write for unknown architecture (env={env.get('PIOENV')})"
        )
        return

    device_meta["architecture"] = board_arch

    # Always set requiresDfu: true for nrf52840 targets
    if board_arch == "nrf52840":
        device_meta["requiresDfu"] = True

    device_meta.setdefault("displayName", pioenv)
    device_meta.setdefault("activelySupported", False)

    if device_meta:
        manifest.update(device_meta)

    # Write the manifest to the build directory
    with open(env.subst("$BUILD_DIR/${PROGNAME}.mt.json"), "w") as f:
        json.dump(manifest, f, indent=2)


Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print(
    f"Using meshtastic platformio-custom.py, firmware version {verObj['long']} on {env.get('PIOENV')}"
)

# get repository owner if git is installed
try:
    r_owner = (
        subprocess.check_output(["git", "config", "--get", "remote.origin.url"])
        .decode("utf-8")
        .strip()
        .split("/")
    )
    repo_owner = r_owner[-2] + "/" + r_owner[-1].replace(".git", "")
except (subprocess.CalledProcessError, FileNotFoundError, IndexError, OSError):
    repo_owner = "unknown"

jsonLoc = env["PROJECT_DIR"] + "/userPrefs.jsonc"
with open(jsonLoc) as f:
    jsonStr = re.sub("//.*", "", f.read(), flags=re.MULTILINE)
    userPrefs = json.loads(jsonStr)

pref_flags = []
# Pre-process the userPrefs
for pref in userPrefs:
    if userPrefs[pref].startswith("{"):
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref].lstrip("-").replace(".", "").isdigit():
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref] == "true" or userPrefs[pref] == "false":
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref].startswith("meshtastic_"):
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    # If the value is a string, we need to wrap it in quotes
    else:
        pref_flags.append("-D" + pref + "=" + env.StringifyMacro(userPrefs[pref]) + "")

# General options that are passed to the C and C++ compilers
# Calculate unix epoch for current day (midnight)
current_date = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
build_epoch = int(current_date.timestamp())

flags = [
    "-DAPP_VERSION=" + verObj["long"],
    "-DAPP_VERSION_SHORT=" + verObj["short"],
    "-DAPP_ENV=" + env.get("PIOENV"),
    "-DAPP_REPO=" + repo_owner,
    "-DBUILD_EPOCH=" + str(build_epoch),
] + pref_flags

print("Using flags:")
for flag in flags:
    print(flag)

projenv.Append(
    CCFLAGS=flags,
)

for lb in env.GetLibBuilders():
    if lb.name == "meshtastic-device-ui":
        lb.env.Append(CPPDEFINES=[("APP_VERSION", verObj["long"])])
        break


# Get the display resolution from macros
def get_display_resolution(build_flags):
    # Check "DISPLAY_SIZE" to determine the screen resolution
    for flag in build_flags:
        if isinstance(flag, tuple) and flag[0] == "DISPLAY_SIZE":
            screen_width, screen_height = map(int, flag[1].split("x"))
            return screen_width, screen_height
    print("No screen resolution defined in build_flags. Please define DISPLAY_SIZE.")
    exit(1)


def load_boot_logo(source, target, env):
    build_flags = env.get("CPPDEFINES", [])
    logo_w, logo_h = get_display_resolution(build_flags)
    print(f"TFT build with {logo_w}x{logo_h} resolution detected")

    # Load the boot logo from `branding/logo_<width>x<height>.png` if it exists
    source_path = join(env["PROJECT_DIR"], "branding", f"logo_{logo_w}x{logo_h}.png")
    dest_dir = join(env["PROJECT_DIR"], "data", "boot")
    dest_path = join(dest_dir, "logo.png")
    if env.File(source_path).exists():
        print(f"Loading boot logo from {source_path}")
        # Prepare the destination
        env.Execute(f"mkdir -p {dest_dir} && rm -f {dest_path}")
        # Copy the logo to the `data/boot` directory
        env.Execute(f"cp {source_path} {dest_path}")


# Load the boot logo on TFT builds
if ("HAS_TFT", 1) in env.get("CPPDEFINES", []):
    env.AddPreAction(f"$BUILD_DIR/{lfsbin}", load_boot_logo)

board_arch = infer_architecture(env.BoardConfig())
should_skip_manifest = board_arch is None

# Most platforms can generate the manifest as part of the default 'buildprog' target.
# Typically this passes success/failure properly.
mtjson_deps = ["buildprog"]
if platform.name == "espressif32":
    # On ESP32, we need to explicitly depend upon the binary to prevent fake-success upon failure.
    mtjson_deps = ["$BUILD_DIR/${PROGNAME}.bin"]
    # Build littlefs image as part of mtjson target
    # Equivalent to `pio run -t buildfs`
    target_lfs = env.DataToBin(
        join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}"), "$PROJECT_DATA_DIR"
    )
    mtjson_deps.append(target_lfs)

if should_skip_manifest:

    def skip_manifest(source, target, env):
        print(f"mtjson: skipped for native environment: {env.get('PIOENV')}")

    env.AddCustomTarget(
        name="mtjson",
        # For host/native envs, avoid depending on 'buildprog' (some targets don't define it)
        dependencies=[],
        actions=[skip_manifest],
        title="Meshtastic Manifest (skipped)",
        description="mtjson generation is skipped for native environments",
        always_build=True,
    )
else:
    env.AddCustomTarget(
        name="mtjson",
        dependencies=mtjson_deps,
        actions=[manifest_gather],
        title="Meshtastic Manifest",
        description="Generating Meshtastic manifest JSON + Checksums",
        always_build=True,
    )

    # Run manifest generation as part of the default build pipeline for non-native builds.
    env.Default("mtjson")

    # Size comparison against develop/master needs network access (gh) to fetch their CI
    # baselines, so unlike mtjson it can't run as part of every default build - it's exposed
    # as an on-demand custom target instead. See bin/check-size.sh for the actual logic; this
    # just invokes it for the current PIOENV once its manifest (mtjson, above) exists.
    def make_sizecheck_action(extra_args):
        # SCons calls action functions as action(target=..., source=..., env=...); the
        # actual env we need (PROJECT_DIR, PIOENV) is the outer construction env, not the
        # source/target args, so ignore whatever SCons passes in.
        def action(**kwargs):
            script = join(env["PROJECT_DIR"], "bin", "check-size.sh")
            cmd = [script, env.get("PIOENV")] + extra_args
            budgets = join(env["PROJECT_DIR"], "bin", "ram_budgets.json")
            if exists(budgets):
                cmd += ["--", "--budgets", budgets]
            return subprocess.call(cmd, cwd=env["PROJECT_DIR"])

        return action

    env.AddCustomTarget(
        name="sizecheck",
        dependencies=["mtjson"],
        actions=[make_sizecheck_action(["--from-develop", "--from-master"])],
        title="Meshtastic Size vs develop/master",
        description="Compare this build's flash/RAM size against the newest develop and master CI baselines (needs 'gh')",
        always_build=True,
    )

    env.AddCustomTarget(
        name="sizecheck-mergebase",
        dependencies=["mtjson"],
        actions=[make_sizecheck_action(["--from-develop", "--merge-base"])],
        title="Meshtastic Size vs merge-base",
        description="Compare against develop at the commit this branch last synced with - isolates this branch's own size drift (needs 'gh')",
        always_build=True,
    )

    env.AddCustomTarget(
        name="sizecheck-local",
        dependencies=["mtjson"],
        actions=[
            make_sizecheck_action(["--from-develop", "--merge-base", "--build-local"])
        ],
        title="Meshtastic Size vs local develop build",
        description="Build develop at this branch's merge-base in a throwaway worktree and compare - no 'gh'/CI artifact needed, but runs a full extra build",
        always_build=True,
    )
