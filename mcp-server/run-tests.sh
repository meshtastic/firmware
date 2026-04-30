#!/usr/bin/env bash
# mcp-server hardware test runner.
#
# Auto-detects connected Meshtastic devices, maps each to its PlatformIO env
# via the same role table the pytest fixtures use, exports the right
# MESHTASTIC_MCP_ENV_* env vars, and invokes pytest.
#
# Usage:
#   ./run-tests.sh                        # full suite, default pytest args
#   ./run-tests.sh tests/mesh             # subset (any pytest args pass through)
#   ./run-tests.sh --force-bake           # override one default with another
#   MESHTASTIC_MCP_ENV_NRF52=foo ./run-tests.sh   # override env per role
#   MESHTASTIC_MCP_SEED=ci-run-42 ./run-tests.sh  # override PSK seed
#
# If zero supported devices are detected, only the unit tier runs.
#
# Also restores `userPrefs.jsonc` from the session-backup sidecar if a prior
# run exited abnormally (belt to conftest.py's atexit suspenders).

set -euo pipefail

# cd to the script's directory so relative paths resolve consistently no
# matter where the user invoked from.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

VENV_PY="$SCRIPT_DIR/.venv/bin/python"
if [[ ! -x $VENV_PY ]]; then
	echo "error: $VENV_PY not found or not executable." >&2
	echo "       Bootstrap the venv first:" >&2
	echo "         cd $SCRIPT_DIR && python3 -m venv .venv && .venv/bin/pip install -e '.[test]'" >&2
	exit 2
fi

# Resolve firmware root the same way conftest.py does (this script sits in
# mcp-server/, firmware repo root is one level up).
FIRMWARE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
USERPREFS_PATH="$FIRMWARE_ROOT/userPrefs.jsonc"
USERPREFS_SIDECAR="$USERPREFS_PATH.mcp-session-bak"

# ---------- Pre-flight: recover stale userPrefs.jsonc from prior crash ----
# If conftest.py's atexit hook didn't fire (SIGKILL, kernel panic, OS
# restart), the sidecar is the ground truth. Self-heal before running so we
# don't bake the previous run's dirty state into this run's firmware.
if [[ -f $USERPREFS_SIDECAR ]]; then
	echo "[pre-flight] found $USERPREFS_SIDECAR from a prior abnormal exit;" >&2
	echo "             restoring userPrefs.jsonc before starting." >&2
	cp "$USERPREFS_SIDECAR" "$USERPREFS_PATH"
	rm -f "$USERPREFS_SIDECAR"
fi

# If userPrefs.jsonc has uncommitted changes BEFORE the run starts, that's
# worth warning about — tests will snapshot this dirty state and restore to
# it at the end, which may not be what the operator wants.
if command -v git >/dev/null 2>&1; then
	cd "$FIRMWARE_ROOT"
	# Capture the git status into a local first — SC2312 flags command
	# substitution inside `[[ -n ... ]]` because the exit code of `git
	# status` is masked. A two-step assignment makes the failure path
	# explicit (non-git, missing file) and keeps the bracket test clean.
	_git_status_porcelain="$(git status --porcelain userPrefs.jsonc 2>/dev/null || true)"
	if [[ -n $_git_status_porcelain ]]; then
		echo "[pre-flight] warning: userPrefs.jsonc has uncommitted changes." >&2
		echo "             Tests will snapshot THIS state and restore to it" >&2
		echo "             at teardown. If that's not intended, run:" >&2
		echo "               git checkout userPrefs.jsonc" >&2
		echo "             and re-invoke." >&2
	fi
	cd "$SCRIPT_DIR"
fi

# ---------- Seed default --------------------------------------------------
# Per-machine default so repeated runs from the same operator land on the
# same PSK (makes --assume-baked valid across invocations). Operator can
# override with an explicit env var if they want isolation (e.g. CI).
if [[ -z ${MESHTASTIC_MCP_SEED-} ]]; then
	WHO="$(whoami 2>/dev/null || echo anon)"
	HOST="$(hostname -s 2>/dev/null || echo host)"
	export MESHTASTIC_MCP_SEED="mcp-${WHO}-${HOST}"
fi

