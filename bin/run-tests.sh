#!/usr/bin/env bash
# Run native PlatformIO unit tests and emit a single, unambiguous verdict.
#
# Why this exists: PlatformIO reports failures three different ways ([FAILED], :FAIL:,
# [ERRORED]) and an all-pass run prints "N succeeded" with NO "0 failed" clause - so naive
# greps produce false greens (see .notes/test-passfail-filter.md). This script encodes the
# correct logic once, and cross-checks the number of suites that actually ran against the
# canonical set in test/ so a suite silently going missing shows up as AMBER, not green.
#
# Usage:
#   ./bin/run-tests.sh                      # run all suites, full verdict + count cross-check
#   ./bin/run-tests.sh -f test_utf8         # run one suite (yields FILTERED, not GREEN)
#   ./bin/run-tests.sh -e native            # override env (default: coverage)
#   ./bin/run-tests.sh --quiet              # only print the final RESULT line
#   ./bin/run-tests.sh --write-manifest     # print the test/state-manifest.tsv entries this run
#                                           # would need, for a human to paste and justify
#   ./bin/run-tests.sh --keep-state         # keep every suite's sandbox, not just the interesting ones
#   ./bin/run-tests.sh --shuffle            # randomise suite order (seed from HEAD; printed)
#   ./bin/run-tests.sh --seed 12345         # replay an exact order (implies --shuffle)
#   ./bin/run-tests.sh --status             # is a run in progress? if not, what did the last one say?
#   ./bin/run-tests.sh --wait               # attach to the run in progress; exit with its verdict
#   ./bin/run-tests.sh --abort              # stop the run in progress (whole build tree), keep its log
#   bin\run-tests.cmd <same args>          # from a Windows shell: forwards into WSL, exit code passed through
#
# Exit codes: 0 = GREEN, 1 = RED, 2 = AMBER, 3 = FILTERED, 4 = BUSY (a run is already in
# progress - never start a second one), 5 = ABORTED (signal or --abort), 6 = UNSUPPORTED host
# (nothing ran; Linux only - WSL and ./bin/test-native-docker.sh are the routes from elsewhere).
#
# FOR A CALLER THAT CANNOT SEE THE TERMINAL (a tool call, a backgrounded job, a fresh session):
#   - The verdict is the final "RESULT:" line and nothing else. pio prints "[PASSED]" per suite and
#     "N succeeded" per invocation long before the verdict exists; a captured output file looks
#     green within the first minute. Use --quiet, which prints only the RESULT line.
#   - One run at a time. A second invocation while one is in progress prints RESULT: BUSY and exits
#     4 without touching the build directory. Ask with --status; block with --wait; never pgrep.
#   - The last verdict is on disk: .pio/runtests/last-result.tsv (--status prints it when idle),
#     with the run's log kept beside it. An interrupted run records RESULT: ABORTED, not nothing.
#
# HOST. This is a Linux tool: bash 4+ (mapfile), GNU coreutils and GNU find (`-printf`, md5sum,
# `-executable`). That is a deliberate choice, not an oversight - the alternative is a second,
# untested code path per host, and a state check that silently degrades is worse than one that does
# not run. It is enforced below rather than left to be discovered. macOS and Windows are supported as
# *build* targets by CI, not as hosts for this harness; run it in a container there, via
# ./bin/test-native-docker.sh.
#
# -f IS NOT A GATE. A filtered run can pass while a full run fails: filtering removes the suites
# that create the shared state a later suite trips over. Use -f to iterate; gate on a full run.
#
# Verdicts:
#   GREEN    - all canonical suites ran, all passed, no ignored test cases, no undeclared leftovers.
#   AMBER    - all that ran passed, but something was lost or unexplained: a suite silently went
#              missing on a full run, individual test cases were skipped (Unity TEST_IGNORE /
#              :IGNORE:), or a suite left behind shared state it does not declare in
#              test/state-manifest.tsv.
#   FILTERED - a -f run completed cleanly; suites not in the filter were intentionally skipped.
#              Use this when iterating on a single suite; it is not a quality signal.
#   RED      - at least one failure, build error, sanitizer fault, or a suite that reported
#              another suite's test cases (bin/check-test-attribution.py).
#
# Two orthogonal axes: PASS/FAIL × CLEAN/DIRTY. Each suite runs in its own scratch $HOME
# (bin/pio-test-isolate.sh), so leftovers are harmless; DIRTY means "undeclared", not "dangerous".
#
# ORDER. PlatformIO chooses suite order itself - list_test_names() walks test/ with os.walk() and
# filters only *select*, they do not order - so --shuffle runs one `pio test -f <suite>` invocation
# per suite in the chosen order. That costs about 4.7s per suite in extra pio startup. The seed is
# printed on every shuffled run and derived from HEAD by default: deterministic for a given commit,
# varied across commits, so a red is reproducible and attributable rather than flaky. A single green
# seed is not evidence of order independence; vary it.
#
# Sanitizers, per env - this trips people up: `coverage` (the default here) has ASan/LSan;
# `native` has NONE. Verified: zero ASan symbols in the native binary. `-e native` runs are not
# sanitized, whatever the coverage wording elsewhere implies.
#
# The final line is machine-readable, e.g.:
#   RESULT: GREEN N/N suites passed
#   RESULT: AMBER N/M suites ran (missing: test_radio test_serial) - all that ran passed
#   RESULT: AMBER 3 test case(s) ignored
#   RESULT: FILTERED 1/N suites ran (N-1 not run) - filtered: test_utf8
#   RESULT: RED test attribution failed - suites did not run their own tests
#   RESULT: RED test_traffic_management: 1 failed  (or: build/crash error)
#   RESULT: RED sanitizer fault - SUMMARY: AddressSanitizer: 1272 byte(s) leaked  (tests may have
#           all passed; the coverage build aborts at exit on an ASan/LSan fault - often shown only
#           as [ERRORED]/SIGHUP. The script names it and points at running the binary bare.)

