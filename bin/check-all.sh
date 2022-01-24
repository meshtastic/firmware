#!/usr/bin/env bash

# Note: This is a prototype for how we could add static code analysis to the CI.

set -e

VERSION=`bin/buildinfo.py long`

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

# only check high and medium in our source
# TODO: only doing tbeam (to start; add all/more later)
#pio check --flags "-DAPP_VERSION=${APP_VERSION} --suppressions-list=suppressions.txt" -e tbeam --skip-packages --severity=medium --severity=high --pattern="src/"
pio check --flags "-DAPP_VERSION=${APP_VERSION} --suppressions-list=suppressions.txt" -e tbeam --skip-packages --pattern="src/"
return_code=$?

# TODO: not sure why return_code is 0
echo "return_code:${return_code}"
