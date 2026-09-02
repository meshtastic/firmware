#!/usr/bin/env bash
# Canary for bin/check-test-attribution.py: reproduce the false green on purpose and require the
# checker to catch it.
#
# The attribution check exists because both harnesses once ran every suite against whichever binary
# was linked last, so all 57 reported a pass while five test programs actually executed. A checker
# for that is only worth having if it still fires, and a checker that has quietly stopped firing
# looks exactly like a codebase with no problem. So: build two suites, run them the broken way
# (--without-building, which is what stops PlatformIO relinking on a non-embedded platform), and
# assert the checker reports a mismatch.
#
# It also fails if the reproduction stops reproducing - if PlatformIO ever relinks per suite under
# --without-building, the premise behind dropping that flag no longer holds and the harness should
# be revisited rather than left resting on a stale assumption.
#
# Not a Unity suite and not a test_* directory, so it stays outside the suite count run-tests.sh
# derives from test/ - same arrangement as bin/test-state-check.sh and bin/test-config-check.sh.
#
# Usage: ./bin/test-attribution-canary.sh [-e <env>]      (default: coverage, as CI runs)

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO" || exit 2

ENV_NAME=coverage
[[ ${1-} == "-e" ]] && ENV_NAME="$2"

PIO="$REPO/.pio_env/bin/pio"
[[ -x $PIO ]] || PIO="$(command -v pio)" || {
	echo "canary: pio not found" >&2
	exit 2
}

# Two suites whose cases cannot be confused: different source files, different counts. Both are
# small and neither touches shared state, so the canary costs a link rather than a rebuild.
A=test_utf8
B=test_breakout
REPORT="$(mktemp -d)/canary.xml"

echo "canary: building $A and $B for $ENV_NAME"
"$PIO" test -e "$ENV_NAME" -f "$A" -f "$B" --without-testing >/dev/null 2>&1 || {
	echo "canary: build failed" >&2
	exit 2
}

echo "canary: running them the broken way (--without-building)"
"$PIO" test -e "$ENV_NAME" -f "$A" -f "$B" --without-building --junit-output-path "$REPORT" >/dev/null 2>&1

[[ -s $REPORT ]] || {
	echo "canary: no JUnit report at $REPORT - cannot judge the checker" >&2
	exit 2
}

# The checker must FAIL here, and fail for the RIGHT reason. Exit 1 is a finding; exit 2 is bad
# usage or an unreadable report, which would let a broken canary read as a caught mismatch.
OUT="$(./bin/check-test-attribution.py --label "canary" "$REPORT" 2>&1)"
RC=$?
if [[ $RC -eq 2 ]]; then
	echo ""
	echo "CANARY INCONCLUSIVE: the checker could not read the report it was given (exit 2)."
	echo "$OUT"
	echo "Report kept at: $REPORT"
	exit 2
fi
if [[ $RC -eq 0 ]] || ! grep -q 'MISATTRIBUTED' <<<"$OUT"; then
	echo ""
	echo "CANARY FAILED: the attribution check passed a run that mis-attributes its cases."
	echo ""
	echo "Two suites were run with --without-building, so PlatformIO did not relink and both"
	echo "executed the same leftover binary. check-test-attribution.py is supposed to catch exactly"
	echo "that and it did not, which means the guard against the whole false-green class is dead."
	echo ""
	echo "Either the checker regressed, or PlatformIO now relinks per suite under --without-building"
	echo "- in which case the reason bin/run-tests.sh and CI stopped passing that flag has changed,"
	echo "and the harness should be revisited rather than left on a stale assumption."
	echo "Report kept at: $REPORT"
	exit 1
fi

echo "canary: OK - the attribution check caught the deliberate mis-attribution"
rm -rf "$(dirname "$REPORT")"
