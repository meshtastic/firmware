#!/usr/bin/env bash
# PlatformIO `test_testing_command` wrapper - runs one native test suite in its own scratch $HOME
# and reports what it left behind. Registered per-env in variants/native/portduino/platformio.ini,
# so it applies to a bare `pio test` and to CI, not only to bin/run-tests.sh.
#
# Every native suite that constructs a NodeDB loads and saves ~/.portduino/default/prefs/, and
# nothing cleared it between suites, so state leaked suite -> suite within a run and run -> run
# after it. Per-*run* isolation is not enough: the leak is generated within a single run, so the
# boundary has to be per suite.
#
# Contract: run "$@" unchanged, exit with its exit code. PlatformIO's own pass/fail is untouched -
# everything else here is reporting.
#
# Escape hatch: MESHTASTIC_TEST_NO_ISOLATION=1 runs the binary bare, for when you need the real
# $HOME (e.g. reproducing against a live prefs directory).

set -uo pipefail

if [[ ${MESHTASTIC_TEST_NO_ISOLATION:-0} == 1 ]]; then
	exec "$@"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=bin/lib/test-state.sh
source "$SCRIPT_DIR/lib/test-state.sh"

STATE_ROOT="${MESHTASTIC_TEST_STATE_DIR:-$ROOT_DIR/.pio/test-state}"
MANIFEST="${MESHTASTIC_TEST_STATE_MANIFEST:-$ROOT_DIR/$STATE_MANIFEST_DEFAULT}"
SUMMARY="${MESHTASTIC_TEST_STATE_SUMMARY:-$STATE_ROOT/summary.tsv}"

if ! mkdir -p "$STATE_ROOT" 2>/dev/null; then
	echo "pio-test-isolate: cannot create $STATE_ROOT - running without isolation" >&2
	exec "$@"
fi

SCRATCH="$(mktemp -d "$STATE_ROOT/suite.XXXXXX")" || exec "$@"
SUITE_HOME="$SCRATCH/home"
LOG="$SCRATCH/output.log"
REPORT="$SCRATCH/per-test.tsv"
mkdir -p "$SUITE_HOME"

state_assert_empty "$SUITE_HOME" || exit 1

BEFORE="$SCRATCH/before.fp"
state_fingerprint "$SUITE_HOME" >"$BEFORE"

# HOME points at the sandbox; PLATFORMIO_CORE_DIR is pinned to the real one so nothing re-downloads
# a toolchain into a directory we are about to delete. (Overriding HOME around `pio` itself is what
# breaks its own ~/.platformio/penv/bin/pio lookup - doing it here, around the already-built binary,
# sidesteps that entirely.)
REAL_HOME="$HOME"
HOME="$SUITE_HOME" \
	PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$REAL_HOME/.platformio}" \
	MESHTASTIC_TEST_STATE_REPORT="$REPORT" \
	"$@" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}