set -uo pipefail

# Refuse to start off Linux rather than fail somewhere in the middle. This harness is a Linux tool by
# choice (see the HOST note in the header); on a BSD userland it would not fail cleanly, it would
# mis-hash the sandbox, mis-read a suite list and report a verdict that looks real.
if [[ $(uname -s) != Linux ]]; then
	echo "run-tests.sh is Linux-only (bash 4+, GNU coreutils, GNU find); this host is $(uname -s)." >&2
	echo "Run it under WSL, or in a container: ./bin/test-native-docker.sh" >&2
	# Its own code, not AMBER: nothing ran, and a caller must not read this as "passed with caveats".
	echo "RESULT: UNSUPPORTED host $(uname -s) - nothing ran; use WSL or ./bin/test-native-docker.sh"
	exit 6
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

ENV="coverage"
FILTER=""
QUIET=false
WRITE_MANIFEST=false
KEEP_STATE=false
SHUFFLE=false
SEED=""
PASSTHRU=()
# Same passthrough args minus the -f pair. The shuffled loop supplies its own -f per suite, but
# must still forward everything else the user gave (-v, -vvv, ...) - otherwise a shuffled run
# builds with those flags and then runs without them.
EXTRA_ARGS=()
ORIG_ARGS=("$@")
SUBCMD=""

while [[ $# -gt 0 ]]; do
	case "$1" in
	--status | --wait | --abort)
		SUBCMD="${1#--}"
		shift
		;;
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
	--write-manifest)
		WRITE_MANIFEST=true
		shift
		;;
	--keep-state)
		KEEP_STATE=true
		shift
		;;
	--shuffle)
		SHUFFLE=true
		shift
		;;
	--seed)
		SEED="$2"
		SHUFFLE=true
		shift 2
		;;
	*)
		PASSTHRU+=("$1")
		EXTRA_ARGS+=("$1")
		shift
		;;
	esac
done

# --- Run record ----------------------------------------------------------------
# One run at a time, and the last verdict on disk. Two things a caller that cannot see the terminal
# needs: a way to ask "is something running?" that is not `pgrep -f` (which matches the asker), and
# a way to read the last verdict that is not scrollback. Both live in .pio/runtests/.
#
# current.tsv exists for exactly the life of a run: written before anything touches .pio/, removed
# by result(). A lock alone would not do - a SIGKILLed wrapper releases flock while its scons tree
# lives on - so a record is valid while EITHER its holder pid OR its recorded process group is
# alive; the second case is ORPHANED, and only --abort (or the tree finishing) clears it.
RUN_DIR="$ROOT_DIR/.pio/runtests"
RUN_RECORD="$RUN_DIR/current.tsv"
LAST_RESULT="$RUN_DIR/last-result.tsv"
KEPT_LOG="$RUN_DIR/last.log"
mkdir -p "$RUN_DIR"
LOG=""
BUILD_LOG=""
# The verdict prints to the stdout this script STARTED with. A signal can arrive while a pio call
# has stdout redirected into a log file, and the trap runs inside that redirect.
exec 3>&1
RUN_START=$(date +%s)
HEAD_SHA=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)

# What the verdict was FOR. HEAD alone is not enough - a verdict from before the current edits is
# the most convincing stale green there is - so the fingerprint covers the working tree too:
# tracked changes by content, untracked files by content. Recorded with the verdict; --status
# recomputes and says whether the last verdict still describes this tree.
tree_fingerprint() {
	{
		git rev-parse HEAD 2>/dev/null
		git diff HEAD 2>/dev/null
		git ls-files --others --exclude-standard -z 2>/dev/null | xargs -0 -r md5sum 2>/dev/null
	} | md5sum | cut -c1-12
}
TREE_FP=$(tree_fingerprint)

# A build that fails leaves the previous binary in place, and run bare it reprints the last good
# run - a green that predates the failure. Remove it; the next build regenerates it.
drop_stale_program() {
	local prog=".pio/build/${ENV}/meshtasticd"
	if [[ -f $prog ]]; then
		rm -f "$prog"
		echo "    removed $prog: it predates this failure and run bare would reprint the last good run"
	fi
}
ARGS_STR="${ORIG_ARGS[*]-}"

tsv_get() { awk -F'\t' -v k="$2" '$1 == k { print $2; exit }' "$1" 2>/dev/null; }
pid_alive() { [[ -n ${1-} ]] && kill -0 "$1" 2>/dev/null; }
pgid_alive() { [[ -n ${1-} ]] && ps -eo pgid= 2>/dev/null | tr -d ' ' | grep -qx "$1"; }
elapsed_since() {
	local s=$(($(date +%s) - ${1:-0}))
	printf '%dm%02ds' $((s / 60)) $((s % 60))
}

run_state() {
	[[ -f $RUN_RECORD ]] || {
		echo IDLE
		return
	}
	if pid_alive "$(tsv_get "$RUN_RECORD" pid)"; then
		echo RUNNING
	elif pgid_alive "$(tsv_get "$RUN_RECORD" pgid)"; then
		echo ORPHANED
	else
		# Holder and tree both gone with the record still here: the wrapper was killed before
		# result() ran. Nothing to report for it beyond the kept log; clear the way.
		rm -f "$RUN_RECORD"
		echo IDLE
	fi
}

# The one place a verdict is printed. Records it, keeps the run log beside it, drops the run
# record, exits. Every RESULT: line in this script goes through here so --status can never
# disagree with what the caller saw.
result() {
	local code=$1 text=$2 src kept="-"
	echo "RESULT: $text" >&3
	# Keep whichever log has content: the test log, or during the build phase the build log.
	for src in "$LOG" "$BUILD_LOG"; do
		if [[ -n $src && -s $src ]] && cp "$src" "$KEPT_LOG" 2>/dev/null; then
			kept="$KEPT_LOG"
			break
		fi
	done
	printf 'result\tRESULT: %s\ncode\t%s\nhead\t%s\ntree\t%s\nargs\t%s\nenv\t%s\nfinished\t%s\nlog\t%s\n' \
		"$text" "$code" "$HEAD_SHA" "$TREE_FP" "$ARGS_STR" "$ENV" "$(date +%s)" "$kept" >"$LAST_RESULT"
	rm -f "$RUN_RECORD"
	exit "$code"
}

