#!/usr/bin/env python3

"""Tests for bin/generate_ci_matrix.py's PR change-detection (select_changed).

These exercise the pure selection logic with a synthetic env universe, so they run
without PlatformIO installed (the PlatformIO import in generate_ci_matrix.py is
deferred into load_all_envs, which these tests never call).

The source-tree -> arch maps that select_changed consumes are themselves derived from
the envs' build_src_filter (platform_src_map_from_envs) and the arch-base .inis
(base_ini_platform_incl); the synthetic universe below carries the same
``src_platforms`` metadata a real env would, so the derivation is exercised end-to-end.
"""

import importlib.util
import json
import os
import sys
import tempfile

# Import generate_ci_matrix.py as a module without triggering its __main__ path.
_SPEC = importlib.util.spec_from_file_location(
    "gcm", os.path.join(os.path.dirname(__file__), "generate_ci_matrix.py")
)
gcm = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(gcm)

BUDGETS = frozenset({"rak4631"})

# Arch-base .ini -> the set of platform subdirs it re-includes (family-wide bases).
# Mirrors what base_ini_platform_incl() derives from the real tree: only
# esp32-common.ini carries a +<platform/esp32> re-include; the chip base esp32.ini
# merely inherits it. Values are sets so a base that re-includes multiple platform
# trees fans out to a safe superset instead of being silently dropped.
BASE_INI_INCL = {"variants/esp32/esp32-common.ini": {"esp32"}}


def env(
    board,
    platform,
    level=None,
    check=False,
    include_dirs=None,
    def_dir=None,
    src_platforms=None,
):
    """Build one synthetic all_envs entry."""
    return {
        "ci": {"board": board, "platform": platform},
        "board_level": level,
        "board_check": check,
        "include_dirs": include_dirs if include_dirs is not None else [],
        "def_dir": def_dir,
        "src_platforms": src_platforms if src_platforms is not None else [],
    }


# Every firmware env compiles its own arch tree plus the shared extra_variants tree
# (base does -<platform/> then +<platform/extra_variants/> +<platform/<arch>/>).
def _sp(arch):
    return [arch, "extra_variants"]


def universe():
    """A representative synthetic env universe covering the tricky cases."""
    return [
        # esp32: a release-only board and an extra board sharing one variant dir.
        env(
            "tbeam",
            "esp32",
            level=None,
            include_dirs=["variants/esp32/tbeam"],
            def_dir="variants/esp32/tbeam",
            src_platforms=_sp("esp32"),
        ),
        env(
            "tbeam-displayshield",
            "esp32",
            level="extra",
            include_dirs=["variants/esp32/tbeam"],
            def_dir="variants/esp32/tbeam",
            src_platforms=_sp("esp32"),
        ),
        env(
            "esp32-pr-rep",
            "esp32",
            level="pr",
            include_dirs=["variants/esp32/somepr"],
            def_dir="variants/esp32/somepr",
            src_platforms=_sp("esp32"),
        ),
        # esp32s3: a pr rep + two boards whose dirs nest (heltec_v4 / heltec_v4_r8).
        env(
            "heltec-v3",
            "esp32s3",
            level="pr",
            check=True,
            include_dirs=["variants/esp32s3/heltec_v3"],
            def_dir="variants/esp32s3/heltec_v3",
            src_platforms=_sp("esp32"),
        ),
        env(
            "heltec_v4",
            "esp32s3",
            level="extra",
            include_dirs=["variants/esp32s3/heltec_v4"],
            def_dir="variants/esp32s3/heltec_v4",
            src_platforms=_sp("esp32"),
        ),
        env(
            "heltec_v4_r8",
            "esp32s3",
            level="extra",
            include_dirs=["variants/esp32s3/heltec_v4_r8"],
            def_dir="variants/esp32s3/heltec_v4_r8",
            src_platforms=_sp("esp32"),
        ),
        # nrf52840: the budgeted pr board, a shared "kit" base, and two derived boards
        # that live in their own dir but -I the shared kit dir.
        env(
            "rak4631",
            "nrf52840",
            level="pr",
            check=True,
            include_dirs=["variants/nrf52840/rak4631"],
            def_dir="variants/nrf52840/rak4631",
            src_platforms=_sp("nrf52"),
        ),
        env(
            "kit_base",
            "nrf52840",
            level="extra",
            include_dirs=["variants/nrf52840/seeed_xiao_nrf52840_kit"],
            def_dir="variants/nrf52840/seeed_xiao_nrf52840_kit",
            src_platforms=_sp("nrf52"),
        ),
        env(
            "xiao_e22_30",
            "nrf52840",
            level="extra",
            include_dirs=["variants/nrf52840/seeed_xiao_nrf52840_kit"],
            def_dir="variants/nrf52840/diy/seeed_xiao_nrf52840_e22",
            src_platforms=_sp("nrf52"),
        ),
        env(
            "xiao_e22_33",
            "nrf52840",
            level="extra",
            include_dirs=["variants/nrf52840/seeed_xiao_nrf52840_kit"],
            def_dir="variants/nrf52840/diy/seeed_xiao_nrf52840_e22",
            src_platforms=_sp("nrf52"),
        ),
        # rp2040 / rp2350 pr reps (share the src/platform/rp2xx0 tree).
        env(
            "rpipico",
            "rp2040",
            level="pr",
            include_dirs=["variants/rp2040/rpipico"],
            def_dir="variants/rp2040/rpipico",
            src_platforms=_sp("rp2xx0"),
        ),
        env(
            "rak11310",
            "rp2350",
            level="pr",
            include_dirs=["variants/rp2350/rak11310"],
            def_dir="variants/rp2350/rak11310",
            src_platforms=_sp("rp2xx0"),
        ),
        # nrf54l15: zero pr-tier envs -> arch changes must escalate to all its envs.
        env(
            "nrf54l15dk",
            "nrf54l15",
            level="extra",
            include_dirs=["variants/nrf54l15/nrf54l15dk"],
            def_dir="variants/nrf54l15/nrf54l15dk",
            src_platforms=_sp("nrf54l15"),
        ),
        # native: excluded from the build matrix (covered by test-native/docker/wasm).
        env(
            "native",
            "native",
            level="extra",
            include_dirs=["variants/native/portduino"],
            def_dir="variants/native/portduino",
            src_platforms=["portduino"],
        ),
        env(
            "native-tft",
            "native",
            level="extra",
            include_dirs=["variants/native/portduino"],
            def_dir="variants/native/portduino",
            src_platforms=["portduino"],
        ),
    ]


