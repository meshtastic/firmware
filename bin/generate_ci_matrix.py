#!/usr/bin/env python3

"""Generate the CI matrix.

Enumerates every PlatformIO environment and prints a JSON list of
``{"board", "platform"}`` entries for the requested selector ("all", "check", or a
specific arch), honoring the per-env ``board_level`` tiers (``pr`` / ``extra`` /
release-only).

On pull requests the workflow additionally passes ``--changed-files``: the matrix is
then narrowed to just the environments a PR's changed files can affect (see
``select_changed``). Any change to shared source, the build system, protobufs, or an
unrecognized path deliberately falls back to the full set, so narrowing can never
cause an under-build.

The source-tree -> arch relationships are *derived from PlatformIO*, not hard-coded:
``build_src_filter`` (which each env re-includes as ``+<platform/X>``) yields the
``platform/<subdir> -> variant-top-dirs`` map, and the ``extra_configs`` globs come
straight from ``[platformio]``. This keeps the mapping self-maintaining when a board
or arch is added. ``select_changed`` and the pure helpers stay import-safe without
PlatformIO (the PlatformIO import is deferred into ``load_all_envs``) so they can be
unit-tested directly.
"""

import argparse
import glob
import json
import os
import re
import sys
from collections import defaultdict

# variants/<top-dir> whose envs are never in the firmware build matrix: portduino
# (native) envs need Emscripten / macOS / Windows toolchains and are already exercised
# on every PR by the dedicated test-native, docker, and build-wasm jobs.
COVERED_ELSEWHERE = {"native"}

# board_level values an env may carry and still be emittable. An ALLOWLIST (not a
# denylist) so an unknown/typo'd level fails CLOSED (excluded) rather than silently
# building. None = release board, "pr"/"extra" = the two explicit tiers.
EMITTABLE_LEVELS = {None, "pr", "extra"}

# Fallback platformio.ini globs that hold [env:...] / [*_base] definitions. Production
# overrides the env-definition scan with the real [platformio] extra_configs (see
# load_all_envs / Win B) so nothing rots when that list changes; this constant only
# backs the PlatformIO-free unit tests and the arch-base scan (arch bases always live
# at variants/<top>/<name>.ini, which is covered here).
DEFAULT_BOARD_INI_GLOBS = (
    "variants/*/*.ini",
    "variants/*/*/platformio.ini",
    "variants/*/diy/*/platformio.ini",
)

# RAM/flash budgets file (repo-relative). Budgeted envs must always be in the matrix
# so the fail-closed size-budget-gate has data to check against.
RAM_BUDGETS = "bin/ram_budgets.json"

# -I variants/<dir> include flag. Anchored on start-or-space so the nrf52 flag
# "-include variants/.../lfs_util.h" never matches; also handles glued "-Ivariants/…".
INCLUDE_DIR_RE = re.compile(r"(?:^|\s)-I\s*(variants/[^\s\\]+)")
# A build_src_filter whole-subdir re-include: +<platform/X> or +<platform/X/>. A
# sub-path include like +<platform/rp2xx0/pico_sleep> is deliberately NOT matched.
PLATFORM_INCL_RE = re.compile(r"\+<\s*platform/([^/>]+)/?>")
# An arch base .ini lives directly under a platform dir: variants/<plat>/<name>.ini.
ARCH_INI_RE = re.compile(r"^variants/([^/]+)/[^/]+\.ini$")
# A per-arch platform source tree: src/platform/<arch>/...
PLATFORM_SRC_RE = re.compile(r"^src/platform/([^/]+)/")
ENV_HEADER_RE = re.compile(r"^\s*\[env:([^\]]+)\]")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Generate the CI matrix")
    parser.add_argument("platform", help="Platform to build for")
    parser.add_argument(
        "--level",
        choices=["extra", "pr"],
        nargs="*",
        default=[],
        help="Board level to build for (omit for full release boards)",
    )
    parser.add_argument(
        "--changed-files",
        metavar="PATH",
        help=(
            "Newline-delimited list of a PR's changed files. When given and non-empty, "
            "restrict the matrix to the envs those files can affect; any shared or "
            "unrecognized path falls back to the full set for the given --level. An "
            "absent or empty file is a no-op (full set), so the workflow can pass this "
            "unconditionally."
        ),
    )
    return parser.parse_args(argv)