status_cmd() {
	local st
	st=$(run_state)
	case $st in
	RUNNING | ORPHANED)
		local progress
		progress=$(tail -n1 "$(tsv_get "$RUN_RECORD" progress)" 2>/dev/null)
		echo "STATUS: $st pid=$(tsv_get "$RUN_RECORD" pid) pgid=$(tsv_get "$RUN_RECORD" pgid) since=$(elapsed_since "$(tsv_get "$RUN_RECORD" started)") head=$(tsv_get "$RUN_RECORD" head) args=\"$(tsv_get "$RUN_RECORD" args)\""
		echo "    progress: ${progress:-none yet}  (tail -f $(tsv_get "$RUN_RECORD" progress))"
		if [[ $st == ORPHANED ]]; then
			echo "    the wrapper died but its build tree is still running: ./bin/run-tests.sh --abort to stop it, or --wait for it to finish (no verdict will be recorded)"
		else
			echo "    ./bin/run-tests.sh --wait blocks until the verdict; --abort stops it"
		fi
		;;
	IDLE)
		if [[ -f $LAST_RESULT ]]; then
			local fresh="current"
			[[ $(tsv_get "$LAST_RESULT" tree) == "$(tree_fingerprint)" ]] || fresh="STALE - the tree has changed since; this verdict is not about the current code"
			echo "STATUS: IDLE - last run ($fresh): $(tsv_get "$LAST_RESULT" result)"
			echo "    exit $(tsv_get "$LAST_RESULT" code), head $(tsv_get "$LAST_RESULT" head), env $(tsv_get "$LAST_RESULT" env), args \"$(tsv_get "$LAST_RESULT" args)\", finished $(elapsed_since "$(tsv_get "$LAST_RESULT" finished)") ago"
			echo "    log: $(tsv_get "$LAST_RESULT" log)"
		else
			echo "STATUS: IDLE - no run recorded"
		fi
		;;
	esac
}

wait_cmd() {
	local st
	st=$(run_state)
	[[ $st == IDLE ]] && {
		status_cmd
		[[ -f $LAST_RESULT ]] && exit "$(tsv_get "$LAST_RESULT" code)"
		exit 0
	}
	echo "waiting for $st run pid=$(tsv_get "$RUN_RECORD" pid) pgid=$(tsv_get "$RUN_RECORD" pgid)..." >&2
	while [[ $(run_state) != IDLE ]]; do sleep 5; done
	if [[ $st == ORPHANED ]]; then
		echo "RESULT: ABORTED orphaned build tree finished; its wrapper was gone, so no verdict was recorded (log: $KEPT_LOG)"
		exit 5
	fi
	echo "$(tsv_get "$LAST_RESULT" result)"
	exit "$(tsv_get "$LAST_RESULT" code)"
}

abort_cmd() {
	local st pid pgid
	st=$(run_state)
	[[ $st == IDLE ]] && {
		echo "nothing to abort"
		status_cmd
		exit 0
	}
	pid=$(tsv_get "$RUN_RECORD" pid)
	pgid=$(tsv_get "$RUN_RECORD" pgid)
	if [[ $st == RUNNING ]]; then
		# The holder's own trap kills the tree and records ABORTED; give it a moment to do so.
		kill -TERM "$pid" 2>/dev/null
		for _ in 1 2 3 4 5 6 7 8 9 10; do
			[[ $(run_state) == IDLE ]] && break
			sleep 1
		done
	fi
	if [[ $(run_state) != IDLE ]]; then
		[[ -n $pgid ]] && kill -TERM -- "-$pgid" 2>/dev/null
		sleep 2
		[[ -n $pgid ]] && kill -KILL -- "-$pgid" 2>/dev/null
		printf 'result\tRESULT: ABORTED by --abort (wrapper pid %s was already gone)\ncode\t5\nhead\t%s\nargs\t%s\nenv\t%s\nfinished\t%s\nlog\t%s\n' \
			"$pid" "$(tsv_get "$RUN_RECORD" head)" "$(tsv_get "$RUN_RECORD" args)" "$(tsv_get "$RUN_RECORD" env)" "$(date +%s)" "$KEPT_LOG" >"$LAST_RESULT"
		rm -f "$RUN_RECORD"
	fi
	status_cmd
	exit 0
}

case $SUBCMD in
status)
	status_cmd
	exit 0
	;;
wait) wait_cmd ;;
abort) abort_cmd ;;
esac

# A run is a run: refuse while one is in progress, whatever env it is for. Two pio jobs share
# .pio/build/ and libdeps and wipe each other's objects, and the state summary below is per run.
case $(run_state) in
RUNNING | ORPHANED)
	status_cmd
	echo "RESULT: BUSY a run is already in progress - never start a second one (--status, --wait, --abort)"
	exit 4
	;;
esac
PROGRESS_FILE=".pio/build/${ENV}/.runtests-progress"
printf 'pid\t%s\npgid\t\nstarted\t%s\nargs\t%s\nenv\t%s\nhead\t%s\nprogress\t%s\n' \
	"$$" "$RUN_START" "$ARGS_STR" "$ENV" "$HEAD_SHA" "$PROGRESS_FILE" >"$RUN_RECORD"