def sel(changed):
    """Run select_changed with maps derived from the synthetic universe."""
    u = universe()
    return gcm.select_changed(
        u,
        changed,
        gcm.platform_src_map_from_envs(u),
        BASE_INI_INCL,
        budgets=BUDGETS,
    )


def test_variant_local_builds_touched_boards_incl_extra():
    """Editing a variant dir builds every env there, even extra-tier ones."""
    result = sel(["variants/esp32/tbeam/variant.h"])
    # Both the release-only tbeam and the extra tbeam-displayshield share the dir.
    assert result == {"tbeam", "tbeam-displayshield", "rak4631"}


def test_budgeted_env_always_present():
    """rak4631 (the only budgeted env) is unioned into every narrowed result."""
    assert "rak4631" in sel(["variants/esp32/tbeam/variant.h"])
    assert "rak4631" in sel(["variants/rp2040/rpipico/variant.h"])


def test_longest_prefix_no_sibling_bleed():
    """A nested board dir must not be swallowed by its shorter sibling."""
    assert sel(["variants/esp32s3/heltec_v4_r8/variant.h"]) == {
        "heltec_v4_r8",
        "rak4631",
    }
    assert sel(["variants/esp32s3/heltec_v4/variant.h"]) == {"heltec_v4", "rak4631"}


def test_definition_dir_catches_derived_board():
    """Editing a derived board's own dir selects it even though its -I is shared."""
    result = sel(["variants/nrf52840/diy/seeed_xiao_nrf52840_e22/platformio.ini"])
    assert result == {"xiao_e22_30", "xiao_e22_33", "rak4631"}
    # kit_base is NOT selected: it lives in the shared kit dir, not the e22 dir.
    assert "kit_base" not in result


def test_shared_include_dir_fans_out():
    """Editing a shared variant dir selects every env that -I-includes it."""
    result = sel(["variants/nrf52840/seeed_xiao_nrf52840_kit/variant.h"])
    assert result == {"kit_base", "xiao_e22_30", "xiao_e22_33", "rak4631"}


def test_platform_src_esp32_family():
    """src/platform/esp32 maps to the whole ESP32 family's pr reps (derived)."""
    result = sel(["src/platform/esp32/ESP32CryptoEngine.cpp"])
    assert result == {"esp32-pr-rep", "heltec-v3", "rak4631"}


