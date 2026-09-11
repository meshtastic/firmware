#!/usr/bin/env bash
# test-lint-unset-sentinel-millis.sh - self-test for bin/lint-unset-sentinel-millis.sh.
#
# The scanner has to tell an arming write apart from a read, a disarm, a shadowing local, a quoted
# string and an already-fixed site. Each case below is a fixture: a snippet, the lines that must be
# reported, and nothing else. Run it after any change to the rule.
#
# Exit 0 = all cases pass, 1 = at least one case failed.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LINT="$ROOT_DIR/bin/lint-unset-sentinel-millis.sh"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

FAILURES=0

# run_case <name> <expected lines, newline-separated or empty> <source>
run_case() {
	local name="$1" expect="$2" body="$3"
	# The rule only looks at src/, and reports paths relative to $PWD, so the fixture has to live
	# under a src/ directory that is also the working directory's child.
	local dir="$WORK/case"
	rm -rf "$dir"
	mkdir -p "$dir/src"
	printf '%s\n' "$body" >"$dir/src/fixture.cpp"

	local got
	got=$(cd "$dir" && "$LINT" src/fixture.cpp | awk -F: '{print $2}' | paste -sd, -)
	local want
	want=$(printf '%s' "$expect" | paste -sd, -)

	if [[ $got == "$want" ]]; then
		echo "PASS  $name"
	else
		echo "FAIL  $name: expected lines [$want], got [$got]"
		FAILURES=$((FAILURES + 1))
	fi
}

# --- must be reported ---------------------------------------------------------

run_case "bare millis() sum" "2" 'void f() {
    rebootAtMsec = millis() + 5000;
}'

run_case "bare millis() stamp" "2" 'void f() {
    shutdownAtMsec = millis();
}'

run_case "Time::getMillis() sum" "2" 'void f() {
    enterDfuAtMsec = Time::getMillis() + 25;
}'

run_case "qualified and arrow targets" "2
3" 'void f(MeshPacket *txp) {
    NotificationRenderer::alertBannerUntil = millis() + durationMs;
    txp->tx_after = millis() + delay;
}'

run_case "statement split across lines" "2" 'void f() {
    ntp_renew =
        millis() + 43200 * 1000;
}'

run_case "parenthesised sum" "2" 'void f() {
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}'

run_case "two writes on one line" "2
2" 'void f() {
    rebootAtMsec = millis() + 5; shutdownAtMsec = millis();
}'

run_case "code after a block comment on the same line" "2" 'void f() {
    /* arm it */ pulseOffAt = millis() + durationMs;
}'

# --- must NOT be reported ----------------------------------------------------

run_case "already fixed - timerEndsAtMillis" "" 'void f() {
    rebootAtMsec = Time::timerEndsAtMillis(5000);
}'

run_case "already fixed - skipZero stamp" "" 'void f() {
    shutdownAtMsec = Time::skipZero(Time::getMillis());
}'

run_case "already fixed - ternary keeping the 0 arm" "" 'void f() {
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : Time::timerEndsAtMillis(durationMs);
}'

run_case "disarm" "" 'void f() {
    rebootAtMsec = 0;
    shutdownAtMsec = 0;
}'

run_case "reads and comparisons" "" 'void f() {
    if (rebootAtMsec && Throttle::deadlinePassed(rebootAtMsec)) {}
    if (shutdownAtMsec == 0 && millis() > 5) {}
    if (tx_after != 0) {}
}'

run_case "shadowing local declaration" "" 'void f() {
    uint32_t tx_after = millis() + 100;
    unsigned long rxTimeMsec = millis();
}'

run_case "longer identifier containing a sentinel name" "" 'void f() {
    myRebootAtMsec = millis() + 5000;
    lastRxTimeMsec = millis();
}'

run_case "inside a line comment" "" 'void f() {
    // rebootAtMsec = millis() + 5000;
}'

run_case "inside a block comment" "" 'void f() {
/*
    rebootAtMsec = millis() + 5000;
*/
}'

run_case "inside a string literal" "" 'void f() {
    LOG_DEBUG("rebootAtMsec = millis() + 5000");
}'

run_case "copy from another variable" "" 'void f() {
    rebootAtMsec = otherDeadline;
}'