# Every pio invocation goes through here: its own process group (setsid), pgid recorded in the run
# record so a signal to this script - or --abort from another session - takes the whole scons tree
# with it. Called inside pipelines, hence the record file rather than a shell variable.
run_pio() {
	setsid "$PIO" "$@" &
	local pid=$!
	sed -i "s/^pgid\t.*/pgid\t$pid/" "$RUN_RECORD"
	wait "$pid"
}

abort_run() {
	local pgid
	pgid=$(tsv_get "$RUN_RECORD" pgid)
	if [[ -n $pgid ]]; then
		kill -TERM -- "-$pgid" 2>/dev/null
		sleep 2
		kill -KILL -- "-$pgid" 2>/dev/null
	fi
	result 5 "ABORTED after $(elapsed_since "$RUN_START") ($1) - ./bin/run-tests.sh --status for the kept log"
}
trap 'abort_run SIGINT' INT
trap 'abort_run SIGTERM' TERM
trap 'abort_run SIGHUP' HUP

# Locate pio (PATH, then the standard PlatformIO venv).
PIO="$(command -v pio || command -v platformio || echo "$HOME/.platformio/penv/bin/pio")"
if [[ ! -x $PIO ]] && ! command -v "$PIO" >/dev/null 2>&1; then
	result 1 "RED pio not found (looked in PATH and ~/.platformio/penv/bin)"
fi

LOG="$(mktemp -t meshtest.XXXXXX.log)"
# Build output stays out of $LOG on purpose: the outcome regexes below match "error:" and
# "[ERRORED]", so a compiler diagnostic in the same file would read as a test failure.
BUILD_LOG="$(mktemp -t meshtest-build.XXXXXX.log)"
MARKER=""
PROGRESS_PID=""
trap 'rm -f "$LOG" "$BUILD_LOG" "${MARKER:-}"; [[ -n ${PROGRESS_PID:-} ]] && kill "$PROGRESS_PID" 2>/dev/null' EXIT

# --- Shared-state reporting ---------------------------------------------------
# bin/pio-test-isolate.sh (wired in as test_testing_command) gives every suite its own scratch
# $HOME and appends one line per suite here: suite, PASS/FAIL, CLEAN/DIRTY/MISSING, detail. The
# wrapper enforces isolation on its own - a bare `pio test` gets it too - so all this section does
# is collect and grade. Start from an empty summary so a stale one cannot be read as this run's.
# shellcheck source=bin/lib/test-state.sh
source "$SCRIPT_DIR/lib/test-state.sh"
STATE_DIR="$ROOT_DIR/.pio/test-state"
STATE_SUMMARY="$STATE_DIR/summary.tsv"
rm -rf "$STATE_DIR"
mkdir -p "$STATE_DIR"
export MESHTASTIC_TEST_STATE_DIR="$STATE_DIR"
export MESHTASTIC_TEST_STATE_SUMMARY="$STATE_SUMMARY"
$KEEP_STATE && export MESHTASTIC_TEST_KEEP_STATE=1
$WRITE_MANIFEST && export MESHTASTIC_TEST_KEEP_STATE=1

# --- Test attribution --------------------------------------------------------
# PlatformIO parses Unity output textually and never checks that the source file a case came from
# belongs to the suite it thinks it ran, so one suite's binary running under another's name reads
# as a pass. The JUnit reports carry both halves (testsuite@name vs testcase@file), so collect them
# here and grade with bin/check-test-attribution.py below. Cleared first: a stale report from an
# earlier run would otherwise satisfy this run's expectations.
ATTRIB_DIR="$ROOT_DIR/.pio/test-attribution"
rm -rf "$ATTRIB_DIR"
mkdir -p "$ATTRIB_DIR"

# Canonical suite set = the directories in test/, detected on the fly. This is the sole source
# of truth for "what should run"; a filtered run only expects its filtered suite.
mapfile -t ALL_SUITES < <(find test -maxdepth 1 -type d -name 'test_*' -printf '%f\n' | sort)
EXPECTED_COUNT=${#ALL_SUITES[@]}

# Cached object-count for this env, written after each completed build (in the gitignored build
# dir). Used as the progress denominator: accurate for a full rebuild (every object recompiles),
# only a rough upper bound for an incremental run.
BASELINE_FILE=".pio/build/${ENV}/.runtests-objcount"

# Progress trail file (gitignored build dir). ALWAYS written so a backgrounded/piped run can be
# checked mid-build with `tail -f` - that's the whole point: don't fly blind on a 20-min rebuild.

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
			line=$(printf '[test] %s/%s suites done - %dm%02ds' "$ran" "$testtotal" $((el / 60)) $((el % 60)))
		else
			done=$(find ".pio/build/${ENV}" -name '*.o' -newer "$marker" 2>/dev/null | wc -l)
			if ((objtotal > 0 && done > 0)); then
				eta=$((objtotal > done ? (objtotal - done) * el / done : 0))
				line=$(printf '[build] %d/%d objs - %dm%02ds - ETA ~%dm%02ds' \
					"$done" "$objtotal" $((el / 60)) $((el % 60)) $((eta / 60)) $((eta % 60)))
			else
				# done==0 (incremental: nothing to rebuild yet) or no cached baseline - no ETA yet.
				line=$(printf '[build] %d objs compiled - %dm%02ds' "$done" $((el / 60)) $((el % 60)))
			fi
		fi
		printf '%s\n' "$line" >>"$pfile" 2>/dev/null                           # file trail (always)
		[[ $totty == 1 ]] && printf '\r\033[K%s' "$line" >/dev/tty 2>/dev/null # live line (human)
		sleep 4
	done
}

