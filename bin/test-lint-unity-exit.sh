#!/usr/bin/env bash
# Self-test for bin/lint-unity-exit.sh.
#
# This exists because the scanner has been wrong twice in review, both times in a way that looked
# fine by inspection: layered regexes cannot tokenise C++, so a `/*` inside a string literal flipped
# comment state, a greedy `.*` swallowed code between two comments, `myexit(...)` matched the `exit`
# exemption as a substring, and `==` matched the assignment exemption. Every one of those is pinned
# below as a fixture, so the next rewrite has to keep them all passing.
#
# Each fixture is a snippet of C++ plus the exact diagnostics it must produce, as a comma-separated
# list of <line>:<col> - empty for none. Asserting the locations rather than just "did it say
# anything" is what catches a rule that reports the right number of findings in the wrong places, or
# that collapses two findings on one line into one.
#
# Not a Unity suite and not counted in test/native-suite-count - same arrangement as
# bin/test-state-check.sh, and for the same reason: it asserts the behaviour of a process.
#
# Usage: ./bin/test-lint-unity-exit.sh   (exit 0 = all fixtures behaved)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

WORK="$(mktemp -d -t meshlintunity.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/test/probe"

PASSES=0
FAILURES=0

# expect "<line>:<col>[,<line>:<col>...]" <label> <body>   ("" = no diagnostics expected)
expect() {
	local want="$1" label="$2" body="$3"
	local f="$WORK/test/probe/case.cpp"
	printf '%s\n' "$body" >"$f"

	# Reduce each diagnostic to line:col. The message text is asserted once, separately, so a
	# reworded message does not churn every fixture here.
	local got
	got="$(cd "$WORK" && "$SCRIPT_DIR/lint-unity-exit.sh" test/probe/case.cpp |
		awk -F: '{printf "%s%s:%s", (NR > 1 ? "," : ""), $2, $3}')"

	if [[ $got == "$want" ]]; then
		echo "  PASS  $label - [${want:-none}]"
		PASSES=$((PASSES + 1))
	else
		echo "  FAIL  $label - expected [${want:-none}], got [${got:-none}]"
		FAILURES=$((FAILURES + 1))
	fi
}

echo "Terminating forms (must NOT be reported):"
expect "" "exit(UNITY_END()) on one line" 'void s() { UNITY_BEGIN(); exit(UNITY_END()); }'
expect "" "exit( and the macro on separate lines" 'void s() {
    exit(
        UNITY_END());
}'
expect "" "capture-then-exit" 'void s() { const int rc = UNITY_END(); restore(); exit(rc); }'

echo
echo "Bare calls (MUST be reported):"
expect "1:27" "plain bare call" 'void s() { UNITY_BEGIN(); UNITY_END(); }'
expect "2:27" "bare call in an #else branch" '#else
void s() { UNITY_BEGIN(); UNITY_END(); }
#endif'
# `return` finalises the report and returns a count; it does not terminate the runner, and there is
# no main() under test/ from which it would.
expect "1:19" "return UNITY_END() does not terminate" 'void s() { return UNITY_END(); }'
# `exit` must be a whole identifier, not a suffix of some other function.
expect "1:19" "myexit(UNITY_END()) is not exit()" 'void s() { myexit(UNITY_END()); }'
# The assignment exemption is for capture; comparison and compound assignment are not capture.
expect "1:21" "== is not an assignment" 'void s() { if (x == UNITY_END()) return; }'
expect "1:21" "+= is not an assignment" 'void s() { total += UNITY_END(); }'

echo
echo "Comments and literals (the two classes that broke it before):"
expect "" "line comment mentioning the macro" 'void s() { exit(UNITY_END()); } // call UNITY_END() at the end'
expect "" "block comment interior mentioning the macro" '/* a comment
   that mentions UNITY_END()
   across lines */
void s() { exit(UNITY_END()); }'
expect "" "the macro inside a string literal" 'void s() { TEST_MESSAGE("call UNITY_END() when done"); exit(UNITY_END()); }'
expect "1:34" "a string containing /* must not open a comment" 'void s() { const char *p = "/*"; UNITY_END(); }'
expect "1:24" "code between two block comments on one line" 'void s() { /* first */ UNITY_END(); /* second */ }'
expect "1:36" "escaped quote inside a string does not end it" 'void s() { const char *p = "a\"b"; UNITY_END(); }'

echo
echo "Multiple occurrences on one line (count and caret must both be right):"
# The caret must land on the BARE call at column 31, not the wrapped one at 17.
expect "1:31" "one wrapped and one bare on the same line" 'void s() { exit(UNITY_END()); UNITY_END(); }'
expect "1:12,1:25" "two bare calls on one line report twice" 'void s() { UNITY_END(); UNITY_END(); }'

echo
if ((FAILURES > 0)); then
	echo "RESULT: RED lint-unity-exit self-test - $FAILURES of $((PASSES + FAILURES)) fixtures behaved unexpectedly"
	exit 1
fi
echo "RESULT: GREEN lint-unity-exit self-test - $PASSES/$PASSES fixtures behaved as specified"
exit 0
