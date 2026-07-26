#!/usr/bin/env python3

"""Tests for bin/collect_sizes.py and bin/size_report.py."""

import json
import os
import subprocess
import sys
import tempfile

SCRIPTS_DIR = os.path.join(os.path.dirname(__file__), "..", "bin")


def make_manifest(target, firmware_bytes, extra_files=None, ram_bytes=None):
    """Create a minimal .mt.json manifest dict."""
    files = [{"name": f"firmware-{target}-2.6.0.bin", "bytes": firmware_bytes}]
    if extra_files:
        files.extend(extra_files)
    manifest = {
        "platformioTarget": target,
        "version": "2.6.0.test",
        "files": files,
    }
    if ram_bytes is not None:
        manifest["ram_bytes"] = ram_bytes
    return manifest


def write_manifests(tmpdir, manifests):
    """Write manifest dicts as .mt.json files into tmpdir."""
    for target, data in manifests.items():
        path = os.path.join(tmpdir, f"firmware-{target}.mt.json")
        with open(path, "w") as f:
            json.dump(data, f)


def write_json(tmpdir, name, data):
    path = os.path.join(tmpdir, name)
    with open(path, "w") as f:
        json.dump(data, f)
    return path


def run_script(script, args):
    """Run a Python script and return (returncode, stdout, stderr)."""
    result = subprocess.run(
        [sys.executable, os.path.join(SCRIPTS_DIR, script)] + args,
        capture_output=True,
        text=True,
    )
    return result.returncode, result.stdout, result.stderr


def test_collect_sizes_basic():
    """collect_sizes picks up firmware-*.bin entries from manifests."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        manifests = {
            "heltec-v3": make_manifest("heltec-v3", 1048576),
            "rak4631": make_manifest("rak4631", 524288),
            "tbeam": make_manifest("tbeam", 786432),
        }
        write_manifests(tmpdir, manifests)

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0, f"collect_sizes failed: {stderr}"
        assert "3 targets" in stdout

        with open(outfile) as f:
            sizes = json.load(f)
        assert sizes == {
            "heltec-v3": {"flash_bytes": 1048576},
            "rak4631": {"flash_bytes": 524288},
            "tbeam": {"flash_bytes": 786432},
        }


def test_collect_sizes_ram_bytes():
    """collect_sizes carries ram_bytes through from the manifest."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        manifests = {
            "rak4631": make_manifest("rak4631", 765192, ram_bytes=110948),
            "heltec-v3": make_manifest("heltec-v3", 1048576),  # no ram_bytes
        }
        write_manifests(tmpdir, manifests)

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0, f"collect_sizes failed: {stderr}"

        with open(outfile) as f:
            sizes = json.load(f)
        assert sizes["rak4631"] == {"flash_bytes": 765192, "ram_bytes": 110948}
        # Manifest without ram_bytes: key absent, not zero / not crashing
        assert sizes["heltec-v3"] == {"flash_bytes": 1048576}


def test_collect_sizes_non_int_ram_bytes_ignored():
    """collect_sizes drops malformed (non-integer) ram_bytes values."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        manifests = {"rak4631": make_manifest("rak4631", 765192, ram_bytes="110948")}
        write_manifests(tmpdir, manifests)

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0, f"collect_sizes failed: {stderr}"
        with open(outfile) as f:
            sizes = json.load(f)
        assert sizes == {"rak4631": {"flash_bytes": 765192}}


def test_collect_sizes_fallback_bin():
    """collect_sizes falls back to non-firmware-prefixed .bin if no firmware-*.bin."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        # Manifest with only a generic .bin (no firmware- prefix)
        data = {
            "platformioTarget": "custom-board",
            "files": [
                {"name": "littlefs-custom-board.bin", "bytes": 100000},
                {"name": "custom-board.bin", "bytes": 500000},
            ],
        }
        path = os.path.join(tmpdir, "firmware-custom-board.mt.json")
        with open(path, "w") as f:
            json.dump(data, f)

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0, f"collect_sizes failed: {stderr}"

        with open(outfile) as f:
            sizes = json.load(f)
        assert sizes == {"custom-board": {"flash_bytes": 500000}}


