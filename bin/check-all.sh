#!/usr/bin/env bash

# Note: This is a prototype for how we could add static code analysis to the CI.

set -e

VERSION=`bin/buildinfo.py long`

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

if [[ $# -gt 0 ]]; then
    # can override which environment by passing arg
    BOARDS="-e $1"
else
    BOARDS="-e tlora-v2 -e tlora-v1 -e tlora_v1_3 -e tlora-v2-1-1.6 -e tbeam -e heltec-v1 -e heltec-v2.0 -e heltec-v2.1 -e tbeam0.7 -e meshtastic-diy-v1 -e rak4631_5005 -e rak4631_19003 -e t-echo"
fi

#echo "BOARDS:${BOARDS}"
pio check --flags "-DAPP_VERSION=${APP_VERSION} --suppressions-list=suppressions.txt" $BOARDS --skip-packages --pattern="src/"
#return_code=$?
# TODO: not sure why return_code is 0
