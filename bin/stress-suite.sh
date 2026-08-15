#!/usr/bin/env bash
#
# Run one native test suite repeatedly and report how often it fails.
#
# For order-independent flakes - a real-time race, a slow-host margin, an uninitialised read - a
# single green run proves nothing. This runs the same built binary N times and prints a flake rate,
# so "passes here" becomes a measurement instead of an anecdote.
#
#   ./bin/stress-suite.sh test_pki_admin_fallback              # 20 runs, coverage, as CI invokes it
#   ./bin/stress-suite.sh -n 200 test_packet_signing           # 200 runs
#   ./bin/stress-suite.sh -e native -n 50 test_admin_radio     # the other env's invocation
#   ./bin/stress-suite.sh -l 8 -n 50 test_pki_admin_fallback   # 8 spinners of CPU contention
#   ./bin/stress-suite.sh --no-simradio -n 50 test_packet_signing
#
# Each run gets a fresh scratch $HOME, so no run inherits another's prefs. Failing runs keep their
# log and their $HOME; passing runs leave nothing behind.
#
# Exit: 0 = every run passed, 1 = at least one failed, 2 = usage/build error.

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME=coverage
RUNS=20
LOAD=0
SIMRADIO=auto
SUITE=""

usage() {
	sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 2
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	-e | --environment)
		ENV_NAME="$2"
		shift 2
		;;
	-n | --runs)
		RUNS="$2"
		shift 2
		;;
	-l | --load)
		LOAD="$2"
		shift 2
		;;
	--simradio)
		SIMRADIO=yes
		shift
		;;
	--no-simradio)
		SIMRADIO=no
		shift
		;;
	-h | --help) usage ;;
	-*)
		echo "unknown option: $1" >&2
		usage
		;;
	*)
		SUITE="$1"
		shift
		;;
	esac
done

[[ -n $SUITE ]] || usage

# Mirror what the env's test_testing_command passes, so a stress run reproduces the real invocation
# rather than a third one of its own. [env:coverage] adds -s (simradio); [env:native] does not.
if [[ $SIMRADIO == auto ]]; then
	# Read to the next [section] header, not a fixed window: -s is the last line of the command block.
	if awk "/^\\[env:$ENV_NAME\\]/{f=1;next} /^\\[/{f=0} f" \
		"$REPO/variants/native/portduino/platformio.ini" | grep -qE '^[[:space:]]+-s[[:space:]]*$'; then
		SIMRADIO=yes
	else
		SIMRADIO=no
	fi
fi
ARGS=()
[[ $SIMRADIO == yes ]] && ARGS+=(-s)

PIO="$REPO/.pio_env/bin/pio"
[[ -x $PIO ]] || PIO="$(command -v pio)" || {
	echo "pio not found" >&2
	exit 2
}

BIN="$REPO/.pio/build/$ENV_NAME/meshtasticd"
echo "building $SUITE for $ENV_NAME ..."
"$PIO" test -e "$ENV_NAME" -f "$SUITE" --without-testing >/dev/null 2>&1 || {
	echo "build failed - rerun without --without-testing to see why" >&2
	exit 2
}
[[ -x $BIN ]] || {
	echo "no binary at $BIN" >&2
	exit 2
}

LOADPIDS=()
cleanup() {
	[[ ${#LOADPIDS[@]} -gt 0 ]] && kill "${LOADPIDS[@]}" 2>/dev/null
	return 0
}
trap cleanup EXIT INT TERM

if [[ $LOAD -gt 0 ]]; then
	echo "starting $LOAD spinner(s) against $(nproc) cpu(s)"
	for ((i = 0; i < LOAD; i++)); do
		(while :; do :; done) &
		LOADPIDS+=($!)
	done
fi

OUT="$REPO/.pio/build/$ENV_NAME/stress"
mkdir -p "$OUT"
fails=0
echo "running $SUITE x$RUNS on $ENV_NAME (simradio=$SIMRADIO)"
for ((run = 1; run <= RUNS; run++)); do
	scratch=$(mktemp -d)
	log="$OUT/$SUITE.$run.log"
	if HOME="$scratch" "$BIN" "${ARGS[@]}" >"$log" 2>&1; then
		rm -rf "$scratch" "$log"
		printf '.'
	else
		fails=$((fails + 1))
		printf '\nRUN %d FAILED - log %s - state %s\n' "$run" "$log" "$scratch"
		grep -E ':(FAIL|IGNORE)' "$log" | head -5
	fi
done
printf '\n'

pct=$((fails * 100 / RUNS))
echo "RESULT: $fails/$RUNS failed (${pct}%)"
[[ $fails -eq 0 ]] || exit 1
