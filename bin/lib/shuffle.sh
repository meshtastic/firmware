# shellcheck shell=bash
#
# The seeded shuffle shared by bin/run-tests.sh and .github/workflows/test_native.yml. Sourced by
# both so there is exactly one implementation; nothing here executes on its own.
#
# This has to live in one place. The workflow prints "replay locally: ./bin/run-tests.sh --shuffle
# --seed $seed" after a CI shuffle, and that instruction is only true while CI and the local script
# produce the same permutation for a seed. Two copies of the algorithm cannot be relied on to stay
# byte-identical, and the way they'd announce their drift is a replay that quietly reproduces a
# different order than the one that failed.

# Deterministic Fisher-Yates over a MINSTD generator rather than awk's rand(), whose sequence
# differs between gawk and mawk - a seed that does not reproduce the same order on another machine
# is not a seed.
shuffle_suites() {
	local seed="$1"
	shift
	# Nothing in, nothing out. `printf '%s\n'` with no arguments still writes one empty line, and the
	# callers read this through mapfile - so an empty suite list would arrive as a suite named "".
	(($#)) || return 0
	printf '%s\n' "$@" | awk -v seed="$seed" '
		function rnd() { s = (s * 16807) % 2147483647; return s / 2147483647 }
		BEGIN { s = seed % 2147483647; if (s <= 0) s += 2147483646 }
		{ a[NR] = $0 }
		END {
			for (i = NR; i > 1; i--) { j = int(rnd() * i) + 1; t = a[i]; a[i] = a[j]; a[j] = t }
			for (i = 1; i <= NR; i++) print a[i]
		}'
}
