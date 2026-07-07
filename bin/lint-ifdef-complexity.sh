#!/usr/bin/env bash
# lint-ifdef-complexity.sh - flag preprocessor conditionals that OR together
# too many defined() terms (e.g. long display-driver chains in main.cpp).
#
# Such chains should be replaced by a single umbrella feature macro computed
# once in a central header (this repo already has HAS_TFT / HAS_SCREEN in
# src/configuration.h). The compiler has no warning for this, so we lint it.
#
# Emits one line per offending logical #if/#elif in the format
#   <path>:<line>:<col>:<severity>:<message>:<code>
# which trunk parses via parse_regex. Always exits 0; findings go to stdout.
#
# Usage: lint-ifdef-complexity.sh <file> [<file> ...]
# Tune the limit with MAX_DEFINED (default 5 → warns at 6+ terms).

set -euo pipefail

MAX_DEFINED="${MAX_DEFINED:-5}"

awk -v max="${MAX_DEFINED}" '
  # Start of a #if / #elif (but not #ifdef / #ifndef, which carry one term).
  /^[[:space:]]*#[[:space:]]*(if|elif)([[:space:](]|$)/ {
    startln = FNR
    logical = $0
    # Splice backslash line-continuations into one logical line.
    while (logical ~ /\\[[:space:]]*$/ && (getline nxt) > 0) {
      sub(/\\[[:space:]]*$/, "", logical)
      logical = logical " " nxt
    }
    tmp = logical
    n = gsub(/defined/, "", tmp)   # gsub returns the match count
    if (n > max) {
      printf "%s:%d:1:warning:preprocessor conditional ORs %d defined() terms (limit %d); replace the driver list with an umbrella feature macro such as HAS_TFT or HAS_SCREEN:too-many-defined\n", \
        FILENAME, startln, n, max
    }
  }
' "$@"