def test_platform_src_nrf52():
    result = sel(["src/platform/nrf52/NRF52Bluetooth.cpp"])
    assert result == {"rak4631"}


def test_platform_src_rp2xx0_both_chips():
    result = sel(["src/platform/rp2xx0/main-rp2xx0.cpp"])
    assert result == {"rpipico", "rak11310", "rak4631"}


def test_zero_pr_arch_escalates_to_all_envs():
    """nrf54l15 has no pr env, so an arch change builds all of its envs."""
    assert sel(["src/platform/nrf54l15/main-nrf54l15.cpp"]) == {
        "nrf54l15dk",
        "rak4631",
    }


def test_platform_src_extra_variants_covers_every_arch():
    """The shared extra_variants tree fans out to a rep of every (non-native) arch.

    Previously this was an unmapped path -> Tier-3 full fallback; deriving the map
    from build_src_filter makes it explicit. The result is a safe superset (every
    arch's pr envs), never an under-build, and never None here."""
    result = sel(["src/platform/extra_variants/foo.cpp"])
    assert result == {
        "esp32-pr-rep",
        "heltec-v3",
        "rak4631",
        "rpipico",
        "rak11310",
        "nrf54l15dk",
    }


def test_native_platform_src_adds_nothing_but_not_full():
    """A portduino-only change adds no build env (covered elsewhere), not a fallback."""
    assert sel(["src/platform/portduino/PortduinoGlue.cpp"]) == {"rak4631"}


def test_native_variant_change_adds_nothing_but_not_full():
    result = sel(["variants/native/portduino/platformio.ini"])
    assert result == {"rak4631"}


def test_arch_ini_common_is_family_wide():
    """esp32-common.ini (a +<platform/esp32> base) affects the whole ESP32 family."""
    assert sel(["variants/esp32/esp32-common.ini"]) == {
        "esp32-pr-rep",
        "heltec-v3",
        "rak4631",
    }


def test_arch_ini_chip_base_is_scoped():
    """esp32.ini (inherits the include, not a family base) affects only its top dir."""
    result = sel(["variants/esp32/esp32.ini"])
    assert result == {"esp32-pr-rep", "rak4631"}
    assert "heltec-v3" not in result  # s3 boards unaffected by the esp32-only base


def test_arch_ini_nrf52840():
    assert sel(["variants/nrf52840/nrf52840.ini"]) == {"rak4631"}


def test_shared_src_falls_back_to_full():
    """A shared source file returns None (full fallback)."""
    assert sel(["src/mesh/Router.cpp"]) is None
    assert sel(["src/main.cpp"]) is None


def test_build_system_and_unknown_fall_back_to_full():
    for path in (
        "platformio.ini",
        "bin/generate_ci_matrix.py",
        ".github/workflows/main_matrix.yml",
        "boards/ttgo-tbeam.json",
        "src/mesh/generated/meshtastic/config.pb.h",
        "variants/nrf52840/cpp_overrides/lfs_util.h",
    ):
        assert sel([path]) is None, f"{path} should force a full build"


def test_mixed_shared_wins():
    """Any shared file in the set forces a full fallback regardless of order."""
    assert sel(["variants/esp32/tbeam/variant.h", "src/mesh/Router.cpp"]) is None
    assert sel(["src/mesh/Router.cpp", "variants/esp32/tbeam/variant.h"]) is None


def test_multiple_variant_dirs_union():
    result = sel(
        ["variants/esp32s3/heltec_v3/variant.h", "variants/rp2040/rpipico/variant.h"]
    )
    assert result == {"heltec-v3", "rpipico", "rak4631"}


def test_build_outlist_narrowed_builds_selected_regardless_of_level():
    """The build ('all') leg emits a selected board even if it's extra-tier."""
    out = gcm.build_outlist(universe(), "all", ["pr"], selected={"tbeam-displayshield"})
    assert out == [{"board": "tbeam-displayshield", "platform": "esp32"}]


def test_build_outlist_full_pr_set_when_not_narrowed():
    """selected=None reproduces the classic pr-set behavior for the build leg."""
    out = gcm.build_outlist(universe(), "all", ["pr"], selected=None)
    boards = {e["board"] for e in out}
    assert boards == {"esp32-pr-rep", "heltec-v3", "rak4631", "rpipico", "rak11310"}


def test_build_outlist_check_leg_full_when_not_narrowed():
    """The check leg (selected=None) yields board_check && pr envs."""
    out = gcm.build_outlist(universe(), "check", ["pr"], selected=None)
    assert {e["board"] for e in out} == {"heltec-v3", "rak4631"}