def test_collect_sizes_skips_ota_littlefs():
    """collect_sizes ignores ota/littlefs/bleota .bin files in fallback."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        data = {
            "platformioTarget": "board-x",
            "files": [
                {"name": "littlefs-board-x.bin", "bytes": 100000},
                {"name": "bleota-board-x.bin", "bytes": 50000},
                {"name": "mt-board-x-ota.bin", "bytes": 60000},
            ],
        }
        path = os.path.join(tmpdir, "firmware-board-x.mt.json")
        with open(path, "w") as f:
            json.dump(data, f)

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0
        with open(outfile) as f:
            sizes = json.load(f)
        # No valid firmware .bin found, board should be absent
        assert sizes == {}


def test_collect_sizes_ignores_non_mt_json():
    """collect_sizes skips non .mt.json files."""
    with tempfile.TemporaryDirectory() as tmpdir:
        outfile = os.path.join(tmpdir, "sizes.json")
        # Write a valid manifest
        manifests = {"rak4631": make_manifest("rak4631", 500000)}
        write_manifests(tmpdir, manifests)
        # Write a decoy file
        with open(os.path.join(tmpdir, "readme.txt"), "w") as f:
            f.write("not a manifest")

        rc, stdout, stderr = run_script("collect_sizes.py", [tmpdir, outfile])
        assert rc == 0
        with open(outfile) as f:
            sizes = json.load(f)
        assert list(sizes.keys()) == ["rak4631"]


def test_size_report_no_baseline():
    """size_report with no baselines shows sizes only (legacy int schema)."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "new.json", {"heltec-v3": 1000000, "rak4631": 500000}
        )

        rc, stdout, stderr = run_script("size_report.py", [sizes_file])
        assert rc == 0, f"size_report failed: {stderr}"
        assert "2 targets" in stdout
        assert "no baseline available yet" in stdout
        assert "`heltec-v3`" in stdout
        assert "`rak4631`" in stdout
        # Legacy schema has no RAM data: column renders n/a, never crashes
        assert "n/a" in stdout


def test_size_report_with_baseline():
    """size_report shows deltas against a baseline (legacy int schema)."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(
            tmpdir,
            "new.json",
            {"heltec-v3": 1050000, "rak4631": 500000, "tbeam": 800000},
        )
        old_file = write_json(
            tmpdir,
            "old.json",
            {"heltec-v3": 1000000, "rak4631": 500000, "tbeam": 810000},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "3 targets" in stdout
        assert "1 increased" in stdout
        assert "1 decreased" in stdout
        # heltec-v3 grew by 50000
        assert "📈" in stdout
        # tbeam shrank by 10000
        assert "📉" in stdout
        # rak4631 unchanged
        assert "vs `develop`" in stdout


def test_size_report_ram_columns():
    """size_report renders RAM and RAM-delta columns from the new schema."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(
            tmpdir,
            "new.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 112000}},
        )
        old_file = write_json(
            tmpdir,
            "old.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 110948}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "RAM vs `develop`" in stdout
        assert "112,000" in stdout
        # RAM grew by 1052 bytes
        assert "+1,052" in stdout


def test_size_report_ram_na_for_legacy_baseline():
    """RAM delta shows n/a when the baseline predates ram_bytes (legacy ints)."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(
            tmpdir,
            "new.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 110948}},
        )
        old_file = write_json(tmpdir, "old.json", {"rak4631": 760000})

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        # Current RAM shown, delta degrades to n/a
        assert "110,948" in stdout
        assert "n/a" in stdout
        # Flash delta still computed
        assert "📈" in stdout


def test_size_report_ram_na_for_current_without_ram():
    """RAM column shows n/a when the current manifest lacks ram_bytes."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(tmpdir, "new.json", {"board-a": {"flash_bytes": 100000}})
        old_file = write_json(
            tmpdir,
            "old.json",
            {"board-a": {"flash_bytes": 100000, "ram_bytes": 50000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "n/a" in stdout


def test_size_report_multiple_baselines():
    """size_report handles multiple baselines."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(tmpdir, "new.json", {"board-a": 100000})
        dev_file = write_json(tmpdir, "develop.json", {"board-a": 95000})
        master_file = write_json(tmpdir, "master.json", {"board-a": 90000})

        rc, stdout, stderr = run_script(
            "size_report.py",
            [
                new_file,
                "--baseline",
                f"develop:{dev_file}",
                "--baseline",
                f"master:{master_file}",
            ],
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "vs `develop`" in stdout
        assert "vs `master`" in stdout


def test_size_report_new_target_no_baseline_entry():
    """size_report handles targets not present in baseline (new boards)."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = write_json(
            tmpdir, "new.json", {"new-board": 300000, "existing": 500000}
        )
        old_file = write_json(tmpdir, "old.json", {"existing": 500000})

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "`new-board`" in stdout
        assert "no changes" in stdout  # only existing is compared, delta=0


def test_size_report_all_unchanged():
    """size_report shows 'no changes' when all sizes match."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"board-a": 100000, "board-b": 200000}
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--baseline", f"develop:{sizes_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "no changes" in stdout


def test_budget_gate_under_budget():
    """Budget gate passes and reports usage when all envs are within budget."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir,
            "sizes.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 110948}},
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {
                "_comment": "budgets are raised deliberately",
                "rak4631": {"ram_bytes": 113000, "flash_bytes": 786000},
            },
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 0, f"expected pass, got rc={rc}: {stderr}"
        assert "Size budgets" in stdout
        assert "113,000" in stdout
        assert "OVER BUDGET" not in stdout


def test_budget_gate_over_ram_budget():
    """Budget gate fails with a clear message when RAM exceeds the budget."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir,
            "sizes.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 118000}},
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 1, f"expected failure, got rc={rc}"
        # Message names the env, measured value, budget, and how to raise it
        assert "rak4631" in stderr
        assert "118,000" in stderr
        assert "113,000" in stderr
        assert "ram_budgets.json" in stderr
        assert "OVER BUDGET" in stdout


def test_budget_gate_over_flash_budget():
    """Budget gate fails when flash exceeds the budget."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir,
            "sizes.json",
            {"rak4631": {"flash_bytes": 790000, "ram_bytes": 110948}},
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 1, f"expected failure, got rc={rc}"
        assert "rak4631" in stderr
        assert "790,000" in stderr
        assert "786,000" in stderr


def test_budget_gate_env_not_built_fails_closed():
    """Under --enforce-budgets, a budgeted env missing from the sizes fails."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"heltec-v3": {"flash_bytes": 1000000}}
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 1, f"expected fail-closed for unbuilt env, got rc={rc}"
        assert "not built" in stderr


def test_budget_report_env_not_built_shows_na():
    """Without enforcement, a budgeted env missing from the sizes is just n/a."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"heltec-v3": {"flash_bytes": 1000000}}
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--budgets", budgets_file]
        )
        assert rc == 0, f"expected report-only pass, got rc={rc}: {stderr}"
        assert "n/a" in stdout


def test_budget_gate_missing_ram_metric_fails_closed():
    """Under --enforce-budgets, a budgeted env without ram_bytes fails."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"rak4631": {"flash_bytes": 765192}}
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 1, f"expected fail-closed for missing metric, got rc={rc}"
        assert "missing this metric" in stderr


def test_budget_report_missing_ram_metric_shows_na():
    """Without enforcement, a missing metric reports n/a and does not fail."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"rak4631": {"flash_bytes": 765192}}
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--budgets", budgets_file]
        )
        assert rc == 0, f"expected report-only pass, got rc={rc}: {stderr}"
        assert "n/a" in stdout


def test_budget_gate_empty_sizes_fails_closed():
    """--enforce-budgets with an empty sizes file must fail, not silently pass."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(tmpdir, "sizes.json", {})
        budgets_file = write_json(
            tmpdir, "budgets.json", {"rak4631": {"ram_bytes": 113000}}
        )

        rc, stdout, stderr = run_script(
            "size_report.py",
            [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
        )
        assert rc == 1, f"expected fail-closed for empty sizes, got rc={rc}"
        assert "no sizes" in stderr

        # Report-only mode keeps the quiet no-op behavior
        rc, stdout, stderr = run_script("size_report.py", [sizes_file])
        assert rc == 0


def test_budget_invalid_budget_rejected():
    """Zero/negative/non-int budgets fail loudly instead of crashing or passing."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir, "sizes.json", {"rak4631": {"ram_bytes": 110948}}
        )
        for bad in (0, -5, "113000", True):
            budgets_file = write_json(
                tmpdir, "budgets.json", {"rak4631": {"ram_bytes": bad}}
            )
            rc, stdout, stderr = run_script(
                "size_report.py",
                [sizes_file, "--budgets", budgets_file, "--enforce-budgets"],
            )
            assert rc == 1, f"expected rejection of budget {bad!r}, got rc={rc}"
            assert "positive integer" in stderr