# Launch the heartbeat for every run. It writes the progress file unconditionally; the live tty
# line only when interactive AND --quiet (where pio's own output is hidden - otherwise pio's
# streamed compile lines already show progress and a \r line would just fight them).
mkdir -p ".pio/build/${ENV}" 2>/dev/null || true
# Clear last run's failure logs: a green run must not leave a red one's log lying around looking
# current.
rm -f ".pio/build/${ENV}/build-failure.log" ".pio/build/${ENV}/test-failure.log" 2>/dev/null || true
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
echo "RUN pending (pid $$): the verdict is the final RESULT: line only - [PASSED] and \"N succeeded\" lines before it are pio's, not a verdict. status: ./bin/run-tests.sh --status" >&2
if [[ ! -t 1 ]] && ! $QUIET; then
	echo "hint: stdout is a pipe - build errors appear at the top of output and may be lost; use --quiet to get just the RESULT line" >&2
fi

# shuffle_suites() lives in lib/ because the CI workflow runs the same permutation; see the header
# of that file for why a second copy cannot be allowed to exist.
# shellcheck source=bin/lib/shuffle.sh
source "$SCRIPT_DIR/lib/shuffle.sh"

RUN_ORDER=()
if $SHUFFLE; then
	# Seed from HEAD when not given: same order for a given commit (so a PR's red is replayable and
	# attributable to its diff), different orders as the project moves.
	if [[ -z $SEED ]]; then
		SEED=$((16#$(git rev-parse --short=8 HEAD 2>/dev/null || echo 0)))
	fi
	if [[ -n $FILTER ]]; then
		mapfile -t RUN_ORDER < <(shuffle_suites "$SEED" "$FILTER")
	else
		mapfile -t RUN_ORDER < <(shuffle_suites "$SEED" "${ALL_SUITES[@]}")
	fi
	echo "suite order: shuffled with --seed $SEED (${#RUN_ORDER[@]} suites)"
fi

# Warm the shared src objects before running any suite, the way .github/workflows/test_native.yml
# does. Fused build+run makes whichever suite PlatformIO's directory walk reaches first absorb the
# whole src compile and report it as its own duration - that is how a 35s suite once reported 13
# minutes, and it hides the build cost from every timing the summary prints.
#
# This is a WARM-UP ONLY: the run below must still build. PlatformIO links every test program to
# the one $BUILD_DIR/$PROGNAME path, so a `--without-building` run executes whichever suite was
# linked last - every suite, under its own name, all PASSED. The warm-up keeps the src compile out
# of the suite timings; the per-suite step is then just one test_main.cpp plus a link.
BUILD_SECS=0
build_started=$SECONDS
if $QUIET; then
	run_pio test -e "$ENV" "${PASSTHRU[@]}" --without-testing >"$BUILD_LOG" 2>&1
	BUILD_RC=$?
else
	run_pio test -e "$ENV" "${PASSTHRU[@]}" --without-testing 2>&1 | tee "$BUILD_LOG"
	BUILD_RC=${PIPESTATUS[0]}
fi
BUILD_SECS=$((SECONDS - build_started))
if ((BUILD_RC != 0)); then
	# The grep below shows the first few diagnostics; the first error: is usually a cascade from
	# something further up, so keep the whole log rather than only what fits on screen.
	BUILD_FAIL_LOG=".pio/build/${ENV}/build-failure.log"
	cp "$BUILD_LOG" "$BUILD_FAIL_LOG" 2>/dev/null || BUILD_FAIL_LOG=""
	echo ""
	echo "RED - build failed before any suite ran:"
	grep -nE 'error:|undefined reference|\[ERRORED\]' "$BUILD_LOG" | head -5 | sed 's/^/    /'
	[[ -n $BUILD_FAIL_LOG ]] && echo "    -> full build output: $BUILD_FAIL_LOG"
	drop_stale_program
	result 1 "RED build failed in ${BUILD_SECS}s (no suites ran)"
fi
if ! $QUIET; then
	echo "build: ${BUILD_SECS}s (shared by every suite; suite durations below exclude it)"
fi

# Run pio, tee to log. PIPESTATUS[0] is pio's real exit (NOT tee's).
PIO_RC=0
if $SHUFFLE; then
	# One invocation per suite: PlatformIO orders by its own directory walk, so this is the only way
	# to control it. Output is appended to the one $LOG the verdict logic already parses.
	: >"$LOG"
	for suite in "${RUN_ORDER[@]}"; do
		if $QUIET; then
			run_pio test -e "$ENV" -f "$suite" "${EXTRA_ARGS[@]}" \
				--junit-output-path "$ATTRIB_DIR/$suite.xml" >>"$LOG" 2>&1
			rc=$?
		else
			run_pio test -e "$ENV" -f "$suite" "${EXTRA_ARGS[@]}" \
				--junit-output-path "$ATTRIB_DIR/$suite.xml" 2>&1 | tee -a "$LOG"
			rc=${PIPESTATUS[0]}
		fi
		((rc != 0)) && PIO_RC=$rc
	done
elif $QUIET; then
	run_pio test -e "$ENV" "${PASSTHRU[@]}" \
		--junit-output-path "$ATTRIB_DIR/all.xml" >"$LOG" 2>&1
	PIO_RC=$?
else
	run_pio test -e "$ENV" "${PASSTHRU[@]}" \
		--junit-output-path "$ATTRIB_DIR/all.xml" 2>&1 | tee "$LOG"
	PIO_RC=${PIPESTATUS[0]}
fi

# Stop the heartbeat, clear its line, and cache this build's object total for next time.
if [[ -n $PROGRESS_PID ]]; then
	kill "$PROGRESS_PID" 2>/dev/null
	wait "$PROGRESS_PID" 2>/dev/null
	PROGRESS_PID=""
	# Clear the live line only if we were writing one - opening /dev/tty when there is none is
	# itself a redirect-open error the trailing 2>/dev/null cannot suppress.
	[[ $TOTTY == 1 ]] && printf '\r\033[K' >/dev/tty 2>/dev/null
fi
[[ -d ".pio/build/${ENV}" ]] && find ".pio/build/${ENV}" -name '*.o' 2>/dev/null | wc -l >"$BASELINE_FILE" 2>/dev/null || true

# --- Outcome detection -------------------------------------------------------
# The SAME outcome is spelled differently depending on which layer emitted the line - this is
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
# and aborts NON-ZERO at exit on a fault - most often a LeakSanitizer leak - AFTER every test has
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

# Keep the whole-run log, which the EXIT trap would otherwise delete. This is the cross-suite view
# - order, pio-level output, what ran before the failure; bin/pio-test-isolate.sh separately keeps
# the failing suite's own sandbox and log under .pio/test-state/<suite>/.
preserve_run_log() {
	local dest=".pio/build/${ENV}/test-failure.log"
	cp "$LOG" "$dest" 2>/dev/null && echo "    -> full run output: $dest"
}

# PlatformIO prints one "N test cases: ... succeeded in T" line per invocation. A shuffled run is one
# invocation per suite appending to the same $LOG, so taking the last line would report whatever the
# LAST suite did - a failure in suite 3 printed under suite 44's "0 failed". Sum the lines instead.
# One line in (the unshuffled case) is passed through verbatim, so the familiar output is unchanged.
summarise_test_cases() {
	# The patterns are strings, not /regex/ literals: awk evaluates a regex literal passed as a
	# function argument as `$0 ~ /re/`, so the callee would receive 0 or 1 rather than a pattern.
	awk '
		function num(s, pat,   m) {
			if (!match(s, pat)) return 0
			m = substr(s, RSTART, RLENGTH); gsub(/[^0-9]/, "", m); return m + 0
		}
		/test cases:/ {
			last = $0; n++
			cases   += num($0, "[0-9]+ test cases")
			failed  += num($0, "[0-9]+ failed")
			skipped += num($0, "[0-9]+ skipped")
			passed  += num($0, "[0-9]+ succeeded")
		}
		END {
			if (n == 0) exit
			if (n == 1) { print "    " last; exit }
			printf "    %d test cases: ", cases
			if (failed)  printf "%d failed, ", failed
			if (skipped) printf "%d skipped, ", skipped
			printf "%d succeeded, summed over %d suite invocations\n", passed, n
		}' "$1"
}

verdict_red() {
	local detail bin
	# The order IS the diagnostic for an order-dependent failure; without it a shuffled red is
	# unreadable.
	if $SHUFFLE; then
		echo ""
		echo "suite order (--seed $SEED):"
		printf '%s\n' "${RUN_ORDER[@]}" | nl -ba | sed 's/^/    /'
	fi
	detail="$(grep -nE '\[FAILED\]|:FAIL:|\[ERRORED\]' "$LOG" | head -3 | sed 's/^/    /')"
	echo ""
	echo "RED - failures detected:"
	[[ -n $detail ]] && echo "$detail"
	summarise_test_cases "$LOG"
	preserve_run_log

	# Path to the test binary for the "run it bare" hint. For native/coverage the test program is
	# the env executable (e.g. .pio/build/coverage/meshtasticd), NOT a file named 'program'.
	bin="$(find ".pio/build/${ENV}" -maxdepth 1 -type f -executable ! -name '*.so' 2>/dev/null | head -1)"
	[[ -z $bin ]] && bin=".pio/build/${ENV}/<program>  (build it first: $PIO test -e ${ENV} ${FILTER:+-f $FILTER} --without-testing)"

	# A signal name from this runner is almost never a crash. `exit(UNITY_END())` returns the
	# FAILURE COUNT, and PlatformIO's native runner renders a non-zero exit code as a POSIX signal:
	# 4 failures -> "Program received signal SIGILL", 5 -> SIGTRAP, and the suite is reported
	# [ERRORED] rather than [FAILED]. That is pure noise, and it cost hours of hunting a memory bug
	# that did not exist. Say so before anyone theorises.
	if grep -qE 'Program received signal SIG' "$LOG"; then
		echo "    -> the signal name above is Unity's exit code, not a crash: exit(UNITY_END()) returns the"
		echo "       failure count and the runner renders it as a signal number (4 -> SIGILL, 5 -> SIGTRAP)."
		echo "       Match it against the failure count before assuming a fault; confirm any real crash in gdb."
	fi

	# Sanitizer fault (ASan/LSan/UBSan/TSan): name the real cause instead of "build/crash error".
	if grep -qE "$SAN_RE" "$LOG"; then
		grep -nE "$SAN_RE" "$LOG" | head -4 | sed 's/^/    /'
		echo "    -> sanitizer fault: if every test above is PASS, this is an exit-time abort, not a failed assertion."
		echo "    -> read the full report by running the binary BARE (gdb hides it via ptrace): ./$bin 2>&1 | tail -40"
		result 1 "RED sanitizer fault - $(grep -ohE 'SUMMARY: [A-Za-z]+Sanitizer:.*' "$LOG" | tail -1 || echo 'see report above')"
	fi

	# A guard in test/TestUtil.cpp aborting on purpose - a listening socket, or force_simradio put
	# back. It prints FATAL on stdout precisely so this can be told apart from a fault: otherwise its
	# exit(EXIT_FAILURE) lands in the heuristic below and is reported as a sanitizer abort that never
	# happened, which is the same wrong-cause-in-the-verdict trap as the phantom signal above.
	if grep -qE '^FATAL: ' "$LOG"; then
		grep -E '^FATAL: ' "$LOG" | head -3 | sed 's/^/    /'
		echo "    -> a harness guard aborted the suite deliberately. Not a crash and not a sanitizer"
		echo "       fault; the reason is the FATAL line above, and the suite's sandbox has the full log."
		result 1 "RED harness guard - $(grep -m1 -oE '^FATAL: .*' "$LOG")"
	fi

	# All tests passed but the process still aborted at EXIT (ERRORED/SIGHUP/SIGABRT) and the
	# sanitizer report was swallowed by the runner (often surfaced only as SIGHUP). Almost always a
	# sanitizer fault - point at how to surface it rather than calling it a generic crash.
	if grep -qE "$PASS_RE" "$LOG" && grep -qE '\[ERRORED\]|SIGHUP|SIGABRT' "$LOG" && ! grep -qE ':FAIL\b|\[FAILED\]' "$LOG"; then
		echo "    -> all tests passed but the process aborted at EXIT - likely an ASan/LSan fault whose report"
		echo "       the runner swallowed (commonly shown as SIGHUP). Run the binary BARE to see it: ./$bin 2>&1 | tail -40"
		result 1 "RED exit-time abort (tests passed; likely sanitizer - see hint above)"
	fi

	# A per-suite compile error ([ERRORED]) leaves the previous suite's binary behind, same as a
	# failed warm-up build.
	grep -qE '\[ERRORED\]|error:|undefined reference' "$LOG" && drop_stale_program
	result 1 "RED $(grep -oE '[0-9]+ failed' "$LOG" | tail -1 || echo 'build/crash error')"
}

# RED: pio non-zero, any failure marker, or no positive summary at all (build died early).
if [[ $PIO_RC -ne 0 ]] || grep -qE "$FAIL_RE" "$LOG"; then
	verdict_red
fi
if ! grep -qE "$PASS_RE" "$LOG"; then
	echo ""
	# This path never runs verdict_red, and if the build died before any suite started there is no
	# per-suite sandbox either - so without preserving here, "see log" points at nothing.
	preserve_run_log
	drop_stale_program
	result 1 "RED no success summary found (build error / no tests ran?)"
fi

# Verdict-line suffix. The suite count itself is derived from the test_* directories on the fly
# (EXPECTED_COUNT above), so the only extra context a verdict needs is the shuffle seed - carried
# into the machine-readable line so a verdict is always replayable from it alone.
verdict_suffix() {
	local rating=""
	$SHUFFLE && rating="[seed: $SEED]"
	echo "$rating"
}

# --- Attribution axis ---------------------------------------------------------
# RED, and checked before every softer verdict: a suite that reported another suite's test cases
# did not run at all, so every count and state verdict below it is measuring the wrong thing. A
# filtered run expects only its own suite; a full run expects the canonical set.
# -f takes an fnmatch pattern, not necessarily a suite name, so resolve it against the canonical
# set rather than expecting a suite literally called "test_nodedb*". An unmatched pattern leaves
# the list empty, which checks attribution only - a filter that selects nothing is already RED
# above, for want of a pass summary.
ATTRIB_EXPECT="${ALL_SUITES[*]}"
if [[ -n $FILTER ]]; then
	ATTRIB_EXPECT=""
	for attrib_suite in "${ALL_SUITES[@]}"; do
		# shellcheck disable=SC2053 # deliberate glob match: FILTER is a pattern, not a literal
		[[ $attrib_suite == $FILTER ]] && ATTRIB_EXPECT+="$attrib_suite "
	done
fi
ATTRIB_OUT="$("$SCRIPT_DIR/check-test-attribution.py" --expect "$ATTRIB_EXPECT" \
	--label "$ENV" "$ATTRIB_DIR"/*.xml 2>&1)"
ATTRIB_RC=$?
if ((ATTRIB_RC != 0)); then
	echo ""
	echo "$ATTRIB_OUT" | sed 's/^/    /'
	preserve_run_log
	result 1 "RED test attribution failed - suites did not run their own tests $(verdict_suffix)"
fi
$QUIET || echo "$ATTRIB_OUT" | tail -1

# --- Shared-state axis --------------------------------------------------------
# Read what the per-suite wrapper recorded. Reported after the count checks so a structural problem
# still wins, and before the pass/fail verdict lines so the state summary always prints.
DIRTY_SUITES=()
MISSING_SUITES=()
SURVIVOR_SUITES=()
if [[ -f $STATE_SUMMARY ]]; then
	mapfile -t DIRTY_SUITES < <(awk -F'\t' '$3 == "DIRTY" { print $1 " (" $4 ")" }' "$STATE_SUMMARY")
	mapfile -t MISSING_SUITES < <(awk -F'\t' '$3 == "MISSING" { print $1 " (" $4 ")" }' "$STATE_SUMMARY")
	mapfile -t SURVIVOR_SUITES < <(awk -F'\t' '$6 != "" { print $1 " (pid " $6 ")" }' "$STATE_SUMMARY")
	mapfile -t ERROR_BUDGET_SUITES < <(awk -F'\t' '$7 == "OVER" || $7 == "UNDER" { print $1 " " tolower($7) " budget: " $8 }' "$STATE_SUMMARY")
fi

# Print the opt-out count on every run, so the number creeping upward is visible without anyone
# auditing test/state-manifest.tsv on purpose.
DECLARED_COUNT=0
if [[ -f $ROOT_DIR/$STATE_MANIFEST_DEFAULT ]]; then
	DECLARED_COUNT=$(grep -cvE '^[[:space:]]*(#|$)' "$ROOT_DIR/$STATE_MANIFEST_DEFAULT" || true)
fi
if ! $QUIET; then
	echo ""
	echo "shared state: $DECLARED_COUNT suite(s) declare non-default state handling (test/state-manifest.tsv)"
fi

# --write-manifest: propose, never apply. An auto-accepted baseline is the same rot as an
# auto-updated snapshot, so this prints lines for a human to paste AND justify - the reason column
# is the point, and only a person can write it.
if $WRITE_MANIFEST; then
	echo ""
	echo "Proposed test/state-manifest.tsv entries from this run (paste and replace <why>):"
	if [[ -f $STATE_SUMMARY ]]; then
		awk -F'\t' '$3 == "DIRTY" {
			detail = $4; sub(/^undeclared: /, "", detail);
			n = split(detail, paths, " "); out = "";
			for (i = 1; i <= n; i++) { base = paths[i]; sub(/^.*\//, "", base); out = out (i > 1 ? "," : "") base }
			printf "%s\twrites=%s\t<why>\n", $1, out
		}' "$STATE_SUMMARY" | sort -u | sed 's/^/  /'
	fi
	echo ""
	echo "  Sandboxes kept under $STATE_DIR/<suite>/ - the leftovers themselves are the evidence."
fi

# MISSING is a warning, never a verdict: a declared write that did not happen catches silently
# broken persistence (the upstream TAK config bug was a has_ flag never set, so the save wrote
# nothing and no test noticed), but some declared writes are legitimately conditional.
if ((${#MISSING_SUITES[@]} > 0)) && ! $QUIET; then
	echo ""
	echo "warning: declared writes that did not happen - check for silently broken persistence:"
	printf '    %s\n' "${MISSING_SUITES[@]}"
fi

# AMBER: individual test cases were skipped (Unity TEST_IGNORE → :IGNORE: in output).
# Applies to both full and filtered runs - a skipped test case is a lost signal either way.
mapfile -t IGNORED_TESTS < <(grep -oE '[^:]+:[0-9]+:[^:]+:IGNORE:.*' "$LOG" 2>/dev/null | sed 's/:IGNORE:.*//' | sort -u)
IGNORED_COUNT=${#IGNORED_TESTS[@]}
if [[ $IGNORED_COUNT -gt 0 ]]; then
	IGNORE_DETAIL="$(printf '%s\n' "${IGNORED_TESTS[@]}" | head -5 | sed 's/^/    /')"
	echo ""
	echo "$IGNORE_DETAIL"
	echo ""
	result 2 "AMBER ${IGNORED_COUNT} test case(s) ignored $(verdict_suffix)"
fi

# AMBER: full run only - a canonical suite neither ran NOR was explicitly skipped (silently missing).
ACCOUNTED_COUNT=$((RAN_COUNT + ${#SKIPPED_SUITES[@]}))
if [[ -z $FILTER && $ACCOUNTED_COUNT -lt $EXPECTED_COUNT ]]; then
	missing=()
	for s in "${ALL_SUITES[@]}"; do
		printf '%s\n' "${RAN_SUITES[@]}" "${SKIPPED_SUITES[@]}" | grep -qx "$s" || missing+=("$s")
	done
	echo ""
	result 2 "AMBER ${RAN_COUNT}/${EXPECTED_COUNT} suites ran (missing: ${missing[*]}) - all that ran passed $(verdict_suffix)"
fi

# AMBER: a suite mutated shared state it does not declare. Per-suite isolation means this is no
# longer dangerous - nothing survives the suite boundary - so it is graded AMBER rather than RED:
# it means "undeclared", not "broken". Applies to filtered runs too, because a suite writing state
# nobody declared is a finding whether or not its neighbours ran.
if ((${#DIRTY_SUITES[@]} > 0)); then
	echo ""
	printf '    %s\n' "${DIRTY_SUITES[@]}"
	echo ""
	echo "    -> declare these in test/state-manifest.tsv with a reason, or stop the write."
	echo "    -> ./bin/run-tests.sh --write-manifest prints the entries to paste."
	result 2 "AMBER ${#DIRTY_SUITES[@]} suite(s) left undeclared shared state $(verdict_suffix)"
fi

# AMBER: a suite spent its LOG_ERROR budget, or came in under a declared floor. Over budget buries a
# real failure in noise - three log sites account for nearly all of today's volume, and until those
# are demoted this stays AMBER rather than RED so it does not land red on day one and get switched
# off. Under a floor is the more interesting half: a fuzz suite that stops logging rejections has
# stopped feeding malformed input, and every one of its cases still passes.
if ((${#ERROR_BUDGET_SUITES[@]} > 0)); then
	echo ""
	printf '    %s\n' "${ERROR_BUDGET_SUITES[@]}"
	echo ""
	echo "    -> over: demote the log line if the condition is expected, or declare errors=<max> in"
	echo "       test/state-manifest.tsv with a reason. Under: check the suite still exercises the path."
	result 2 "AMBER ${#ERROR_BUDGET_SUITES[@]} suite(s) outside their error budget $(verdict_suffix)"
fi

# AMBER: a suite was still running after PlatformIO reported it. A bare UNITY_END() ends the
# reporting, not the process - the runtime goes on calling loop() - so the suite passes, the run goes
# green, and the binary stays resident. The wrapper has already killed it, but the consequences do
# not undo: its CLEAN/DIRTY verdict was measured against a tree it may still have been writing to,
# and .gcda plus LeakSanitizer both flush from atexit handlers that never ran, so the suite silently
# contributed no coverage and got no leak check. AMBER, not RED - the tests themselves did pass.
if ((${#SURVIVOR_SUITES[@]} > 0)); then
	echo ""
	printf '    %s\n' "${SURVIVOR_SUITES[@]}"
	echo ""
	echo "    -> end every setup() branch with exit(UNITY_END()), not a bare UNITY_END()."
	echo "    -> ./bin/lint-unity-exit.sh test/**/*.cpp finds the sites; see test/README.md."
	result 2 "AMBER ${#SURVIVOR_SUITES[@]} suite(s) still running after the suite finished $(verdict_suffix)"
fi

# FILTERED: a -f run completed cleanly. Suites outside the filter were intentionally not run;
# this is not a quality signal and is distinct from suites that went missing unexpectedly. The
# not-run set is by construction "everything outside the filter", so it is a count, not a list.
if [[ -n $FILTER ]]; then
	result 3 "FILTERED ${RAN_COUNT}/${EXPECTED_COUNT} suites ran ($((EXPECTED_COUNT - RAN_COUNT - ${#SKIPPED_SUITES[@]})) not run) - filtered: $FILTER $(verdict_suffix)"
fi

# GREEN: all canonical suites ran, all passed, no ignored test cases, nothing undeclared left behind.
result 0 "GREEN ${RAN_COUNT}/${EXPECTED_COUNT} suites passed, all CLEAN $(verdict_suffix)"
