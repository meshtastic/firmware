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
# bin/test-lint-unity-exit.sh is this rule's self-test. It exists because the scanner has now been
# wrong twice: every false positive and false negative found in review is pinned there as a
# fixture, so the next rewrite has to keep them all passing.
#
# Not handled: raw string literals (R"(...)"). There are none under test/, and delimiter tracking
# for a case that does not occur would be untested code guarding untested code.
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
	# Return the line with comments and string/char literals removed, carrying /* ... */ state
	# across lines. A character-level scan, not layered regexes: regexes cannot tokenise C++ and
	# each attempt was wrong differently - a /* inside a string literal flipped comment state and
	# hid real calls, a greedy .* swallowed the code between two comments on one line, and
	# UNITY_END() inside a string read as code. Literals collapse to a space rather than vanishing,
	# so a token cannot be glued to its neighbour.
	# Also fills colmap[], mapping each position in the returned string back to its column in the
	# raw line. Without it a caret cannot be placed: removing a comment or collapsing a literal
	# shifts every later column, and counting occurrences in the raw line does not help either -
	# TEST_MESSAGE("... UNITY_END() ..."); UNITY_END(); has two in the raw text and one in the code.
	function strip_noncode(s,   out, i, n, c, two, q) {
		n = length(s); i = 1; out = ""
		delete colmap
		while (i <= n) {
			if (in_block) {
				if (substr(s, i, 2) == "*/") { in_block = 0; i += 2 } else { i++ }
				continue
			}
			two = substr(s, i, 2)
			if (two == "//") return out	# rest of the line is a comment
			if (two == "/*") { in_block = 1; i += 2; continue }
			c = substr(s, i, 1)
			if (c == "\"" || c == "'"'"'") {	# skip a whole literal, honouring backslash escapes
				q = c
				out = out " "; colmap[length(out)] = i
				i++
				while (i <= n) {
					c = substr(s, i, 1)
					if (c == "\\") { i += 2; continue }
					i++
					if (c == q) break
				}
				continue
			}
			out = out c; colmap[length(out)] = i; i++
		}
		return out
	}

	# Is this one occurrence wrapped in a form that terminates the process? Two count:
	#
	#   exit(UNITY_END())      the documented one
	#   int rc = UNITY_END()   capture-then-exit, used by test_packet_signing to restore globals
	#                          between the summary and the exit
	#
	# Judged per occurrence by looking back through whitespace, not by stripping forms out of the
	# whole statement. A line carrying both - exit(UNITY_END()); UNITY_END(); - must report the bare
	# call at the bare column, rather than once at whichever came first.
	#
	# Both forms are token-bounded. `exit` must be a whole identifier, so myexit(UNITY_END()) is
	# still reported; the assignment must be a plain `=`, so `==`, `!=`, `<=`, `>=` and `+=` are not
	# mistaken for a capture. `return UNITY_END()` is deliberately NOT accepted - it only terminates
	# from main(), there is no main() under test/, and from a helper it just returns a count.
	#
	# Where the rule gives ground: capturing the value and then never exiting would leak and is not
	# flagged. That is rarer than the bare call, and flagging a correct idiom would push someone to
	# "fix" working code.
	function is_wrapped(s, at,   j, c, tail) {
		j = at - 1
		while (j >= 1 && substr(s, j, 1) ~ /[ \t]/) j--	# skip space before the macro
		if (j < 1) return 0

		# exit ( UNITY_END - `exit` must be a whole identifier, so myexit( does not qualify
		if (substr(s, j, 1) == "(") {
			j--
			while (j >= 1 && substr(s, j, 1) ~ /[ \t]/) j--
			if (j >= 4 && substr(s, j - 3, 4) == "exit" &&
			    (j - 4 < 1 || substr(s, j - 4, 1) !~ /[A-Za-z0-9_]/)) return 1
			return 0
		}

		# = UNITY_END - a plain assignment is capture-then-exit; ==, !=, <=, >=, += are not
		if (substr(s, j, 1) == "=") {
			c = (j - 1 >= 1) ? substr(s, j - 1, 1) : " "
			tail = (j + 1 <= length(s)) ? substr(s, j + 1, 1) : " "
			if (c ~ /[-+*\/%&|^!<>=]/ || tail == "=") return 0
			return 1
		}

		return 0
	}

	BEGIN { LINE_CAP = 12 }	# give up accumulating a statement after this many lines

	{
		code = strip_noncode($0)

		# Record every occurrence on this line with the position it has in the accumulated
		# statement, plus its real line and column, so each can be judged and reported separately.
		if (stmt == "") { start = NR; nhits = 0 }
		base = length(stmt) + 1	# the leading space added below shifts everything by one
		stmt = stmt " " code

		off = 0
		while ((p = index(substr(code, off + 1), "UNITY_END")) > 0) {
			off += p
			nhits++
			hit_at[nhits] = base + off	# index within stmt
			hit_line[nhits] = NR
			hit_col[nhits] = colmap[off]
		}

		# End of statement. The cap is the backstop for a semicolon we never see, so one unclosed
		# call cannot swallow every later finding in the file.
		if (code ~ /;/ || NR - start >= LINE_CAP) {
			for (k = 1; k <= nhits; k++)
				if (!is_wrapped(stmt, hit_at[k]))
					printf "%s:%d:%d:%s:%s:%s\n", path, hit_line[k], hit_col[k], "note",
					       "bare UNITY_END() leaves the process running - use exit(UNITY_END()) (see test/README.md)",
					       "unity-exit"
			stmt = ""
			nhits = 0
		}
	}
	' "$target"
done

exit 0