def test_budget_render_without_enforce():
    """--budgets alone renders the table but never fails, even over budget."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(
            tmpdir,
            "sizes.json",
            {"rak4631": {"flash_bytes": 765192, "ram_bytes": 999999}},
        )
        budgets_file = write_json(
            tmpdir,
            "budgets.json",
            {"rak4631": {"ram_bytes": 113000, "flash_bytes": 786000}},
        )

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--budgets", budgets_file]
        )
        assert rc == 0, f"render-only budgets must not fail: {stderr}"
        assert "OVER BUDGET" in stdout


def test_budget_enforce_requires_budgets():
    """--enforce-budgets without --budgets is an argument error."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(tmpdir, "sizes.json", {"x": 1})

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--enforce-budgets"]
        )
        assert rc == 1
        assert "--budgets" in stderr


def test_collect_sizes_bad_args():
    """collect_sizes exits with error on wrong arg count."""
    rc, stdout, stderr = run_script("collect_sizes.py", [])
    assert rc == 1
    assert "Usage" in stderr


def test_size_report_bad_baseline_format():
    """size_report exits with error on malformed --baseline."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = write_json(tmpdir, "sizes.json", {"x": 1})

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--baseline", "no-colon-here"]
        )
        assert rc == 1
        assert "LABEL:PATH" in stderr


if __name__ == "__main__":
    tests = [v for k, v in globals().items() if k.startswith("test_")]
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
        except Exception as e:
            print(f"  ERROR: {test.__name__}: {type(e).__name__}: {e}")
            failed += 1

    print(f"\n{passed} passed, {failed} failed out of {passed + failed}")
    sys.exit(1 if failed else 0)
