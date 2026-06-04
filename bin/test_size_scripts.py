#!/usr/bin/env python3

"""Tests for bin/collect_sizes.py and bin/size_report.py."""

import json
import os
import subprocess
import sys
import tempfile

SCRIPTS_DIR = os.path.join(os.path.dirname(__file__), "..", "bin")


def make_manifest(target, firmware_bytes, extra_files=None):
    """Create a minimal .mt.json manifest dict."""
    files = [{"name": f"firmware-{target}-2.6.0.bin", "bytes": firmware_bytes}]
    if extra_files:
        files.extend(extra_files)
    return {
        "platformioTarget": target,
        "version": "2.6.0.test",
        "files": files,
    }


def write_manifests(tmpdir, manifests):
    """Write manifest dicts as .mt.json files into tmpdir."""
    for target, data in manifests.items():
        path = os.path.join(tmpdir, f"firmware-{target}.mt.json")
        with open(path, "w") as f:
            json.dump(data, f)


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
        assert sizes == {"heltec-v3": 1048576, "rak4631": 524288, "tbeam": 786432}


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
        assert sizes == {"custom-board": 500000}


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
    """size_report with no baselines shows sizes only."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = os.path.join(tmpdir, "new.json")
        with open(sizes_file, "w") as f:
            json.dump({"heltec-v3": 1000000, "rak4631": 500000}, f)

        rc, stdout, stderr = run_script("size_report.py", [sizes_file])
        assert rc == 0, f"size_report failed: {stderr}"
        assert "2 targets" in stdout
        assert "no baseline available yet" in stdout
        assert "`heltec-v3`" in stdout
        assert "`rak4631`" in stdout


def test_size_report_with_baseline():
    """size_report shows deltas against a baseline."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = os.path.join(tmpdir, "new.json")
        old_file = os.path.join(tmpdir, "old.json")
        with open(new_file, "w") as f:
            json.dump({"heltec-v3": 1050000, "rak4631": 500000, "tbeam": 800000}, f)
        with open(old_file, "w") as f:
            json.dump({"heltec-v3": 1000000, "rak4631": 500000, "tbeam": 810000}, f)

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


def test_size_report_multiple_baselines():
    """size_report handles multiple baselines."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = os.path.join(tmpdir, "new.json")
        dev_file = os.path.join(tmpdir, "develop.json")
        master_file = os.path.join(tmpdir, "master.json")
        with open(new_file, "w") as f:
            json.dump({"board-a": 100000}, f)
        with open(dev_file, "w") as f:
            json.dump({"board-a": 95000}, f)
        with open(master_file, "w") as f:
            json.dump({"board-a": 90000}, f)

        rc, stdout, stderr = run_script(
            "size_report.py",
            [new_file, "--baseline", f"develop:{dev_file}", "--baseline", f"master:{master_file}"],
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "vs `develop`" in stdout
        assert "vs `master`" in stdout


def test_size_report_new_target_no_baseline_entry():
    """size_report handles targets not present in baseline (new boards)."""
    with tempfile.TemporaryDirectory() as tmpdir:
        new_file = os.path.join(tmpdir, "new.json")
        old_file = os.path.join(tmpdir, "old.json")
        with open(new_file, "w") as f:
            json.dump({"new-board": 300000, "existing": 500000}, f)
        with open(old_file, "w") as f:
            json.dump({"existing": 500000}, f)

        rc, stdout, stderr = run_script(
            "size_report.py", [new_file, "--baseline", f"develop:{old_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "`new-board`" in stdout
        assert "no changes" in stdout  # only existing is compared, delta=0


def test_size_report_all_unchanged():
    """size_report shows 'no changes' when all sizes match."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = os.path.join(tmpdir, "sizes.json")
        with open(sizes_file, "w") as f:
            json.dump({"board-a": 100000, "board-b": 200000}, f)

        rc, stdout, stderr = run_script(
            "size_report.py", [sizes_file, "--baseline", f"develop:{sizes_file}"]
        )
        assert rc == 0, f"size_report failed: {stderr}"
        assert "no changes" in stdout


def test_collect_sizes_bad_args():
    """collect_sizes exits with error on wrong arg count."""
    rc, stdout, stderr = run_script("collect_sizes.py", [])
    assert rc == 1
    assert "Usage" in stderr


def test_size_report_bad_baseline_format():
    """size_report exits with error on malformed --baseline."""
    with tempfile.TemporaryDirectory() as tmpdir:
        sizes_file = os.path.join(tmpdir, "sizes.json")
        with open(sizes_file, "w") as f:
            json.dump({"x": 1}, f)

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
