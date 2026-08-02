# shellcheck shell=bash
#
# Shared helpers for the native test harness's shared-state check. Sourced by
# bin/pio-test-isolate.sh (which enforces it per suite) and bin/test-state-check.sh (which proves
# the checker itself still works). Nothing here executes on its own.
#
# Linux-only, like the rest of the native harness: this uses GNU coreutils behaviour (`find -printf`,
# md5sum) rather than carrying a per-host fallback. bin/run-tests.sh states and enforces that.
#
# The check answers one question: did this suite change any file it did not declare? It deliberately
# does NOT compare file *contents* against a baseline. Content baselines over protobuf bytes are
# snapshot tests - add a field to NodeInfoLite and every recorded hash in the repo churns, which is
# how snapshot suites turn into an --update-all ritual and then into noise. Hashes are used only to
# answer the boolean "did this change?"; what gets declared and reviewed is the set of paths.

STATE_MANIFEST_DEFAULT="test/state-manifest.tsv"

# Files the native binaries persist under $HOME. Listed for documentation and for the
# --write-manifest hint; the scan itself is unfiltered, so a suite writing somewhere unexpected is
# still caught.
# shellcheck disable=SC2034 # referenced by callers and by the docs
STATE_KNOWN_FILES="nodes.proto config.proto channels.proto module.proto device.proto warm.dat transmit_history.dat"

# Guard the guard: refuse to run a suite against a sandbox that is not empty. If isolation ever
# leaks, the after-diff measures against the wrong baseline and the whole check reports CLEAN while
# meaning nothing - so before-empty is as load-bearing as after-diff. Returns non-zero and explains
# itself rather than carrying on.
state_assert_empty() {
	local dir="$1"
	if [[ -n $(find "$dir" -mindepth 1 -print -quit 2>/dev/null) ]]; then
		echo "test-state: sandbox $dir is not empty before the suite ran - isolation is broken" >&2
		return 1
	fi
	return 0
}

# Fingerprint every file under $1 as "<relative-path> <md5>", sorted. Empty output for an empty or
# missing tree. Output is fed to comm/diff, so the sort order has to be stable across calls.
#
# GNU `find -printf` and md5sum(1), deliberately: this harness is Linux-only and bin/run-tests.sh
# refuses to start anywhere else, so there is no host here that needs a BSD fallback.
state_fingerprint() {
	local root="$1"
	[[ -d $root ]] || return 0
	(
		cd "$root" || return 0
		find . -type f -printf '%P\n' 2>/dev/null | LC_ALL=C sort | while IFS= read -r rel; do
			printf '%s %s\n' "$rel" "$(md5sum -- "$rel" 2>/dev/null | cut -d' ' -f1)"
		done
	)
}

# Processes still running inside the suite's sandbox $HOME. Prints one PID per line.
#
# A suite that ends on a bare UNITY_END() does not stop: setup() returns, the runtime keeps calling
# loop(), and PlatformIO - which reports a suite from its Unity output, not from process exit -
# moves on with the binary still resident. Nothing else notices, and the damage is quiet: the
# sandbox gets deleted under a live process, so the after-fingerprint below describes what the suite
# had written when we stopped looking rather than what it left behind, and .gcda plus LeakSanitizer
# both flush from atexit handlers that never run.
#
# Matching on the environment rather than on a remembered PID is deliberate: the sandbox HOME is
# mktemp-unique per suite, so this identifies survivors whatever their parentage - a fork, a
# grandchild, a process already reparented to init - none of which a $! comparison would catch.
# Scoped to this user's processes: /proc/<pid>/environ is unreadable for anyone else's anyway, and
# the narrower sweep costs ~270ms against ~460ms for all of /proc.
state_find_survivors() {
	local home="$1" pid
	[[ -n $home ]] || return 0
	for pid in $(ps -u "$(id -u)" -o pid= 2>/dev/null); do
		[[ $pid == "$$" ]] && continue
		if tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | grep -qxF "HOME=$home"; then
			printf '%s\n' "$pid"
		fi
	done
}

