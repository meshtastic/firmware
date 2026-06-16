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
#   RESULT: RED sanitizer fault — SUMMARY: AddressSanitizer: 1272 byte(s) leaked  (tests may have
#           all passed; the coverage build aborts at exit on an ASan/LSan fault — often shown only
#           as [ERRORED]/SIGHUP. The script names it and points at running the binary bare.)

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
MARKER=""
PROGRESS_PID=""
trap 'rm -f "$LOG" "${MARKER:-}"; [[ -n ${PROGRESS_PID:-} ]] && kill "$PROGRESS_PID" 2>/dev/null' EXIT

# Canonical suite set = the directories in test/. This is the source of truth for
# "what should run"; a filtered run only expects its filtered suite.
mapfile -t ALL_SUITES < <(find test -maxdepth 1 -type d -name 'test_*' -printf '%f\n' | sort)
EXPECTED_COUNT=${#ALL_SUITES[@]}

# Cached object-count for this env, written after each completed build (in the gitignored build
# dir). Used as the progress denominator: accurate for a full rebuild (every object recompiles),
# only a rough upper bound for an incremental run.
BASELINE_FILE=".pio/build/${ENV}/.runtests-objcount"

# Progress trail file (gitignored build dir). ALWAYS written so a backgrounded/piped run can be
# checked mid-build with `tail -f` — that's the whole point: don't fly blind on a 20-min rebuild.
PROGRESS_FILE=".pio/build/${ENV}/.runtests-progress"

# --- Progress heartbeat ------------------------------------------------------
# Emit ONE status line every few seconds: build = objects (re)compiled this run / cached total +
# best-effort ETA; test = suites finished / expected. Appends to $PROGRESS_FILE always (tail it to
# check on a backgrounded run); also live-updates the tty when $5=1 (interactive --quiet). Never
# touches $LOG, which is parsed for the verdict, so piped/CI captures stay clean.
progress_monitor() {
	local marker="$1" objtotal="$2" testtotal="$3" pfile="$4" totty="$5" start now el done ran eta line
	start=$(date +%s)
	while :; do
		now=$(date +%s)
		el=$((now - start))
		if grep -q 'Testing\.\.\.' "$LOG" 2>/dev/null; then
			ran=$(grep -cE "${ENV}:test_[a-z0-9_]+ \[(PASSED|FAILED|ERRORED)\]" "$LOG" 2>/dev/null)
			line=$(printf '[test] %s/%s suites done — %dm%02ds' "$ran" "$testtotal" $((el / 60)) $((el % 60)))
		else
			done=$(find ".pio/build/${ENV}" -name '*.o' -newer "$marker" 2>/dev/null | wc -l)
			if ((objtotal > 0 && done > 0)); then
				eta=$((objtotal > done ? (objtotal - done) * el / done : 0))
				line=$(printf '[build] %d/%d objs — %dm%02ds — ETA ~%dm%02ds' \
					"$done" "$objtotal" $((el / 60)) $((el % 60)) $((eta / 60)) $((eta % 60)))
			else
				# done==0 (incremental: nothing to rebuild yet) or no cached baseline — no ETA yet.
				line=$(printf '[build] %d objs compiled — %dm%02ds' "$done" $((el / 60)) $((el % 60)))
			fi
		fi
		printf '%s\n' "$line" >>"$pfile" 2>/dev/null                           # file trail (always)
		[[ $totty == 1 ]] && printf '\r\033[K%s' "$line" >/dev/tty 2>/dev/null # live line (human)
		sleep 4
	done
}

# Launch the heartbeat for every run. It writes the progress file unconditionally; the live tty
# line only when interactive AND --quiet (where pio's own output is hidden — otherwise pio's
# streamed compile lines already show progress and a \r line would just fight them).
mkdir -p ".pio/build/${ENV}" 2>/dev/null || true
: >"$PROGRESS_FILE" 2>/dev/null || true
MARKER="$(mktemp -t meshtest-mark.XXXXXX)"
TOTTY=0
{ $QUIET && [[ -t 1 ]]; } && TOTTY=1
progress_monitor "$MARKER" "$(cat "$BASELINE_FILE" 2>/dev/null || echo 0)" \
	"$([[ -n $FILTER ]] && echo 1 || echo "$EXPECTED_COUNT")" "$PROGRESS_FILE" "$TOTTY" &
PROGRESS_PID=$!

if ! $QUIET; then
	echo "Running: $PIO test -e $ENV ${PASSTHRU[*]-} (expecting $EXPECTED_COUNT suites)"
fi
echo "progress: tail -f $PROGRESS_FILE" >&2

# Run pio, tee to log. PIPESTATUS[0] is pio's real exit (NOT tee's).
if $QUIET; then
	"$PIO" test -e "$ENV" "${PASSTHRU[@]}" >"$LOG" 2>&1
else
	"$PIO" test -e "$ENV" "${PASSTHRU[@]}" 2>&1 | tee "$LOG"
fi
PIO_RC=${PIPESTATUS[0]}

# Stop the heartbeat, clear its line, and cache this build's object total for next time.
if [[ -n $PROGRESS_PID ]]; then
	kill "$PROGRESS_PID" 2>/dev/null
	wait "$PROGRESS_PID" 2>/dev/null
	PROGRESS_PID=""
	# Clear the live line only if we were writing one — opening /dev/tty when there is none is
	# itself a redirect-open error the trailing 2>/dev/null cannot suppress.
	[[ $TOTTY == 1 ]] && printf '\r\033[K' >/dev/tty 2>/dev/null
fi
[[ -d ".pio/build/${ENV}" ]] && find ".pio/build/${ENV}" -name '*.o' 2>/dev/null | wc -l >"$BASELINE_FILE" 2>/dev/null || true

# --- Outcome detection -------------------------------------------------------
# The SAME outcome is spelled differently depending on which layer emitted the line — this is
# the trap that produces false greens (grepping ":PASS" misses pio's "[PASSED]", grepping
# "[FAILED]" misses Unity's ":FAIL:"). So every regex below matches BOTH spellings:
#   pass:  Unity per-assertion ":PASS"   | pio per-suite "[PASSED]"      | summary "N succeeded"
#   fail:  Unity per-assertion ":FAIL:"  | pio per-suite "[FAILED]"      | summary "M failed"
#   error: pio build/crash "[ERRORED]"   | Unity "M Failures"            | compiler "error:"
# Match \b after :PASS/:FAIL so ":PASSED"/":FAILED" forms are also caught either way.
FAIL_RE=':FAIL\b|\[FAILED\]|\[ERRORED\]|[1-9][0-9]* failed|[0-9]+ Tests [1-9][0-9]* Failures|error:|undefined reference|Segmentation fault|terminate called|SIGHUP|SIGSEGV|SIGABRT'
# Positive proof tests actually ran & passed (absence != success). Accept any pass spelling:
# the per-test/per-suite tokens OR a success summary line.
PASS_RE=':PASS\b|\[PASSED\]|test cases: *[0-9]+ succeeded|[0-9]+ Tests 0 Failures'
# Sanitizer (ASan/LSan/UBSan/TSan) fault signatures. The coverage build is sanitizer-instrumented
# and aborts NON-ZERO at exit on a fault — most often a LeakSanitizer leak — AFTER every test has
# already printed [PASSED]. pio then reports [ERRORED]/SIGHUP with no :FAIL: anywhere, so it
# masquerades as a phantom "N-1 of N succeeded". See .notes/test-passfail-filter.md.
# Match only real FAULT lines, never the benign "AddressSanitizer: failed to intercept '...'"
# startup noise that prints on every sanitizer run (it'd mislabel a normal [FAILED] as a leak).
# Formats per LLVM/Google sanitizer docs: ASan/LSan emit "==PID==ERROR: <San>: ...", UBSan emits
# "file:line:col: runtime error: ...", TSan emits "WARNING: ThreadSanitizer: ..."; all close with
# a "SUMMARY: <San>: ..." line (LSan-under-ASan reports its SUMMARY as "AddressSanitizer").
SAN_RE='(ERROR|WARNING): (Address|Leak|Thread|UndefinedBehavior)Sanitizer:|SUMMARY: (Address|Leak|Thread|UndefinedBehavior)Sanitizer:|Direct leak of|Indirect leak of|detected memory leaks|heap-use-after-free|heap-buffer-overflow|stack-buffer-overflow|attempting double-free|LeakSanitizer has encountered a fatal error|runtime error:'

# Suites that produced a per-suite verdict. pio emits "coverage:test_x [PASSED|FAILED|ERRORED]";
# a SKIPPED suite (hardware-only on native) is "accounted for" too, so it doesn't read as missing.
mapfile -t RAN_SUITES < <(grep -oE "${ENV}:test_[a-z0-9_]+ \[(PASSED|FAILED|ERRORED)\]" "$LOG" |
	sed -E "s/^${ENV}:(test_[a-z0-9_]+) .*/\1/" | sort -u)
RAN_COUNT=${#RAN_SUITES[@]}
# Suites pio explicitly skipped (don't count these as "missing" in the canonical cross-check).
mapfile -t SKIPPED_SUITES < <(grep -oE "${ENV}:test_[a-z0-9_]+.*\bSKIPPED\b" "$LOG" |
	grep -oE "test_[a-z0-9_]+" | sort -u)

verdict_red() {
	local detail bin
	detail="$(grep -nE '\[FAILED\]|:FAIL:|\[ERRORED\]' "$LOG" | head -3 | sed 's/^/    /')"
	echo ""
	echo "RED — failures detected:"
	[[ -n $detail ]] && echo "$detail"
	grep -E 'test cases:' "$LOG" | tail -1 | sed 's/^/    /'

	# Path to the test binary for the "run it bare" hint. For native/coverage the test program is
	# the env executable (e.g. .pio/build/coverage/meshtasticd), NOT a file named 'program'.
	bin="$(find ".pio/build/${ENV}" -maxdepth 1 -type f -executable ! -name '*.so' 2>/dev/null | head -1)"
	[[ -z $bin ]] && bin=".pio/build/${ENV}/<program>  (build it first: $PIO test -e ${ENV} ${FILTER:+-f $FILTER} --without-testing)"

	# Sanitizer fault (ASan/LSan/UBSan/TSan): name the real cause instead of "build/crash error".
	if grep -qE "$SAN_RE" "$LOG"; then
		grep -nE "$SAN_RE" "$LOG" | head -4 | sed 's/^/    /'
		echo "    -> sanitizer fault: if every test above is PASS, this is an exit-time abort, not a failed assertion."
		echo "    -> read the full report by running the binary BARE (gdb hides it via ptrace): ./$bin 2>&1 | tail -40"
		echo "RESULT: RED sanitizer fault — $(grep -ohE 'SUMMARY: [A-Za-z]+Sanitizer:.*' "$LOG" | tail -1 || echo 'see report above')"
		exit 1
	fi

	# All tests passed but the process still aborted at EXIT (ERRORED/SIGHUP/SIGABRT) and the
	# sanitizer report was swallowed by the runner (often surfaced only as SIGHUP). Almost always a
	# sanitizer fault — point at how to surface it rather than calling it a generic crash.
	if grep -qE "$PASS_RE" "$LOG" && grep -qE '\[ERRORED\]|SIGHUP|SIGABRT' "$LOG" && ! grep -qE ':FAIL\b|\[FAILED\]' "$LOG"; then
		echo "    -> all tests passed but the process aborted at EXIT — likely an ASan/LSan fault whose report"
		echo "       the runner swallowed (commonly shown as SIGHUP). Run the binary BARE to see it: ./$bin 2>&1 | tail -40"
		echo "RESULT: RED exit-time abort (tests passed; likely sanitizer — see hint above)"
		exit 1
	fi

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

# AMBER: everything that ran passed, but (full run only) a canonical suite neither ran NOR was
# explicitly skipped — i.e. it silently went missing. SKIPPED suites are accounted for.
ACCOUNTED_COUNT=$((RAN_COUNT + ${#SKIPPED_SUITES[@]}))
if [[ -z $FILTER && $ACCOUNTED_COUNT -lt $EXPECTED_COUNT ]]; then
	missing=()
	for s in "${ALL_SUITES[@]}"; do
		printf '%s\n' "${RAN_SUITES[@]}" "${SKIPPED_SUITES[@]}" | grep -qx "$s" || missing+=("$s")
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
