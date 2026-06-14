#!/usr/bin/env bash
# Run native PlatformIO unit tests and emit a single, unambiguous RED/AMBER/GREEN verdict.
#
# Why this exists: PlatformIO reports failures three different ways ([FAILED], :FAIL:,
# [ERRORED]) and an all-pass run prints "N succeeded" with NO "0 failed" clause — so naive
# greps produce false greens (see .notes/test-passfail-filter.md). This script encodes the
# correct logic once, and cross-checks the number of suites that actually ran against the
# canonical set in test/ so a suite silently going missing shows up as AMBER, not green.
#
# Usage:
#   ./bin/run-tests.sh                      # run all suites, full RAG + count cross-check
#   ./bin/run-tests.sh -f test_utf8         # run one suite (no count cross-check)
#   ./bin/run-tests.sh -e native            # override env (default: coverage)
#   ./bin/run-tests.sh --quiet              # only print the final RESULT line
#
# Exit codes: 0 = GREEN, 1 = RED, 2 = AMBER.
#
# The final line is machine-readable, e.g.:
#   RESULT: GREEN 19/19 suites passed
#   RESULT: AMBER 17/19 suites ran (missing: test_radio test_serial) — all that ran passed
#   RESULT: RED test_traffic_management: 1 failed  (or: build/crash error)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

ENV="coverage"
FILTER=""
QUIET=false
PASSTHRU=()

while [[ $# -gt 0 ]]; do
	case "$1" in
	-f)
		FILTER="$2"
		PASSTHRU+=("-f" "$2")
		shift 2
		;;
	-e)
		ENV="$2"
		shift 2
		;;
	--quiet)
		QUIET=true
		shift
		;;
	*)
		PASSTHRU+=("$1")
		shift
		;;
	esac
done

# Locate pio (PATH, then the standard PlatformIO venv).
PIO="$(command -v pio || command -v platformio || echo "$HOME/.platformio/penv/bin/pio")"
if [[ ! -x $PIO ]] && ! command -v "$PIO" >/dev/null 2>&1; then
	echo "RESULT: RED pio not found (looked in PATH and ~/.platformio/penv/bin)"
	exit 1
fi

LOG="$(mktemp -t meshtest.XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

# Canonical suite set = the directories in test/. This is the source of truth for
# "what should run"; a filtered run only expects its filtered suite.
mapfile -t ALL_SUITES < <(find test -maxdepth 1 -type d -name 'test_*' -printf '%f\n' | sort)
EXPECTED_COUNT=${#ALL_SUITES[@]}

if ! $QUIET; then
	echo "Running: $PIO test -e $ENV ${PASSTHRU[*]-} (expecting $EXPECTED_COUNT suites)"
fi

# Run pio, tee to log. PIPESTATUS[0] is pio's real exit (NOT tee's).
if $QUIET; then
	"$PIO" test -e "$ENV" "${PASSTHRU[@]}" >"$LOG" 2>&1
else
	"$PIO" test -e "$ENV" "${PASSTHRU[@]}" 2>&1 | tee "$LOG"
fi
PIO_RC=${PIPESTATUS[0]}

# --- Outcome detection -------------------------------------------------------
# Failure markers across all three reporting formats + build/crash signatures.
FAIL_RE='\[FAILED\]|\[ERRORED\]|:FAIL:|[1-9][0-9]* failed|[0-9]+ Tests [1-9][0-9]* Failures|error:|undefined reference|Segmentation fault|terminate called|SIGHUP|SIGSEGV|SIGABRT'
# A positive success summary must be present to call anything green (absence != success).
PASS_RE='test cases: *[0-9]+ succeeded|[0-9]+ Tests 0 Failures'

# Suites that actually produced a verdict line: "coverage:test_x [PASSED|FAILED|ERRORED]".
mapfile -t RAN_SUITES < <(grep -oE "${ENV}:test_[a-z0-9_]+ \[(PASSED|FAILED|ERRORED)\]" "$LOG" |
	sed -E "s/^${ENV}:(test_[a-z0-9_]+) .*/\1/" | sort -u)
RAN_COUNT=${#RAN_SUITES[@]}

verdict_red() {
	local detail
	detail="$(grep -nE '\[FAILED\]|:FAIL:|\[ERRORED\]' "$LOG" | head -3 | sed 's/^/    /')"
	echo ""
	echo "RED — failures detected:"
	[[ -n $detail ]] && echo "$detail"
	grep -E 'test cases:' "$LOG" | tail -1 | sed 's/^/    /'
	echo "RESULT: RED $(grep -oE '[0-9]+ failed' "$LOG" | tail -1 || echo 'build/crash error')"
	exit 1
}

# RED: pio non-zero, any failure marker, or no positive summary at all (build died early).
if [[ $PIO_RC -ne 0 ]] || grep -qE "$FAIL_RE" "$LOG"; then
	verdict_red
fi
if ! grep -qE "$PASS_RE" "$LOG"; then
	echo ""
	echo "RESULT: RED no success summary found (build error / no tests ran?) — see log"
	exit 1
fi

# AMBER: everything that ran passed, but (full run only) fewer suites ran than exist in test/.
if [[ -z $FILTER && $RAN_COUNT -lt $EXPECTED_COUNT ]]; then
	missing=()
	for s in "${ALL_SUITES[@]}"; do
		printf '%s\n' "${RAN_SUITES[@]}" | grep -qx "$s" || missing+=("$s")
	done
	echo ""
	echo "RESULT: AMBER ${RAN_COUNT}/${EXPECTED_COUNT} suites ran (missing: ${missing[*]}) — all that ran passed"
	exit 2
fi

# GREEN.
if [[ -n $FILTER ]]; then
	echo "RESULT: GREEN ${RAN_COUNT} suite(s) passed (filtered: $FILTER)"
else
	echo "RESULT: GREEN ${RAN_COUNT}/${EXPECTED_COUNT} suites passed"
fi
exit 0
