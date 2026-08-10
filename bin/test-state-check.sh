#!/usr/bin/env bash
# Self-test for the shared-state checker in bin/pio-test-isolate.sh.
#
# A checker that silently matches everything passes forever and nobody finds out - which is exactly
# how the leak it exists to catch survived. So it ships with fixtures: stand-in "suites" that write
# nothing, write exactly what they declare, write something undeclared, and declare a write they
# never make, asserting CLEAN / CLEAN / DIRTY / MISSING respectively. Plus one that proves the
# before-empty assertion fires, because an after-diff measured against a dirty baseline reports
# green while meaning nothing.
#
# Not a Unity suite and not counted in test/native-suite-count - the same arrangement as
# bin/test-config-check.sh, and for the same reason: what it asserts is the behaviour of a process,
# not of a linkable function.
#
# Usage: ./bin/test-state-check.sh   (exit 0 = all fixtures behaved)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

WORK="$(mktemp -d -t meshstatecheck.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

PREFS=".portduino/default/prefs"
MANIFEST="$WORK/manifest.tsv"
cat >"$MANIFEST" <<EOF
# suite	flags	reason
test_fixture_declared	writes=nodes.proto	fixture: writes exactly what it declares
test_fixture_undeclared	writes=nodes.proto	fixture: declares one file and writes two
test_fixture_missing	writes=warm.dat	fixture: declares a write it never makes
EOF

# Stand-in for a suite binary. Writes the files named in FIXTURE_WRITES into its own $HOME, and
# prints a Unity-shaped line so the wrapper can recover the suite name the same way it does for a
# real suite.
FAKE="$WORK/fake-suite.sh"
cat >"$FAKE" <<'EOF'
#!/usr/bin/env bash
set -u
echo "test/${FIXTURE_SUITE}/test_main.cpp:1:test_fixture:PASS"
mkdir -p "$HOME/.portduino/default/prefs"
for f in ${FIXTURE_WRITES:-}; do
  printf 'fixture payload\n' > "$HOME/.portduino/default/prefs/$f"
done
exit "${FIXTURE_RC:-0}"
EOF
chmod +x "$FAKE"

PASSES=0
FAILURES=0

# Run one fixture through the real wrapper and compare the verdict it recorded.
expect_verdict() {
	local label="$1" suite="$2" writes="$3" want="$4"
	local state_dir="$WORK/state-$suite"
	local summary="$state_dir/summary.tsv"
	rm -rf "$state_dir"
	mkdir -p "$state_dir"

	FIXTURE_SUITE="$suite" FIXTURE_WRITES="$writes" \
		MESHTASTIC_TEST_STATE_DIR="$state_dir" \
		MESHTASTIC_TEST_STATE_SUMMARY="$summary" \
		MESHTASTIC_TEST_STATE_MANIFEST="$MANIFEST" \
		"$SCRIPT_DIR/pio-test-isolate.sh" "$FAKE" >/dev/null 2>&1

	local got
	got="$(awk -F'\t' -v s="$suite" '$1 == s { print $3; exit }' "$summary" 2>/dev/null)"
	if [[ $got == "$want" ]]; then
		echo "  PASS  $label - $want"
		PASSES=$((PASSES + 1))
	else
		echo "  FAIL  $label - expected $want, got '${got:-<no entry>}'"
		FAILURES=$((FAILURES + 1))
	fi
}

echo "Fixture suites (verdict axis):"
expect_verdict "writes nothing" test_fixture_clean "" CLEAN
expect_verdict "writes what it declares" test_fixture_declared "nodes.proto" CLEAN
expect_verdict "writes something undeclared" test_fixture_undeclared "nodes.proto warm.dat" DIRTY
expect_verdict "declares a write it skips" test_fixture_missing "" MISSING

