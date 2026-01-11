#!/usr/bin/env sh

INSTANCE=$1
CONF_DIR="/etc/meshtasticd/config.d"
VFS_DIR="/var/lib"

# If no instance ID provided, start bare daemon and exit
echo "no instance ID provided, starting bare meshtasticd service"
if [ -z "${INSTANCE}" ]; then
  /usr/bin/meshtasticd
  exit 0
fi

# Make VFS dir if it does not exist
if [ ! -d "${VFS_DIR}/meshtasticd-${INSTANCE}" ]; then
  echo "vfs for ${INSTANCE} does not exist, creating it."
  mkdir "${VFS_DIR}/meshtasticd-${INSTANCE}"
fi

# Abort if config for $INSTANCE does not exist
if [ ! -f "${CONF_DIR}/config-${INSTANCE}.yaml" ]; then
  echo "no config for ${INSTANCE} found in ${CONF_DIR}. refusing to start" >&2
  exit 1
fi

# Start meshtasticd with instance parameters
printf "starting meshtasticd-%s..., ${INSTANCE}"
if /usr/bin/meshtasticd --config="${CONF_DIR}/config-${INSTANCE}.yaml" --fsdir="${VFS_DIR}/meshtasticd-${INSTANCE}"; then
  echo "ok"
else
  echo "failed"
fi