# ---------- Flash progress log --------------------------------------------
# pio.py / hw_tools.py tee subprocess output (pio run -t upload, esptool,
# nrfutil, picotool) to this file line-by-line as it arrives when this env
# var is set. The TUI tails it so the operator sees live flash progress
# instead of 3 minutes of silence during `test_00_bake.py`. Plain CLI users
# also benefit — the log is a post-run diagnostic even without the TUI.
# Truncate at session start so each run gets a clean log.
export MESHTASTIC_MCP_FLASH_LOG="$SCRIPT_DIR/tests/flash.log"
: >"$MESHTASTIC_MCP_FLASH_LOG"

# ---------- Detect connected hardware -------------------------------------
# In-process call to the same Python API the test fixtures use, so the
# script never drifts from what pytest sees. Returns a JSON object
# {role: port, ...}.
ROLES_JSON="$(
	"$VENV_PY" - <<'PY'
import json
import sys

sys.path.insert(0, "src")
from meshtastic_mcp import devices

# Role → canonical VID map. Kept in sync with
# `tests/conftest.py::hub_profile` defaults; if that changes, this must too.
ROLE_BY_VID = {
    0x239A: "nrf52",     # Adafruit / RAK nRF52 native USB (app + DFU)
    0x303A: "esp32s3",   # Espressif native USB (ESP32-S3)
    0x10C4: "esp32s3",   # CP2102 USB-UART (common on Heltec/LilyGO ESP32 boards)
}

out: dict[str, str] = {}
for dev in devices.list_devices(include_unknown=True):
    vid_raw = dev.get("vid") or ""
    try:
        if isinstance(vid_raw, str) and vid_raw.startswith("0x"):
            vid = int(vid_raw, 16)
        else:
            vid = int(vid_raw)
    except (TypeError, ValueError):
        continue
    role = ROLE_BY_VID.get(vid)
    # First port wins per role — matches hub_devices fixture semantics.
    if role and role not in out:
        out[role] = dev["port"]

json.dump(out, sys.stdout)
PY
)"

# ---------- Map role → pio env --------------------------------------------
# Honor MESHTASTIC_MCP_ENV_<ROLE> operator overrides; fall back to the
# same defaults hardcoded in tests/conftest.py::_DEFAULT_ROLE_ENVS.
resolve_env() {
	local role="$1"
	local default="$2"
	local upper
	upper="$(echo "$role" | tr '[:lower:]' '[:upper:]')"
	local var="MESHTASTIC_MCP_ENV_${upper}"
	eval "local override=\${$var:-}"
	if [[ -n $override ]]; then
		echo "$override"
	else
		echo "$default"
	fi
}

NRF52_PORT="$(echo "$ROLES_JSON" | "$VENV_PY" -c 'import json,sys; print(json.loads(sys.stdin.read()).get("nrf52", ""))')"
ESP32S3_PORT="$(echo "$ROLES_JSON" | "$VENV_PY" -c 'import json,sys; print(json.loads(sys.stdin.read()).get("esp32s3", ""))')"

DETECTED=""
if [[ -n $NRF52_PORT ]]; then
	NRF52_ENV="$(resolve_env nrf52 rak4631)"
	export MESHTASTIC_MCP_ENV_NRF52="$NRF52_ENV"
	DETECTED="${DETECTED}  nrf52   @ ${NRF52_PORT} -> env=${NRF52_ENV}\n"
fi
if [[ -n $ESP32S3_PORT ]]; then
	ESP32S3_ENV="$(resolve_env esp32s3 heltec-v3)"
	export MESHTASTIC_MCP_ENV_ESP32S3="$ESP32S3_ENV"
	DETECTED="${DETECTED}  esp32s3 @ ${ESP32S3_PORT} -> env=${ESP32S3_ENV}\n"
fi

# ---------- Pre-flight summary --------------------------------------------
# Surface what pytest is about to do with respect to the bake phase: the
# operator should see "will verify + bake if needed" by default, so a
# 3-minute flash appearing mid-run isn't a surprise. Detection of the
# explicit overrides is best-effort — we just scan $@ for the known flags.
_bake_mode="auto (verify + bake if needed)"
for _arg in "$@"; do
	case "$_arg" in
	--assume-baked) _bake_mode="skip (--assume-baked)" ;;
	--force-bake) _bake_mode="force (--force-bake)" ;;
	*) ;; # any other arg: pass-through; bake mode unchanged
	esac