# Survivors, before anything else looks at the sandbox: reap them first so the after-fingerprint is
# taken against a tree nobody is still writing to, and so a run cannot leave processes accumulating
# on the host. SIGTERM, then SIGKILL for anything that ignores it. Reported on the summary line as a
# fourth outcome - it is not a filesystem verdict, and folding it into DIRTY would lose the reason.
SURVIVORS="$(state_find_survivors "$SUITE_HOME" | tr '\n' ' ')"
SURVIVORS="${SURVIVORS% }"
if [[ -n $SURVIVORS ]]; then
	# shellcheck disable=SC2086 # deliberate word splitting: SURVIVORS is a PID list
	kill $SURVIVORS 2>/dev/null
	sleep 0.2
	STILL="$(state_find_survivors "$SUITE_HOME" | tr '\n' ' ')"
	# shellcheck disable=SC2086 # as above
	[[ -n ${STILL// /} ]] && kill -9 $STILL 2>/dev/null
	echo "pio-test-isolate: survivor(s) still running after the suite finished: $SURVIVORS (killed)" >&2
fi

# The suite name is not passed to a test_testing_command, so recover it from the output: every Unity
# result line carries the suite's source path. Fall back to the per-test report, which records it
# from __FILE__, and finally to the scratch dir name.
SUITE="$(grep -oE 'test/test_[a-z0-9_]+/' "$LOG" 2>/dev/null | head -1 | sed -E 's#test/(test_[a-z0-9_]+)/#\1#')"
if [[ -z $SUITE && -f $REPORT ]]; then
	SUITE="$(awk -F'\t' 'NR==1 {print $1}' "$REPORT")"
fi
[[ -z $SUITE ]] && SUITE="unknown-$(basename "$SCRATCH")"

AFTER="$SCRATCH/after.fp"
state_fingerprint "$SUITE_HOME" >"$AFTER"
CHANGED="$(state_changed_paths "$BEFORE" "$AFTER")"

FLAGS="$(state_manifest_flags "$SUITE" "$MANIFEST")"
DECLARED="$(state_flag_value writes "$FLAGS")"
GRANULARITY="$(state_flag_value state "$FLAGS")"
[[ -z $GRANULARITY ]] && GRANULARITY="per-test"

IFS=$'\t' read -r VERDICT DETAIL <<<"$(state_classify "$CHANGED" "$DECLARED")"

# Per-test attribution, when the suite has not declared that it carries state across its own test
# cases. For a state=per-suite suite every test after the first would be flagged by design - that
# carry *is* the declared behaviour - so only the suite boundary is meaningful there.
PER_TEST_DETAIL=""
if [[ $GRANULARITY == "per-test" && -f $REPORT ]]; then
	awk -F'\t' -v d="$DECLARED" '
		BEGIN { n = split(d, a, ","); }
		{
			path = $4; base = path; sub(/^.*\//, "", base);
			for (i = 1; i <= n; i++) if (a[i] == path || a[i] == base) next;
			print $2 " -> " base;
		}' "$REPORT" | LC_ALL=C sort -u >"$SCRATCH/per-test-undeclared.txt"
	# Keep the summary line readable; the full attribution stays in the sandbox's per-test.tsv.
	PER_TEST_COUNT=$(wc -l <"$SCRATCH/per-test-undeclared.txt")
	# Not `paste -sd'; '`: with -s, paste cycles through a multi-char delimiter one character per
	# join, so five paths render as "a;b c;d e" rather than "a; b; c; d; e".
	PER_TEST_DETAIL="$(head -5 "$SCRATCH/per-test-undeclared.txt" | awk '{printf "%s%s", (NR > 1 ? "; " : ""), $0} END {print ""}')"
	((PER_TEST_COUNT > 5)) && PER_TEST_DETAIL="$PER_TEST_DETAIL; +$((PER_TEST_COUNT - 5)) more"
fi

STATUS=$([[ $RC -eq 0 ]] && echo PASS || echo FAIL)
mkdir -p "$(dirname "$SUMMARY")" 2>/dev/null
printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$SUITE" "$STATUS" "$VERDICT" "${DETAIL-}" "${PER_TEST_DETAIL-}" \
	"${SURVIVORS-}" >>"$SUMMARY"

# Keep the sandbox when there is something to look at: on a failure it plus the built binary is a
# complete, replayable reproduction, and on a DIRTY verdict the leftovers *are* the bug report. A
# clean pass leaves nothing behind.
KEEP="${MESHTASTIC_TEST_KEEP_STATE:-0}"
if [[ $RC -ne 0 || $VERDICT != CLEAN || -n ${SURVIVORS-} || $KEEP == 1 ]]; then
	DEST="$STATE_ROOT/$SUITE"
	rm -rf "$DEST" 2>/dev/null
	mv "$SCRATCH" "$DEST" 2>/dev/null || DEST="$SCRATCH"
	echo "pio-test-isolate: $SUITE $STATUS/$VERDICT - state and log kept at $DEST" >&2
else
	rm -rf "$SCRATCH"
fi

exit "$RC"
