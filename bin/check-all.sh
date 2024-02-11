#!/usr/bin/env bash

# Note: This is a prototype for how we could add static code analysis to the CI.

set -e

VERSION=$(bin/buildinfo.py long)

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

if [[ $# -gt 0 ]]; then
	# can override which environment by passing arg
	BOARDS="$@"
else
	BOARDS="tlora-v2 tlora-v1 tlora_v1_3 tlora-v2-1-1.6 tbeam heltec-v1 heltec-v2.0 heltec-v2.1 tbeam0.7 meshtastic-diy-v1 rak4631 rak4631_eink rak11200 t-echo canaryone pca10059_diy_eink"
fi

echo "BOARDS:${BOARDS}"

CHECK=""
for BOARD in $BOARDS; do
	CHECK="${CHECK} -e ${BOARD}"
done

pio check --flags "-DAPP_VERSION=${APP_VERSION} --suppressions-list=suppressions.txt" $CHECK --skip-packages --pattern="src/" --fail-on-defect=medium --fail-on-defect=high