def repo_path(rel):
    """Resolve a repo-relative path from this script's location (bin/..)."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(root, rel)


def _ini_glob_list(raw):
    """Normalize an extra_configs value (list or newline string) to .ini globs."""
    if isinstance(raw, str):
        raw = raw.splitlines()
    return [g.strip() for g in (raw or []) if g and g.strip().endswith(".ini")]


def env_definition_dirs(root=".", globs=DEFAULT_BOARD_INI_GLOBS):
    """Map env name -> repo-relative dir of the platformio.ini that declares it.

    This is reverse-map "B": it catches edits to a *derived* board's own
    platformio.ini even when that board's -I points at a shared variant dir (e.g. the
    seeed_xiao_nrf52840_e22 boards live in their own dir but -I the shared kit dir).
    """
    defdir = {}
    for pattern in globs:
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            board_dir = os.path.dirname(rel)
            try:
                with open(path) as f:
                    for line in f:
                        m = ENV_HEADER_RE.match(line)
                        if m:
                            defdir[m.group(1).strip()] = board_dir
            except OSError:
                continue
    return defdir


def base_ini_platform_incl(root=".", globs=DEFAULT_BOARD_INI_GLOBS):
    """Map repo-relative .ini path -> the set of platform subdirs it re-includes.

    Only arch-base .inis that carry at least one ``+<platform/X>`` re-include appear
    (e.g. esp32-common.ini -> {"esp32"}). These are the *family-wide* bases; a chip
    base that merely inherits the include (esp32.ini) has no direct re-include and is
    absent, so a change to it scopes to its own top-dir instead of the whole family.
    A base that re-includes multiple platform trees maps to all of them, so Tier 2b
    fans out to a safe superset rather than silently dropping the extra trees.
    """
    incl = {}
    for pattern in globs:
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            try:
                with open(path) as f:
                    hits = set(PLATFORM_INCL_RE.findall(f.read()))
            except OSError:
                continue
            if hits:
                incl[rel] = hits
    return incl


def board_ini_globs(cfg=None):
    """The .ini globs from [platformio] extra_configs (Win B), or the defaults.

    Derived once and shared by every .ini scan (env_definition_dirs and
    base_ini_platform_incl) so they never diverge: a new extra_configs glob is picked
    up by both, not just one. Pass an existing cfg to avoid a redundant get_instance;
    the PlatformIO import stays deferred so the module is import-safe for unit tests.
    """
    if cfg is None:
        from platformio.project.config import ProjectConfig

        cfg = ProjectConfig.get_instance()
    globs = _ini_glob_list(cfg.get("platformio", "extra_configs", default=[]))
    return globs or DEFAULT_BOARD_INI_GLOBS


def load_all_envs():
    """Load every PlatformIO env with the metadata selection/filtering need.

    Each entry: {"ci": {"board", "platform"}, "board_level", "board_check",
    "include_dirs": [repo-relative variants dirs from -I], "def_dir": <repo-rel dir>,
    "src_platforms": [platform/<subdir> names this env compiles]}. The PlatformIO
    import is deferred here so the rest of this module stays import-safe for tests.
    """
    from platformio.project.config import ProjectConfig

    cfg = ProjectConfig.get_instance()
    # Win B: derive the env-definition scan globs from the real extra_configs so a new
    # glob (or the InkHUD config) is never silently missed.
    defdir = env_definition_dirs(globs=board_ini_globs(cfg))
    all_envs = []
    for pio_env in cfg.envs():
        build_flags = cfg.get(f"env:{pio_env}", "build_flags")
        include_dirs = []
        for flag in build_flags:
            for m in INCLUDE_DIR_RE.finditer(flag):
                include_dirs.append(m.group(1).rstrip("/"))
        # platform = first path segment after "variants/" (unchanged semantics).
        env_platform = include_dirs[0].split("/")[1] if include_dirs else None
        # Intentionally fail if platform cannot be determined.
        if not env_platform:
            print(
                f"Error: Could not determine platform for environment '{pio_env}'",
                file=sys.stderr,
            )
            sys.exit(1)
        bsf = cfg.get(f"env:{pio_env}", "build_src_filter", default=[])
        bsf_text = " ".join(bsf) if isinstance(bsf, list) else str(bsf or "")
        board_check = (
            cfg.get(f"env:{pio_env}", "board_check", default="false").strip().lower()
            == "true"
        )
        all_envs.append(
            {
                "ci": {"board": pio_env, "platform": env_platform},
                "board_level": cfg.get(f"env:{pio_env}", "board_level", default=None),
                "board_check": board_check,
                "include_dirs": include_dirs,
                "def_dir": defdir.get(pio_env),
                "src_platforms": sorted(set(PLATFORM_INCL_RE.findall(bsf_text))),
            }
        )
    return all_envs


def platform_src_map_from_envs(all_envs):
    """Derive ``platform/<subdir> -> {variant-top-dirs}`` from envs' build_src_filter.

    The ESP32 family self-groups here (every esp32* env re-includes +<platform/esp32>),
    so no hard-coded family list is needed; adding a new arch/board updates this map
    automatically.
    """
    m = defaultdict(set)
    for e in all_envs:
        top = e["ci"]["platform"]
        for sub in e.get("src_platforms", []):
            m[sub].add(top)
    return m


def budget_envs(path=None):
    """Env names carrying a RAM/flash budget (bin/ram_budgets.json)."""
    try:
        with open(path or repo_path(RAM_BUDGETS)) as f:
            raw = json.load(f)
    except (OSError, ValueError):
        return set()
    return {k for k, v in raw.items() if not k.startswith("_") and isinstance(v, dict)}


def _emittable(env):
    """True if an env can appear in the firmware build matrix at all.

    Allowlist on board_level (unknown levels fail closed) plus the covered-elsewhere
    platform exclusion.
    """
    return (
        env["board_level"] in EMITTABLE_LEVELS
        and env["ci"]["platform"] not in COVERED_ELSEWHERE
    )


def select_changed(
    all_envs, changed, platform_src_map, base_ini_incl, budgets=frozenset()
):
    """Return the set of env board-names to build for a PR's ``changed`` files.

    Returns ``None`` to mean "fall back to the full set": any shared, build-system,
    or unrecognized path trips this, so narrowing can never drop an affected board.
    ``platform_src_map`` (platform subdir -> top-dirs) and ``base_ini_incl`` (arch-base
    .ini path -> the platform subdir it re-includes) are derived from PlatformIO in
    ``main``; passing them in keeps this function unit-testable without PlatformIO.
    ``budgets`` (env names) are always unioned in so the fail-closed size-budget-gate
    always has data, which also guarantees a non-empty matrix.
    """
    dir_to_envs = defaultdict(set)  # board dir -> ALL envs there (for path matching)
    pr_by_top = defaultdict(list)
    all_by_top = defaultdict(list)
    emittable = set()
    for e in all_envs:
        board = e["ci"]["board"]
        top = e["ci"]["platform"]
        if _emittable(e):
            emittable.add(board)
            all_by_top[top].append(board)
            if e["board_level"] == "pr":
                pr_by_top[top].append(board)
        for d in e.get("include_dirs", []):  # reverse-map A: -I include dirs
            dir_to_envs[d].add(board)
        if e.get("def_dir"):  # reverse-map B: definition dir
            dir_to_envs[e["def_dir"]].add(board)
    # Only genuine board dirs (variants/<plat>/<board>[/...] => >= 2 slashes), longest
    # first with a trailing "/" so heltec_v4 never swallows heltec_v4_r8.
    board_dirs = sorted(
        (d for d in dir_to_envs if d.count("/") >= 2), key=len, reverse=True
    )

    def add_top(topdirs, sel):
        for td in topdirs:
            if td in COVERED_ELSEWHERE:
                continue
            # A zero-pr arch (nrf54l15, esp32s2) escalates to all of its envs.
            sel.update(pr_by_top.get(td) or all_by_top.get(td, []))

    sel = set()
    for raw in changed:
        p = raw.strip().replace("\\", "/")
        if not p:
            continue
        # Tier 1: variant-local -> the exact board's env(s), regardless of tier.
        hit = next((d for d in board_dirs if p == d or p.startswith(d + "/")), None)
        if hit is not None:
            sel.update(dir_to_envs[hit] & emittable)
            continue
        # Tier 2a: a per-arch platform source tree -> every top-dir that compiles it.
        m = PLATFORM_SRC_RE.match(p)
        if m and m.group(1) in platform_src_map:
            add_top(platform_src_map[m.group(1)], sel)
            continue
        # Tier 2b: an arch base .ini (variants/<plat>/<name>.ini).
        m = ARCH_INI_RE.match(p)
        if m:
            incls = base_ini_incl.get(p) or set()
            mapped = [platform_src_map[s] for s in incls if s in platform_src_map]
            if mapped:
                # Family-wide base: fan out to every arch that compiles each
                # re-included platform tree (a safe superset if it re-includes >1).
                for tops in mapped:
                    add_top(tops, sel)
            else:
                add_top([m.group(1)], sel)  # chip/board base -> its own top dir
            continue
        # Tier 3: shared source / build system / unknown -> full fallback.
        return None
    sel |= set(budgets)
    return sel or None


def build_outlist(all_envs, platform, level, selected):
    """Apply the platform / board_level filters (and optional narrowing) to envs."""
    outlist = []
    want_check = "check" in platform
    for env in all_envs:
        ci = env["ci"]
        if want_check:
            if not env["board_check"]:
                continue
            if selected is not None and ci["board"] not in selected:
                continue
            if "pr" in level:
                if env["board_level"] == "pr":
                    outlist.append(ci)
            else:
                outlist.append(ci)
            continue
        # Filter (non-check) builds by platform.
        if not (platform == "all" or platform == ci["platform"]):
            continue
        if selected is not None:
            # Narrowed: build every selected board, regardless of its board_level.
            if ci["board"] in selected:
                outlist.append(ci)
            continue
        # Always include board_level = 'pr'.
        if env["board_level"] == "pr":
            outlist.append(ci)
        # Include board_level = 'extra' when requested.
        elif "extra" in level and env["board_level"] == "extra":
            outlist.append(ci)
        # If no board level is specified, include in release builds (not PR).
        elif "pr" not in level and not env["board_level"]:
            outlist.append(ci)
    return outlist


def main(argv=None):
    args = parse_args(argv)
    all_envs = load_all_envs()
    platform_src_map = platform_src_map_from_envs(all_envs)

    selected = None
    if args.changed_files:
        try:
            with open(args.changed_files) as f:
                changed = [ln.strip() for ln in f if ln.strip()]
        except OSError:
            changed = []
        # Empty (diff failed / nothing changed / non-PR) -> selected stays None -> full
        # set, so the workflow can pass --changed-files unconditionally.
        if changed:
            selected = select_changed(
                all_envs,
                changed,
                platform_src_map,
                base_ini_platform_incl(globs=board_ini_globs()),
                budgets=budget_envs(),
            )

    # Return as a JSON list
    print(json.dumps(build_outlist(all_envs, args.platform, args.level, selected)))


if __name__ == "__main__":
    main()