run_case "compound assignment" "" 'void f() {
    rebootAtMsec += millis();
}'

# A field that is simply not on the list, and a local that never persists. nagCycleCutoff is NOT
# used as the example here: it is off the list because its exemption was rejected, not because 0 is
# safe for it, so pinning it as a negative fixture would encode the opposite of what the header says.
run_case "unlisted field and a local deadline" "" 'void f() {
    someUnrelatedDeadline = millis() + durationMs;
    const uint32_t deadline = millis() + BODY_TIMEOUT_MS;
}'

# --- opt-out comments --------------------------------------------------------

run_case "opt-out on the same line" "" 'void f() {
    lastSort = millis(); // unset-sentinel-ok: sortingIsPaused gates it, 0 is legal
}'

run_case "opt-out on the line above" "" 'void f() {
    // unset-sentinel-ok: busyTx carries the armed state
    tx_after = millis() + d;
}'

run_case "opt-out above, separated by more comment lines" "" 'void f() {
    // unset-sentinel-ok: a separate flag carries the armed state
    // and here is some more explanation spilling onto another line
    // and another
    tx_after = millis() + d;
}'

run_case "opt-out in a block comment" "" 'void f() {
    /* unset-sentinel-ok: a separate flag carries the armed state */
    tx_after = millis() + d;
}'

run_case "opt-out in a multi-line block comment" "" 'void f() {
    /*
     * unset-sentinel-ok: a separate flag carries the armed state
     */
    tx_after = millis() + d;
}'

# A bare marker is reported rather than honoured, so nothing can be muted silently.
run_case "bare opt-out with no reason" "2" 'void f() {
    rebootAtMsec = millis() + 5000; // unset-sentinel-ok
}'

run_case "bare opt-out with a colon but nothing after it" "2" 'void f() {
    rebootAtMsec = millis() + 5000; // unset-sentinel-ok:
}'

# Must not be mutable from data. A marker inside a string literal is not a comment.
run_case "marker inside a string literal does not mute" "3" 'void f() {
    LOG_DEBUG("unset-sentinel-ok: pretend this counts");
    rebootAtMsec = millis() + 5000;
}'

run_case "marker in a trailing string on the same line does not mute" "2" 'void f() {
    rebootAtMsec = millis() + 5000; LOG_DEBUG("unset-sentinel-ok: nope");
}'

# The opt-out is consumed by the statement it was written for and must not leak onward.
run_case "opt-out does not leak to the next write" "3" 'void f() {
    lastSort = millis(); // unset-sentinel-ok: legal here
    rebootAtMsec = millis() + 5000;
}'

run_case "opt-out attached to an unrelated statement does not leak" "3" 'void f() {
    int x = 1; // unset-sentinel-ok: nothing to do with the line below
    rebootAtMsec = millis() + 5000;
}'

run_case "opt-out covers both writes on its own line only" "3" 'void f() {
    tx_after = millis() + 1; lastSort = millis(); // unset-sentinel-ok: both legal
    rebootAtMsec = millis() + 5000;
}'

# --- scope -------------------------------------------------------------------

# test/ builds raw wrap values on purpose, so the rule must not reach into it.
mkdir -p "$WORK/scope/test"
printf 'void f() { rebootAtMsec = millis() + 5000; }\n' >"$WORK/scope/test/test_main.cpp"
if [[ -z $(cd "$WORK/scope" && "$LINT" test/test_main.cpp) ]]; then
	echo "PASS  test/ is out of scope"
else
	echo "FAIL  test/ is out of scope: expected no findings"
	FAILURES=$((FAILURES + 1))
fi

# The helpers' own header must not report itself.
mkdir -p "$WORK/self/src"
printf 'void f() { rebootAtMsec = millis() + 5000; }\n' >"$WORK/self/src/UptimeClock.h"
if [[ -z $(cd "$WORK/self" && "$LINT" src/UptimeClock.h) ]]; then
	echo "PASS  src/UptimeClock.h is exempt"
else
	echo "FAIL  src/UptimeClock.h is exempt: expected no findings"
	FAILURES=$((FAILURES + 1))
fi

echo
if [[ $FAILURES -eq 0 ]]; then
	echo "RESULT: PASS"
	exit 0
fi
echo "RESULT: FAIL ($FAILURES case(s))"
exit 1
