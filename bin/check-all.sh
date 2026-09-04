#!/usr/bin/env bash

# Run cppcheck static analysis over one or more PlatformIO environments.
#
# Why this is more than a bare `pio check`: the CI `check` matrix job in main_matrix.yml runs this
# script inside meshtastic/gh-action-firmware, and `pio check` must download the platform package
# before it can analyse anything. That download is regularly reset mid-flight by the CDN, e.g.
#
#   Checking station-g3 > cppcheck (board: station-g3; platform: .../platform-espressif32.zip)
#   requests.exceptions.ConnectionError: ('Connection aborted.', ConnectionResetError(104, ...))
#
# The analysis never starts, so there is no signal at all - just a red X someone has to re-run by
# hand. `pio check` exits 1 for that AND for real defects, so the exit code cannot tell them apart;
# only the output can. This script classifies the failure:
#
#   * cppcheck produced results (summary table or defect lines) -> real, propagate the exit status.
#   * transport-level network error and nothing else            -> warn and skip.
#   * anything else                                             -> propagate the exit status.
#
# A missing or forbidden package (404 / UnknownPackageError / 403) is deliberately NOT treated as
# transient: that is a reproducible break from a bad pin in a .ini, not weather.
#
# Exit codes: 0 = clean (or skipped under CI), 1 = defects/error, 2 = skipped locally.

set -uo pipefail

VERSION=$(bin/buildinfo.py long) || exit 1

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

if [[ $# -gt 0 ]]; then
	# can override which environment by passing arg
	BOARDS=("$@")
else
	BOARDS=(tlora-v2 tlora-v1 tlora_v1_3 tlora-v2-1-1.6 tbeam heltec-v2.0 heltec-v2.1 tbeam0.7 meshtastic-diy-v1 rak4631 rak4631_eink rak11200 t-echo canaryone pca10059_diy_eink)
fi

echo "BOARDS:${BOARDS[*]}"

# Array, not a string: board names reach pio as literal arguments, so an override containing
# whitespace or a glob character cannot turn into extra pio arguments.
CHECK=()
for BOARD in "${BOARDS[@]}"; do
	CHECK+=(-e "${BOARD}")
done

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# Keep streaming to the console so the CI log reads exactly as it did before; tee a copy for the
# post-mortem classification below.
pio check --flags "-DAPP_VERSION=${APP_VERSION} --suppressions-list=suppressions.txt --inline-suppr" "${CHECK[@]}" --skip-packages --pattern="src/" --fail-on-defect=low --fail-on-defect=medium --fail-on-defect=high 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}

if [[ $STATUS -eq 0 ]]; then
	exit 0
fi

# Fail closed: if cppcheck got far enough to report anything, the run is real no matter what other
# noise is in the log.
if grep -Eqi '^Component[[:space:]]+.*HIGH|^Total[[:space:]]+[0-9]|^[^[:space:]]+:[0-9]+: \[(high|medium|low)' "$LOG"; then
	exit "$STATUS"
fi

# Transport-level failures only. No 403/404/UnknownPackageError here, on purpose.
TRANSIENT='requests\.exceptions\.(ConnectionError|Timeout|ReadTimeout|ConnectTimeout)|ConnectionResetError|urllib3\.exceptions\.(ProtocolError|MaxRetryError|NameResolutionError|ReadTimeoutError)|Read timed out|Connection aborted|Connection refused|Temporary failure in name resolution|Could not resolve host|(429|500|502|503|504) (Server|Client) Error|Too Many Requests'

if ! grep -Eq "$TRANSIENT" "$LOG"; then
	exit "$STATUS"
fi

REASON=$(grep -Em1 "$TRANSIENT" "$LOG" | tr -d '\r' | cut -c1-200)

echo "RESULT: SKIPPED ${BOARDS[*]} - transient network failure: ${REASON}"

if [[ ${GITHUB_ACTIONS:-false} == "true" ]]; then
	echo "::warning title=Static analysis skipped (${BOARDS[*]})::pio check could not download its packages: ${REASON}"
	exit 0
fi

exit 2