done

echo "mcp-server test runner"
echo "  firmware root : $FIRMWARE_ROOT"
echo "  seed          : $MESHTASTIC_MCP_SEED"
echo "  bake          : $_bake_mode"
if [[ -n $DETECTED ]]; then
	echo "  detected hub  :"
	printf "%b" "$DETECTED"
else
	echo "  detected hub  : (none)"
fi
echo

# ---------- Invoke pytest -------------------------------------------------
# If no devices detected, only the unit tier would produce meaningful
# PASS/FAIL — every hardware test would SKIP with "role not present". We
# narrow to tests/unit explicitly so the summary reads as "no hardware,
# unit suite only" instead of "big skip count looks suspicious".
if [[ -z $DETECTED && $# -eq 0 ]]; then
	echo "[pre-flight] no supported devices detected; running unit tier only."
	echo
	exec "$VENV_PY" -m pytest tests/unit -v --report-log=tests/reportlog.jsonl
fi

# Default pytest args when the user passed none. Power users can invoke
# `./run-tests.sh tests/mesh -v --tb=long` and skip all of these defaults.
#
# NOTE: `--assume-baked` is DELIBERATELY omitted here. `tests/test_00_bake.py`
# has an internal skip-if-already-baked check (`_bake_role`: query device_info,
# compare region + primary_channel to the session profile, skip on match).
# So the fast path is ~8-10 s of verification overhead when the devices are
# already baked — negligible next to the 2-6 min suite runtime. Letting
# test_00_bake.py run means a fresh device, a re-seeded session, or a post-
# factory-reset device gets flashed automatically instead of silently
# skipping half the hardware tests with "not baked with session profile"
# errors. Power users who know their hardware is current and want to shave
# those seconds can pass `--assume-baked` explicitly.
if [[ $# -eq 0 ]]; then
	set -- tests/ \
		--html=tests/report.html --self-contained-html \
		--junitxml=tests/junit.xml \
		-v --tb=short
fi

# UI tier requires opencv-python-headless (and ideally easyocr). If it's
# not installed, auto-deselect tests/ui so operators without the [ui]
# extra still get a green run. Printed in yellow; silent when cv2 is
# present.
_cv2_ok=0
if "$VENV_PY" -c "import cv2" >/dev/null 2>&1; then
	_cv2_ok=1
fi
_running_ui=0
for _arg in "$@"; do
	case "$_arg" in
	*tests/ui* | tests/) _running_ui=1 ;;
	*) ;;
	esac
done
if [[ $_running_ui -eq 1 && $_cv2_ok -eq 0 ]]; then
	printf '\033[33m[pre-flight] tests/ui tier detected, but opencv-python-headless is not installed — deselecting.\033[0m\n'
	printf '             install with: .venv/bin/pip install -e "mcp-server/.[ui]"\n'
	echo
	set -- "$@" --ignore=tests/ui
fi

# Recovery tier needs `uhubctl` on PATH — it power-cycles devices via USB
# hub PPPS. The tier's conftest already skips cleanly, so this is just a
# friendly heads-up before the skip happens. `baked_single`'s auto-
# recovery hook also benefits from having uhubctl available across the
# whole suite.
if ! command -v uhubctl >/dev/null 2>&1; then
	printf "\033[33m[pre-flight] uhubctl not found on PATH — recovery tier will skip, and\n"
	printf "             wedged-device auto-recovery is disabled.\033[0m\n"
	printf "             install with: brew install uhubctl (macOS) or apt install uhubctl (Debian/Ubuntu).\n"
	echo
fi

# Always emit `tests/reportlog.jsonl` (unless the operator explicitly passed
# their own `--report-log=...`). Consumers — notably the
# `meshtastic-mcp-test-tui` TUI — tail the reportlog for live per-test state.
# Appending here means power-user invocations like `./run-tests.sh tests/mesh`
# also produce it, not just the all-defaults invocation.
_has_report_log=0
for _arg in "$@"; do
	case "$_arg" in
	--report-log | --report-log=*) _has_report_log=1 ;;
	*) ;; # any other arg: no-op; loop continues
	esac
done
if [[ $_has_report_log -eq 0 ]]; then
	set -- "$@" --report-log=tests/reportlog.jsonl
fi

exec "$VENV_PY" -m pytest "$@"
