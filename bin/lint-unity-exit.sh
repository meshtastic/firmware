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
# Statement-aware, like bin/lint-node-id-format.sh and for the same reason: judging one physical
# line at a time reports `exit(\n    UNITY_END());` as bare, and reports the interior lines of a
# /* ... */ block comment that happens to mention the macro. A note-level rule that cries wolf
# gets ignored, and then the real finding goes with it.
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
	# Comment stripper carrying /* ... */ state across lines. Comments are prose: without this the
	# rule fires on the very sentence explaining it, and on any block comment naming the macro.
	function strip_comments(s,   t, i) {
		t = s
		if (in_block) {
			i = index(t, "*/")
			if (i == 0) return ""	# whole line is inside a block comment
			t = substr(t, i + 2)
			in_block = 0
		}
		while (match(t, /\/\*.*\*\//)) # complete /* ... */ pairs on one line
			t = substr(t, 1, RSTART - 1) substr(t, RSTART + RLENGTH)
		sub(/\/\/.*/, "", t)
		i = index(t, "/*")	# an opener with no closer: rest of line, and following lines, are comment
		if (i > 0) {
			t = substr(t, 1, i - 1)
			in_block = 1
		}
		return t
	}

	# True when the statement still contains UNITY_END after every terminating form is removed:
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
		t = s
		gsub(/exit[ \t]*\([ \t]*UNITY_END[ \t]*\([ \t]*\)[ \t]*\)/, "", t)
		gsub(/return[ \t]+UNITY_END[ \t]*\([ \t]*\)/, "", t)
		gsub(/=[ \t]*UNITY_END[ \t]*\([ \t]*\)/, "", t)
		return (t ~ /UNITY_END[ \t]*\(/)
	}

	BEGIN { LINE_CAP = 12 }	# give up accumulating a statement after this many lines

	{
		code = strip_comments($0)

		# Remember where the macro first appeared in this statement, for a useful caret position.
		if (!hit_line && code ~ /UNITY_END[ \t]*\(/) {
			hit_line = NR
			hit_col = index($0, "UNITY_END")
		}

		if (stmt == "") start = NR
		stmt = stmt " " code

		# End of statement. The cap is the backstop for a semicolon we never see, so one unclosed
		# call cannot swallow every later finding in the file.
		if (code ~ /;/ || NR - start >= LINE_CAP) {
			if (hit_line && bare_unity_end(stmt))
				printf "%s:%d:%d:%s:%s:%s\n", path, hit_line, (hit_col ? hit_col : 1), "note",
				       "bare UNITY_END() leaves the process running - use exit(UNITY_END()) (see test/README.md)",
				       "unity-exit"
			stmt = ""
			hit_line = 0
		}
	}
	' "$target"
done

exit 0