def test_platform_src_map_from_envs_derives_family_grouping():
    """The platform subdir -> top-dirs map is derived from envs' src_platforms."""
    m = gcm.platform_src_map_from_envs(universe())
    assert m["esp32"] == {"esp32", "esp32s3"}  # family self-groups, no hard-coded list
    assert m["nrf52"] == {"nrf52840"}
    assert m["rp2xx0"] == {"rp2040", "rp2350"}
    assert m["nrf54l15"] == {"nrf54l15"}
    assert m["portduino"] == {"native"}
    # extra_variants is shared across every firmware arch's top-dir. native envs
    # compile platform/portduino only (not extra_variants), so native is absent.
    assert m["extra_variants"] == {
        "esp32",
        "esp32s3",
        "nrf52840",
        "rp2040",
        "rp2350",
        "nrf54l15",
    }


def test_emittable_allowlist_fails_closed():
    """Unknown board_level values are excluded; only None/pr/extra emit."""
    assert gcm._emittable(env("a", "esp32", level=None))
    assert gcm._emittable(env("a", "esp32", level="pr"))
    assert gcm._emittable(env("a", "esp32", level="extra"))
    # Retired 'community' tier and any typo fail closed.
    assert not gcm._emittable(env("a", "esp32", level="community"))
    assert not gcm._emittable(env("a", "esp32", level="bogus"))
    # covered-elsewhere platform is excluded regardless of a valid level.
    assert not gcm._emittable(env("a", "native", level="pr"))


def test_ini_glob_list_normalizes_str_and_list():
    """extra_configs may arrive as a newline string or a list; only .ini globs pass."""
    raw = "variants/*/*.ini\nvariants/*/*/platformio.ini\nsrc/x/Config.ini\nnot_a.txt"
    assert gcm._ini_glob_list(raw) == [
        "variants/*/*.ini",
        "variants/*/*/platformio.ini",
        "src/x/Config.ini",
    ]
    assert gcm._ini_glob_list(["a.ini", " b.ini ", "c.json"]) == ["a.ini", "b.ini"]
    assert gcm._ini_glob_list([]) == []
    assert gcm._ini_glob_list(None) == []


def test_env_definition_dirs_parses_headers():
    """env_definition_dirs maps each [env:NAME] to its platformio.ini's dir."""
    with tempfile.TemporaryDirectory() as root:
        board_dir = os.path.join(root, "variants", "esp32", "tbeam")
        os.makedirs(board_dir)
        with open(os.path.join(board_dir, "platformio.ini"), "w") as f:
            f.write("[env:tbeam]\nboard = ttgo-tbeam\n\n[env:tbeam-displayshield]\n")
        diy_dir = os.path.join(root, "variants", "nrf52840", "diy", "washy")
        os.makedirs(diy_dir)
        with open(os.path.join(diy_dir, "platformio.ini"), "w") as f:
            f.write("[env:WashTastic]\n")

        defdir = gcm.env_definition_dirs(root=root)
        assert defdir["tbeam"] == "variants/esp32/tbeam"
        assert defdir["tbeam-displayshield"] == "variants/esp32/tbeam"
        assert defdir["WashTastic"] == "variants/nrf52840/diy/washy"


def test_base_ini_platform_incl_finds_family_base_only():
    """A base .ini that re-includes one or more platform trees is a family base.

    A chip base that only inherits (${esp32_common.build_src_filter}) has no direct
    +<platform/X> and must be absent, so a change to it stays scoped to its top-dir.
    Values are sets; a base re-including multiple trees maps to all of them."""
    with tempfile.TemporaryDirectory() as root:
        esp32_dir = os.path.join(root, "variants", "esp32")
        os.makedirs(esp32_dir)
        # Family base: carries the +<platform/esp32> re-include. (In the real tree
        # +<platform/extra_variants/> lives in the root base, so this file's only
        # direct +<platform/X> is esp32 -> exactly one hit.)
        with open(os.path.join(esp32_dir, "esp32-common.ini"), "w") as f:
            f.write(
                "[esp32_common]\nbuild_src_filter =\n"
                "  ${arduino_base.build_src_filter} +<platform/esp32/>\n"
            )
        # Chip base: only inherits, no direct +<platform/X>.
        with open(os.path.join(esp32_dir, "esp32.ini"), "w") as f:
            f.write(
                "[esp32_base]\nbuild_src_filter =\n"
                "  ${esp32_common.build_src_filter}\n"
            )
        nrf_dir = os.path.join(root, "variants", "nrf52840")
        os.makedirs(nrf_dir)
        with open(os.path.join(nrf_dir, "nrf52.ini"), "w") as f:
            f.write("[nrf52_base]\nbuild_src_filter =\n  +<platform/nrf52/>\n")

        incl = gcm.base_ini_platform_incl(root=root)
        assert incl["variants/esp32/esp32-common.ini"] == {"esp32"}
        assert incl["variants/nrf52840/nrf52.ini"] == {"nrf52"}
        assert "variants/esp32/esp32.ini" not in incl


