#!/usr/bin/env bash
# Runs the native crypto suites and prints gcov line and branch coverage for src/crypto.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=.pio/build/coverage-crypto
find "$BUILD" -name '*.gcda' -delete 2>/dev/null || true
pio test -e coverage-crypto 2>&1 | grep 'succeeded'
cd "$BUILD/src/crypto"
gcov -b -c ./*.gcda 2>&1 | grep -A3 "File '.*/src/crypto/.*\.cpp'" | grep -v '^--'
rm -f ./*.gcov
