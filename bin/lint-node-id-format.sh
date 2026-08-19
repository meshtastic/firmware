#!/usr/bin/env bash
# lint-node-id-format.sh - flag node/packet IDs logged as bare %08x instead of 0x%08x.
#
# src/mesh/RadioInterface.cpp states the convention: node IDs and packet IDs are
# formatted 0x%08x in logs and !%08x in user-facing display. A bare %08x still
# prints the right digits, so nothing breaks - it just makes the value hard to
# grep for and easy to misread as decimal.
#
# Emitted at "note" on purpose: this is a consistency rule, not a correctness one, so
# it should never be the thing that fails someone's review. Note is trunk's only
# non-blocking level - "warning" and "info" both exit non-zero and would gate CI.
#
# Only flags a %08x whose statement also mentions an ID-shaped argument
# (->num, .from, nodeNum, getFrom(), ...). A 32-bit hex value that is not an ID -
# a CRC, a register, a hash - is none of this rule's business.
#
# Emits one line per finding in the format
#   <path>:<line>:<col>:<severity>:<message>:<code>
# which trunk parses via parse_regex. Always exits 0; findings go to stdout.

set -uo pipefail

ALLOWLIST=".github/node-id-format-allowlist.txt"

for target in "$@"; do
	[[ -f $target ]] || continue

	# Path is reported relative to the workspace so allowlist entries stay portable.
	rel="${target#"$PWD"/}"

	awk -v path="$rel" -v allowlist="$ALLOWLIST" '
	BEGIN {
		LINE_CAP = 12 # give up accumulating a statement after this many lines

		# Allowlist entries are "<path>" (whole file) or "<path>:<line>", followed by
		# whitespace and a mandatory reason. Blank lines and # comments are ignored.
		while ((getline line < allowlist) > 0) {
			sub(/#.*/, "", line)
			gsub(/^[ \t]+|[ \t]+$/, "", line)
			if (line == "") continue
			split(line, f, /[ \t]+/)
			skip[f[1]] = 1
		}
		close(allowlist)
		if (path in skip) exempt_file = 1
	}

	# Two independent signals, either of which marks the value as an ID: an ID-shaped
	# argument, or the message text naming one. The second catches the common
	# LOG_INFO("node %08x", n) shape, where the argument alone is indistinguishable
	# from a CRC. Deliberately a allowlist of shapes rather than "any variable" - a
	# false positive here costs more than a miss.
	function looks_like_id(s) {
		return (s ~ /(->|\.)(num|from|to|id|dest|sender|relay_node|next_hop)[^A-Za-z0-9_]/) ||
		       (s ~ /[Nn]ode[Nn]um/) || (s ~ /nodeId/) || (s ~ /getFrom[ \t]*\(/) ||
		       (s ~ /[^A-Za-z0-9_]sender[^A-Za-z0-9_]/) ||
		       (s ~ /[Nn]ode/) || (s ~ /[Pp]acket/) || (s ~ /[Ss]ender/) || (s ~ /[Rr]elay/)
	}

	# True when the text still contains a %08x after every correctly-prefixed
	# 0x%08x has been removed - i.e. at least one occurrence is bare.
	function has_bare_hex(s,   t) {
		t = s
		gsub(/0[xX]%08[xX]/, "", t)
		gsub(/![ \t]*%08[xX]/, "", t) # !%08x is the user-facing display form, also fine
		return (t ~ /%08[xX]/)
	}

	{
		if (exempt_file) next

		# Accumulate a logical LOG_ statement; these routinely wrap across lines.
		if (!in_stmt && $0 ~ /LOG_[A-Z]+[ \t]*\(/) {
			in_stmt = 1; stmt = $0; start = NR; hit_line = 0; hit_col = 0
		} else if (in_stmt) {
			stmt = stmt " " $0
		} else {
			next
		}

		# Remember the first line carrying a bare %08x, for a useful caret position.
		if (!hit_line && has_bare_hex($0)) { hit_line = NR; hit_col = index($0, "%08") }

		# End of statement: any line closing the call. Matched anywhere on the line, not
		# just at EOL, so `LOG_INFO(...); }` terminates too - if it did not, the
		# accumulator would run to EOF and silently swallow every later finding in the
		# file. LINE_CAP is the same backstop for a close paren we never see at all.
		if ($0 ~ /\)[ \t]*;/ || NR - start >= LINE_CAP) {
			if (has_bare_hex(stmt) && looks_like_id(stmt)) {
				if (!hit_line) { hit_line = start; hit_col = 1 }
				if ((path ":" hit_line) in skip) { in_stmt = 0; next }
				printf "%s:%d:%d:%s:%s:%s\n", path, hit_line, (hit_col ? hit_col : 1), "note",
				       "node/packet ID logged as bare %08x - use 0x%08x (see src/mesh/RadioInterface.cpp)",
				       "node-id-format"
			}
			in_stmt = 0
		}
	}
	' "$target"
done

exit 0