# Survivor axis. Stands in for a suite that ends on a bare UNITY_END(): it prints its Unity line and
# returns, but leaves a process running inside the sandbox $HOME, exactly as the runtime's loop()
# does. Asserts the wrapper both records it and kills it - a detector that reports without reaping
# would leave the host accumulating processes, which is half the harm.
echo
echo "Survivor axis (state_find_survivors):"
LEAKY="$WORK/leaky-suite.sh"
cat >"$LEAKY" <<'EOF'
#!/usr/bin/env bash
set -u
echo "test/${FIXTURE_SUITE}/test_main.cpp:1:test_fixture:PASS"
mkdir -p "$HOME/.portduino/default/prefs"
# Detached from this shell's stdout so the wrapper's `| tee` sees EOF and the pipeline returns -
# the survivor outlives the suite exactly as a spun loop() does.
setsid sleep 300 >/dev/null 2>&1 &
printf '%s\n' "$!" > "$HOME/../survivor.pid"
exit 0
EOF
chmod +x "$LEAKY"

survivor_dir="$WORK/state-survivor"
mkdir -p "$survivor_dir"
FIXTURE_SUITE=test_fixture_survivor \
	MESHTASTIC_TEST_STATE_DIR="$survivor_dir" \
	MESHTASTIC_TEST_STATE_SUMMARY="$survivor_dir/summary.tsv" \
	MESHTASTIC_TEST_STATE_MANIFEST="$MANIFEST" \
	"$SCRIPT_DIR/pio-test-isolate.sh" "$LEAKY" >/dev/null 2>&1

recorded="$(awk -F'\t' '$1 == "test_fixture_survivor" { print $6; exit }' "$survivor_dir/summary.tsv" 2>/dev/null)"
if [[ -n ${recorded// /} ]]; then
	echo "  PASS  a process outliving the suite is reported"
	PASSES=$((PASSES + 1))
else
	echo "  FAIL  a process outliving the suite went unreported"
	FAILURES=$((FAILURES + 1))
fi

# Find the pid file by search, not by a glob that assumes a directory depth: the wrapper renames
# its mktemp'd sandbox to the suite name when it keeps it, so the path is not fixed. Assert the
# file was found BEFORE asserting the process is gone - otherwise an empty pid takes the "not
# running" branch and the check passes without having checked anything.
leaked_pid="$(cat "$(find "$survivor_dir" -name survivor.pid -print -quit 2>/dev/null)" 2>/dev/null | head -1)"
if [[ -z $leaked_pid ]]; then
	echo "  FAIL  no survivor pid recorded - the reap assertion would pass vacuously"
	FAILURES=$((FAILURES + 1))
elif kill -0 "$leaked_pid" 2>/dev/null; then
	echo "  FAIL  the survivor was reported but left running (pid $leaked_pid)"
	kill -9 "$leaked_pid" 2>/dev/null
	FAILURES=$((FAILURES + 1))
else
	echo "  PASS  the survivor is reaped, not just reported (pid $leaked_pid)"
	PASSES=$((PASSES + 1))
fi

# Guard the guard. The wrapper mktemp's its own sandbox name, so the leak cannot be staged through
# it; exercise the assertion the wrapper actually calls instead - same function, same code path.
echo
echo "Before-empty assertion (state_assert_empty):"
# shellcheck source=bin/lib/test-state.sh
source "$SCRIPT_DIR/lib/test-state.sh"

seeded="$WORK/seeded"
mkdir -p "$seeded/$PREFS"
printf 'stale\n' >"$seeded/$PREFS/nodes.proto"
if state_assert_empty "$seeded" 2>/dev/null; then
	echo "  FAIL  a dirty sandbox was accepted - the after-diff would measure against the wrong baseline"
	FAILURES=$((FAILURES + 1))
else
	echo "  PASS  a dirty sandbox is refused"
	PASSES=$((PASSES + 1))
fi

empty="$WORK/empty"
mkdir -p "$empty"
if state_assert_empty "$empty" 2>/dev/null; then
	echo "  PASS  an empty sandbox is accepted"
	PASSES=$((PASSES + 1))
else
	echo "  FAIL  an empty sandbox was refused - the assertion matches everything"
	FAILURES=$((FAILURES + 1))
fi

echo
if ((FAILURES > 0)); then
	echo "RESULT: RED state-checker self-test - $FAILURES of $((PASSES + FAILURES)) fixtures behaved unexpectedly"
	exit 1
fi
echo "RESULT: GREEN state-checker self-test - $PASSES/$PASSES fixtures behaved as specified"
exit 0
