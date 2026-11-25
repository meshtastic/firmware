#!/usr/bin/env bash
#
# Install one environment's dependencies, retrying a fetch that times out.
#
# PlatformIO 6.2.0 hard-codes a 10s connect timeout that no setting changes, and it is spent on the
# TLS handshake - so a slow handshake fails the install, and with set -e the whole build, before
# anything is compiled. See the commit message for the full mechanism.

ENV_NAME=$1
ATTEMPTS=${PIO_PKG_INSTALL_ATTEMPTS:-5}

if [[ -z "$ENV_NAME" ]]; then
  echo "usage: $0 <platformio-environment>" >&2
  exit 2
fi

# Without this, a bad override makes seq yield nothing, the loop never runs, and the script falls
# off the end reporting success while having installed nothing.
if [[ ! $ATTEMPTS =~ ^[1-9][0-9]*$ ]]; then
  echo "PIO_PKG_INSTALL_ATTEMPTS must be a positive integer, got '$ATTEMPTS'" >&2
  exit 2
fi

for attempt in $(seq 1 "$ATTEMPTS"); do
  if pio pkg install -e "$ENV_NAME"; then
    exit 0
  fi
  if [[ "$attempt" -ge "$ATTEMPTS" ]]; then
    echo "pio pkg install failed $ATTEMPTS times for $ENV_NAME; giving up" >&2
    exit 1
  fi
  delay=$((attempt * 5))
  echo "pio pkg install attempt $attempt of $ATTEMPTS failed for $ENV_NAME; retrying in ${delay}s" >&2
  sleep "$delay"
done
