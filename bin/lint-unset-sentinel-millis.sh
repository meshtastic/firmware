#!/usr/bin/env bash
# lint-unset-sentinel-millis.sh - flag a 0-means-unset deadline armed from a raw clock read.
#
# A family of fields in this tree uses 0 to mean "unarmed", and is then read as `if (field)` or
# `field != 0` before the deadline is compared. src/main.h says so out loud:
#
#   extern uint32_t enterDfuAtMsec; // 0 = unset; else millis() deadline for the deferred DFU jump
#
# Arming one of those with `field = millis() + delay` is correct for ~49.7 days and then wrong for
# one tick: the sum lands on 0 exactly once per wrap, and at that instant every reader decides the
# timer was never set. A pending reboot, shutdown, DFU jump or banner expiry is silently dropped.
# `field = millis()` has the same hole for a stamp. The fix is Time::timerEndsAtMillis(delay) for a
# countdown, or Time::skipZero(Time::getMillis()) for a stamp; both are in src/UptimeClock.h, whose
# static_asserts pin the behaviour this rule steers people toward.
#
# Why a name list and not a pattern over every `millis() + x`: most sums are fine. A local
# `const uint32_t deadline = millis() + timeoutMs;` that is compared a few lines later never stores
# 0 for anything to misread, and src/ has nine such sites that are all correct. The 0 contract is
# also declared in one file and enforced in six others - rebootAtMsec is written in AdminModule.cpp
# and tested in Power.cpp, PowerFSM.cpp, main.cpp, Screen.cpp and portduino/USBHal.h - so no
# single-file scan can infer it. The list below is therefore explicit.
#
# Two kinds of field are on it:
#
#   * Fields whose 0 IS the unset state. These must be armed through the helpers.
#   * Fields whose unset state is a separate flag, so 0 is a value they may legally hold. These are
#     listed so that the rule notices them, and each arm site carries an opt-out comment naming the
#     flag that actually carries the armed state. Listing-plus-opt-out beats silent omission: if
#     someone later rewrites `if (haveSample && ...)` into `if (lastSampleMs && ...)`, the field has
#     quietly acquired the contract, and the opt-out comment is sitting right there at the write to
#     be reconsidered.
#
# OPTING OUT. Put `unset-sentinel-ok: <reason>` in a comment, either on the line of the write or on
# a comment line above it:
#
#   // unset-sentinel-ok: busyTx carries the armed state, so 0 is a legal timestamp here
#   lastTxStart = Time::getMillis();
#
# The reason is mandatory - a bare `unset-sentinel-ok` with nothing after the colon is reported
# rather than honoured, so a site cannot be muted without saying why. trunk-ignore works too, but
# prefer this: it states the justification at the write, and it also applies when the script is run
# directly rather than through trunk.
#
# Adding a field: append it to SENTINELS. If its 0 is the unset state, fix the arm sites; if a
# separate flag carries the armed state, add an opt-out comment at each write. Verify which of the
# two it is by reading every site that READS the field - one missed read is what makes this wrong.
#
# Three fields are absent on purpose, and NOT because 0 is safe there. Each was examined and came
# back unresolved rather than exempt, so listing one would mean stamping an opt-out over a claim
# that does not hold:
#
#   * nagCycleCutoff. ExternalNotificationModule::handleInputEvent reads
#     `if (nagCycleCutoff != UINT32_MAX)` without consulting isNagging, so at that read the field is
#     its own armed flag with UINT32_MAX - not 0 - as the sentinel, and the arm site can land there.
#     skipZero() would not help: it lifts 0 to 1 and leaves UINT32_MAX alone, by design. The fix is
#     to gate that read on isNagging, a behaviour change that belongs in its own PR.
#   * TouchScreenBase::_start. Overloaded as both an event stamp and a `+ 30000` suppression
#     deadline compared by signed subtraction, so a near-zero value reads as "long ago" rather than
#     "armed 30s out", and LONG_PRESS re-fires. skipZero() does not fix that either - 1 reads as
#     long-ago exactly as 0 does. It needs the stamp and the deadline held separately.
#   * StoreForwardModule::retry_delay. Has no reads at all today, so nothing misbehaves yet;
#     exempting it now would pre-approve the raw arm for whoever implements the retry its own
#     comment promises.
#
# Also absent, for the ordinary reason that 0 carries no meaning there: locals such as
# NodeInfoModule's lastNodeInfo, derived per call from TransmitHistory rather than stored.
#
# Emitted at "error" rather than the "note" its neighbours use, because nothing catches this at run
# time: the window is one tick in seven weeks, so a test run, a soak and a bench session all pass.
# The tree has zero unexplained violations, so blocking costs nothing and is the only thing that
# actually prevents the next one.
#
# Not handled: a write through an alias (`uint32_t &d = rebootAtMsec; d = millis() + 5;`) or through
# a pointer, and a sentinel armed by a helper that takes it by reference. None occur, and tracking
# aliases is untested code guarding a case that does not exist. Raw string literals (R"(...)") are
# not tokenised either, for the same reason as in bin/lint-unity-exit.sh.
#
# bin/test-lint-unset-sentinel-millis.sh is this rule's self-test; every false positive and false
# negative found in review belongs there as a fixture.
#
# Emits one line per finding in the format
#   <path>:<line>:<col>:<severity>:<message>:<code>
# which trunk parses via parse_regex. Always exits 0; findings go to stdout.