# Paths present in the "after" fingerprint ($2) that are absent or different in "before" ($1).
# Prints one relative path per line.
state_changed_paths() {
	local before="$1" after="$2"
	LC_ALL=C comm -13 <(LC_ALL=C sort "$before") <(LC_ALL=C sort "$after") | awk '{print $1}' | LC_ALL=C sort -u
}

# Read a suite's flag string out of the manifest. Empty when the suite has no entry, which is the
# default and means "isolated": fresh state in, contents discarded out.
#
# Manifest format - TSV, three columns, the same shape as an allowlist entry: the thing, what it is
# allowed to do, and why. The reason column is mandatory and is what a reviewer reads.
#
#   test_nodedb_blocked<TAB>state=per-suite writes=nodes.proto<TAB>saturates the DB to test the cap
state_manifest_flags() {
	local suite="$1" manifest="${2:-$STATE_MANIFEST_DEFAULT}"
	[[ -f $manifest ]] || return 0
	awk -F'\t' -v s="$suite" '!/^[[:space:]]*#/ && $1 == s { print $2; exit }' "$manifest"
}

# Pull one flag's value out of a flag string: state_flag_value "writes" "state=per-suite writes=a,b"
state_flag_value() {
	local key="$1" flags="$2" f
	for f in $flags; do
		[[ $f == "$key="* ]] && {
			printf '%s' "${f#"$key="}"
			return 0
		}
	done
	return 0
}

# Does a changed path match a declared write? A declaration matches either the full path relative to
# the scratch HOME or just the basename, because the useful name for these is the basename
# (`nodes.proto`) and nobody should have to write .portduino/default/prefs/ in front of it.
state_path_declared() {
	local path="$1" declared="$2" entry
	IFS=',' read -ra _entries <<<"$declared"
	for entry in "${_entries[@]}"; do
		[[ -z $entry ]] && continue
		[[ $path == "$entry" || ${path##*/} == "$entry" ]] && return 0
	done
	return 1
}

# Classify a suite's leftovers. Prints "<verdict>\t<detail>" where verdict is one of:
#
#   CLEAN    nothing changed, or everything that changed was declared
#   DIRTY    at least one undeclared path changed - the finding this whole check exists for
#   MISSING  every changed path was declared, but a declared path did NOT change
#
# MISSING is reported separately rather than folded into DIRTY because it catches the opposite bug:
# persistence that silently stopped happening. That is a real class here - the TAK config bug
# upstream was a has_ flag never being set, so the save wrote nothing and no test noticed. It starts
# as a warning because some declared writes are legitimately conditional.
state_classify() {
	local changed="$1" declared="$2"
	local undeclared=() missing=() path entry found

	while IFS= read -r path; do
		[[ -z $path ]] && continue
		if ! state_path_declared "$path" "$declared"; then
			undeclared+=("$path")
		fi
	done <<<"$changed"

	# Ask state_path_declared() in the other direction rather than matching by hand: one rule for
	# "does this path match this declaration", so the two directions cannot drift apart. Matching
	# an entry as a regex would also let a metacharacter in a manifest name (`.`, `+`) match a file
	# that is not the declared one.
	IFS=',' read -ra _declared <<<"$declared"
	for entry in "${_declared[@]}"; do
		[[ -z $entry ]] && continue
		found=1
		while IFS= read -r path; do
			[[ -z $path ]] && continue
			if state_path_declared "$path" "$entry"; then
				found=0
				break
			fi
		done <<<"$changed"
		((found)) && missing+=("$entry")
	done

	if ((${#undeclared[@]} > 0)); then
		printf 'DIRTY\tundeclared: %s\n' "${undeclared[*]}"
	elif ((${#missing[@]} > 0)); then
		printf 'MISSING\tdeclared but unwritten: %s\n' "${missing[*]}"
	else
		printf 'CLEAN\t\n'
	fi
}
