#!/usr/bin/env bash
# lint-unity-exit.sh - flag a UNITY_END() that is not wrapped in exit().
#
# A bare UNITY_END() ends the *reporting*, not the suite: setup() returns, the runtime goes on
# calling loop(), and the process runs forever. PlatformIO does not notice - it reads the Unity
# summary off stdout, reports the suite PASSED and moves on - so the run is green while the binary
# is still resident. The costs are invisible by construction: the per-suite sandbox is deleted
# under a live process, and .gcda coverage plus LeakSanitizer's report are both flushed by atexit
# handlers, so a suite that never exits contributes no coverage and gets no leak check.
#
# The rule is per occurrence, not per file. test/test_serial/SerialModule.cpp had a correct
# exit(UNITY_END()) in its ESP32 branch and bare ones in both #else branches; a "does this file
# call exit() anywhere" check passes it. The empty branch of a feature or architecture guard is
# the easiest one to get wrong, because it looks like there is nothing to clean up.
#
# Emitted at "note" - trunk's only non-blocking level - because the enforcing half of this pair is
# bin/pio-test-isolate.sh, which detects an actual survivor at run time and grades it AMBER. This
# is the author-time advice that stops it being written in the first place.
#
# Emits one line per finding in the format
#   <path>:<line>:<col>:<severity>:<message>:<code>
# which trunk parses via parse_regex. Always exits 0; findings go to stdout.

set -uo pipefail

for target in "$@"; do
	[[ -f $target ]] || continue

	# Path is reported relative to the workspace so findings are clickable from the repo root.
	rel="${target#"$PWD"/}"

	# Only test sources declare a suite's lifecycle. Unity's own headers and any production file
	# mentioning the macro are none of this rule's business.
	[[ $rel == test/* ]] || continue

	awk -v path="$rel" '
	# Comments are prose, not code. Without this the rule fires on the very sentence explaining
	# it - the comment above a correct exit(UNITY_END()) naturally names the bare form.
	function strip_comments(s,   t) {
		t = s
		gsub(/\/\*.*\*\//, "", t) # single-line block comment
		sub(/\/\/.*/, "", t)
		sub(/\/\*.*/, "", t) # opening of a multi-line block comment
		return t
	}

	# True when the line still contains UNITY_END after every terminating form has been removed:
	#
	#   exit(UNITY_END())      the documented one
	#   return UNITY_END()     equally final, but only from main() - it does not compile in a void
	#                          setup(), so allowing it cannot mask a leak
	#   int rc = UNITY_END()   capture-then-exit, used by test_packet_signing to restore globals
	#                          between the summary and the exit
	#
	# The last of those is where the rule gives ground: capturing the value and then never exiting
	# would leak and is not flagged. That is a rarer mistake than the bare call, and flagging a
	# correct idiom would push someone to "fix" working code.
	function bare_unity_end(s,   t) {
		t = strip_comments(s)
		gsub(/exit[ \t]*\([ \t]*UNITY_END[ \t]*\([ \t]*\)[ \t]*\)/, "", t)
		gsub(/return[ \t]+UNITY_END[ \t]*\([ \t]*\)/, "", t)
		gsub(/=[ \t]*UNITY_END[ \t]*\([ \t]*\)/, "", t)
		return (t ~ /UNITY_END[ \t]*\(/)
	}

	bare_unity_end($0) {
		printf "%s:%d:%d:%s:%s:%s\n", path, NR, index($0, "UNITY_END"), "note",
		       "bare UNITY_END() leaves the process running - use exit(UNITY_END()) (see test/README.md)",
		       "unity-exit"
	}
	' "$target"
done

exit 0