set -uo pipefail

# Millisecond fields this rule watches. See the notes above before editing.
SENTINELS='rebootAtMsec|shutdownAtMsec|enterDfuAtMsec|alertBannerUntil|pulseOffAt|delayedPulseAt|ntp_renew|tx_after|suppressTouchTapUntilMs|fixHoldEnds|lastChipRecoveryMs|activeReceiveStart|rxTimeMsec|lastInterruptTime|lastSentReply|lastSort|lastTxStart|lastHeartbeat|lastAveraged|lastSampleMs|lastIaqMs|last_format_ms|nextRepeatX|nextRepeatY|_cached_next_run'

for target in "$@"; do
	[[ -f $target ]] || continue

	# Path is reported relative to the workspace so findings are clickable from the repo root.
	rel="${target#"$PWD"/}"

	# Firmware sources only. Tests construct raw wrap values on purpose - pinning what happens at
	# 0xFFFFFFFF is the point of test_uptime_clock - and UptimeClock.h itself defines the helpers.
	[[ $rel == src/* ]] || continue
	[[ $rel == src/UptimeClock.h ]] && continue

	awk -v path="$rel" -v sentinels="$SENTINELS" '
	# Return the line with comments and string/char literals removed, carrying /* ... */ state
	# across lines, and fill colmap[] mapping each position in the result back to its raw column.
	# Character-level rather than layered regexes, for the reasons spelled out at length in
	# bin/lint-unity-exit.sh: a /* inside a string literal flips comment state and hides real code,
	# and an assignment quoted inside a log string reads as one. Literals collapse to a space so two
	# tokens cannot be glued together.
	#
	# Also sets CMT to this line s comment text, which is where an opt-out has to live. Collecting
	# it here rather than re-scanning the raw line is what stops `LOG_DEBUG("unset-sentinel-ok: x")`
	# from muting anything: a string literal is not a comment.
	function strip_noncode(s,   out, i, n, c, two, q) {
		n = length(s); i = 1; out = ""
		delete colmap
		CMT = ""
		while (i <= n) {
			if (in_block) {
				if (substr(s, i, 2) == "*/") { in_block = 0; i += 2 }
				else { CMT = CMT substr(s, i, 1); i++ }
				continue
			}
			two = substr(s, i, 2)
			if (two == "//") { CMT = CMT " " substr(s, i + 2); return out }
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

	# An opt-out only counts with something after the colon. A bare marker is reported instead of
	# honoured, so "shut this up" is not available without writing down why.
	function has_reasoned_ok(t) { return (t ~ /unset-sentinel-ok:[ \t]*[^ \t]/) }
	function has_bare_ok(t)     { return (t ~ /unset-sentinel-ok/) && !has_reasoned_ok(t) }

	# Is the character before position `at` part of an identifier? Used to require a token boundary,
	# so myRebootAtMsec is not mistaken for rebootAtMsec. A `.`, `->` or `::` qualifier is a
	# boundary on purpose: txp->tx_after and NotificationRenderer::alertBannerUntil are the real
	# call sites and must still be seen.
	function ident_before(s, at,   c) {
		if (at <= 1) return 0
		c = substr(s, at - 1, 1)
		return (c ~ /[A-Za-z0-9_]/)
	}

	# A local declaration that happens to reuse a sentinel name shadows the field and carries none
	# of its contract, so it is not this rule business. Detected by a type-ish token immediately
	# before the name - `uint32_t tx_after = millis() + d;` declares a local, `tx_after = ...` does
	# not. Kept narrow: only the spellings this tree actually uses for a millis value.
	function is_declaration(s, at,   head) {
		head = substr(s, 1, at - 1)
		sub(/[ \t]*(\*|&)?[ \t]*$/, "", head)
		return (head ~ /(^|[^A-Za-z0-9_])(uint32_t|uint64_t|int32_t|unsigned[ \t]+long|unsigned[ \t]+int|unsigned|long|int|auto|size_t|TickType_t)$/)
	}

	BEGIN { LINE_CAP = 12 }	# give up accumulating a statement after this many lines

	{
		code = strip_noncode($0)

		# An opt-out is sticky until the next statement that actually contains code is judged. That
		# is what lets it sit on its own line above the write, however many comment lines intervene,
		# without leaking past the statement it was written for.
		if (has_reasoned_ok(CMT)) pending_ok = 1
		if (has_bare_ok(CMT))     pending_bare = 1

		if (stmt == "") { start = NR; nhits = 0 }
		base = length(stmt) + 1	# the leading space added below shifts everything by one
		stmt = stmt " " code

		# Record every `<sentinel> =` on this line, with its position in the accumulated statement
		# so the RHS can be judged once the statement is whole.
		rest = code; off = 0
		while (match(rest, "(" sentinels ")[ \t]*=")) {
			p = RSTART; len = RLENGTH
			off += p
			# A plain assignment only: ==, !=, <=, >=, +=, -= and friends are reads or updates,
			# not an arming write, and `=` must not be the head of `==`.
			eq = off + len - 1
			prev = (eq - 1 >= 1) ? substr(code, eq - 1, 1) : " "
			nxt  = (eq + 1 <= length(code)) ? substr(code, eq + 1, 1) : " "
			if (prev !~ /[-+*\/%&|^!<>=]/ && nxt != "=" && !ident_before(code, off) &&
			    !is_declaration(code, off)) {
				nhits++
				hit_at[nhits] = base + eq	# index of the `=` within stmt
				hit_line[nhits] = NR
				hit_col[nhits] = colmap[off]
				hit_name[nhits] = substr(code, off, len - 1)
				sub(/[ \t]*$/, "", hit_name[nhits])
			}
			rest = substr(rest, p + len - 1)
			off += len - 2
		}

		# End of statement: judge each recorded write against its own right-hand side.
		if (code ~ /;/ || NR - start >= LINE_CAP) {
			for (k = 1; k <= nhits; k++) {
				# The right-hand side of THIS write only, cut at its own semicolon. Without the
				# cut, rhs ran on to the end of the accumulated statement and judged a neighbour
				# as if it belonged to this write - wrongly in both directions. On
				#   rebootAtMsec = millis() + 5; shutdownAtMsec = Time::timerEndsAtMillis(10);
				# the later helper call suppressed a genuine raw arm, and on
				#   rebootAtMsec = otherDeadline; shutdownAtMsec = millis();
				# the later millis() reported a safe copy. Both are fixtures now.
				rhs = substr(stmt, hit_at[k] + 1)
				semi = index(rhs, ";")
				if (semi > 0) rhs = substr(rhs, 1, semi - 1)

				# Only a raw clock read is a finding. `= 0` disarms, a copy from another
				# variable inherits whatever that one did, and anything already routed through
				# the helpers is the fix rather than the defect. Matching `millis` loosely
				# covers millis(), Time::getMillis() and any wrapper ending in millis.
				if (rhs !~ /[Mm]illis[ \t]*\(/ || rhs ~ /skipZero/ || rhs ~ /timerEndsAtMillis/)
					continue
				if (pending_ok)
					continue	# opted out, with a reason, at the write
				if (pending_bare)
					printf "%s:%d:%d:%s:%s:%s\n", path, hit_line[k], hit_col[k], "error",
					       "unset-sentinel-ok needs a reason after the colon saying why 0 is legal for " hit_name[k] " (see bin/lint-unset-sentinel-millis.sh)",
					       "unset-sentinel-millis"
				else
					printf "%s:%d:%d:%s:%s:%s\n", path, hit_line[k], hit_col[k], "error",
					       hit_name[k] " is 0-means-unset - arm it with Time::timerEndsAtMillis(delay), or Time::skipZero(Time::getMillis()) for a stamp (see src/UptimeClock.h)",
					       "unset-sentinel-millis"
			}
			# Comment-only lines carry an opt-out toward the write below them, so they must not
			# clear it; a statement with real code in it consumes it.
			if (stmt ~ /[^ \t]/) { pending_ok = 0; pending_bare = 0 }
			stmt = ""
			nhits = 0
		}
	}
	' "$target"
done

exit 0
