#!/usr/bin/env bash
#
# Fail if a program's captured output carries a sanitizer fault.
#
#   bin/check-sanitizer-log.sh <log-file> [what-produced-it]
#
# The native simulator is built from [env:coverage], which links -fsanitize=address, and a fault
# there leaves its report on stderr on the way out. The integration step in
# .github/workflows/test_native.yml points this at the simulator's captured output; the pattern is
# bin/lib/sanitizer.sh, the same one bin/run-tests.sh scans a local test run with.
#
# Exit 0 clean, 1 on a fault (with a GitHub error annotation), 2 on a usage or missing-file error.
set -euo pipefail

LOG="${1:-}"
WHAT="${2:-the program}"

if [ -z "$LOG" ]; then
	echo "usage: $0 <log-file> [what-produced-it]" >&2
	exit 2
fi
if [ ! -f "$LOG" ]; then
	echo "$0: no such log file: $LOG" >&2
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib/sanitizer.sh"

if grep -qE "$SAN_RE" "$LOG"; then
	echo "::error title=Sanitizer fault in ${WHAT}::A sanitizer report is in ${WHAT}'s output. The coverage env is built with -fsanitize=address, so this is a real memory fault in src/, not a test artifact. First matching lines below; the full output is in this step's log."
	# -m 4, not `| head -4`: head closing the pipe early is a SIGPIPE that pipefail reports and
	# set -e then exits on, in place of the status 1 this is documented to give.
	grep -m 4 -nE "$SAN_RE" "$LOG" | sed 's/^/    /'
	exit 1
fi

echo "No sanitizer faults in ${WHAT}'s output ($LOG)."