def test_base_ini_platform_incl_multi_include_maps_to_all():
    """A base re-including 2+ platform trees maps to the full set (fail-safe).

    Previously such a base was silently dropped (len(hits)==1 guard), which would
    scope a change to it to its own top-dir only -> under-build. Now it maps to every
    re-included tree so Tier 2b fans out to a safe superset."""
    with tempfile.TemporaryDirectory() as root:
        esp32_dir = os.path.join(root, "variants", "esp32")
        os.makedirs(esp32_dir)
        with open(os.path.join(esp32_dir, "esp32-common.ini"), "w") as f:
            f.write(
                "[esp32_common]\nbuild_src_filter =\n"
                "  -<platform/> +<platform/esp32/> +<platform/firmware_common/>\n"
            )

        incl = gcm.base_ini_platform_incl(root=root)
        assert incl["variants/esp32/esp32-common.ini"] == {"esp32", "firmware_common"}


def test_tier2b_multi_include_base_fans_out_superset():
    """A multi-include family base selects every arch that compiles either tree.

    esp32-common re-includes both esp32 and (hypothetically) extra_variants; a change
    to it must build every esp32-family env AND every arch compiling extra_variants --
    a safe superset, never an under-build."""
    u = universe()
    multi = dict(BASE_INI_INCL)
    multi["variants/esp32/esp32-common.ini"] = {"esp32", "extra_variants"}
    result = gcm.select_changed(
        u,
        ["variants/esp32/esp32-common.ini"],
        gcm.platform_src_map_from_envs(u),
        multi,
        budgets=BUDGETS,
    )
    # esp32 family (esp32-pr-rep, heltec-v3) plus every extra_variants arch's pr reps.
    assert result == {
        "esp32-pr-rep",
        "heltec-v3",
        "rak4631",
        "rpipico",
        "rak11310",
        "nrf54l15dk",
    }


def test_board_ini_globs_prefers_extra_configs():
    """board_ini_globs returns the derived extra_configs globs, or the defaults.

    A fake cfg lets this run without PlatformIO. Ensures the same glob list backs both
    env_definition_dirs and base_ini_platform_incl (Win B applied consistently)."""

    class _Cfg:
        def __init__(self, value):
            self._value = value

        def get(self, section, option, default=None):
            assert section == "platformio" and option == "extra_configs"
            return self._value

    derived = "variants/*/*.ini\nvariants/*/bases/*.ini\nnot_a.txt"
    assert gcm.board_ini_globs(cfg=_Cfg(derived)) == [
        "variants/*/*.ini",
        "variants/*/bases/*.ini",
    ]
    # Empty extra_configs falls back to the built-in defaults.
    assert gcm.board_ini_globs(cfg=_Cfg([])) == gcm.DEFAULT_BOARD_INI_GLOBS


def test_budget_envs_reads_ram_budgets():
    """budget_envs returns the budgeted env names and skips comment keys."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "b.json")
        with open(path, "w") as f:
            json.dump({"_comment": ["x"], "rak4631": {"ram_bytes": 1}, "bad": 5}, f)
        assert gcm.budget_envs(path) == {"rak4631"}

    # The real repo file always includes rak4631.
    assert "rak4631" in gcm.budget_envs()


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    passed = 0
    failed = 0
    for test in tests:
        try:
            test()
            print(f"  PASS: {test.__name__}")
            passed += 1
        except AssertionError as e:
            print(f"  FAIL: {test.__name__}: {e}")
            failed += 1
        except Exception as e:  # noqa: BLE001
            print(f"  ERROR: {test.__name__}: {type(e).__name__}: {e}")
            failed += 1

    print(f"\n{passed} passed, {failed} failed out of {passed + failed}")
    sys.exit(1 if failed else 0)
