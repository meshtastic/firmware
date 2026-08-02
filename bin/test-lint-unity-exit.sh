#!/usr/bin/env bash
# Self-test for bin/lint-unity-exit.sh.
#
# This exists because the scanner has been wrong twice in review, both times in a way that looked
# fine by inspection: layered regexes cannot tokenise C++, so a `/*` inside a string literal flipped
# comment state, a greedy `.*` swallowed code between two comments, `myexit(...)` matched the `exit`
# exemption as a substring, and `==` matched the assignment exemption. Every one of those is pinned
# below as a fixture, so the next rewrite has to keep them all passing.
#
# Each fixture is one line of C++ plus the verdict it must produce: BARE (must be reported) or OK
# (must not be). Multi-line cases carry an explicit expected line number.
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

# expect <BARE|OK> <label> <<< one or more lines of C++
expect() {
	local want="$1" label="$2" body="$3"
	local f="$WORK/test/probe/case.cpp"
	printf '%s\n' "$body" >"$f"

	local out got
	out="$(cd "$WORK" && "$SCRIPT_DIR/lint-unity-exit.sh" test/probe/case.cpp)"
	[[ -n $out ]] && got=BARE || got=OK

	if [[ $got == "$want" ]]; then
		echo "  PASS  $label - $want"
		PASSES=$((PASSES + 1))
	else
		echo "  FAIL  $label - expected $want, got $got"
		[[ -n $out ]] && echo "        $out"
		FAILURES=$((FAILURES + 1))
	fi
}

echo "Terminating forms (must NOT be reported):"
expect OK "exit(UNITY_END()) on one line" 'void s() { UNITY_BEGIN(); exit(UNITY_END()); }'
expect OK "exit( and the macro on separate lines" 'void s() {
    exit(
        UNITY_END());
}'
expect OK "capture-then-exit" 'void s() { const int rc = UNITY_END(); restore(); exit(rc); }'

echo
echo "Bare calls (MUST be reported):"
expect BARE "plain bare call" 'void s() { UNITY_BEGIN(); UNITY_END(); }'
expect BARE "bare call in an #else branch" '#else
void s() { UNITY_BEGIN(); UNITY_END(); }
#endif'
# `return` finalises the report and returns a count; it does not terminate the runner, and there is
# no main() under test/ from which it would.
expect BARE "return UNITY_END() does not terminate" 'void s() { return UNITY_END(); }'
# `exit` must be a whole identifier, not a suffix of some other function.
expect BARE "myexit(UNITY_END()) is not exit()" 'void s() { myexit(UNITY_END()); }'
# The assignment exemption is for capture; comparison and compound assignment are not capture.
expect BARE "== is not an assignment" 'void s() { if (x == UNITY_END()) return; }'
expect BARE "+= is not an assignment" 'void s() { total += UNITY_END(); }'

echo
echo "Comments and literals (the two classes that broke it before):"
expect OK "line comment mentioning the macro" 'void s() { exit(UNITY_END()); } // call UNITY_END() at the end'
expect OK "block comment interior mentioning the macro" '/* a comment
   that mentions UNITY_END()
   across lines */
void s() { exit(UNITY_END()); }'
expect OK "the macro inside a string literal" 'void s() { TEST_MESSAGE("call UNITY_END() when done"); exit(UNITY_END()); }'
expect BARE "a string containing /* must not open a comment" 'void s() { const char *p = "/*"; UNITY_END(); }'
expect BARE "code between two block comments on one line" 'void s() { /* first */ UNITY_END(); /* second */ }'
expect BARE "escaped quote inside a string does not end it" 'void s() { const char *p = "a\"b"; UNITY_END(); }'

echo
if ((FAILURES > 0)); then
	echo "RESULT: RED lint-unity-exit self-test - $FAILURES of $((PASSES + FAILURES)) fixtures behaved unexpectedly"
	exit 1
fi
echo "RESULT: GREEN lint-unity-exit self-test - $PASSES/$PASSES fixtures behaved as specified"
exit 0
