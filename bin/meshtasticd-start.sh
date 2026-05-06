#!/usr/bin/env sh

INSTANCE=$1
CONF_DIR="/etc/meshtasticd/"
VFS_DIR="/var/lib/meshtasticd/${INSTANCE}"
OLD_VFS_DIR="/var/lib/meshtasticd-${INSTANCE}"

# If no instance ID provided, start bare daemon and exit
if [ -z "${INSTANCE}" ]; then
  echo "no instance ID provided, starting bare meshtasticd service"
  /usr/bin/meshtasticd
  exit 0
fi

if [ -d "${OLD_VFS_DIR}" ]; then
  VFS_DIR="${OLD_VFS_DIR}"
  echo "found old vfs dir for ${INSTANCE} at ${OLD_VFS_DIR}, using it as vfs dir for ${INSTANCE}"
fi

# Make VFS dir if it does not exist
if [ ! -d "${VFS_DIR}" ]; then
  echo "vfs for ${INSTANCE} does not exist, creating it."
  mkdir "${VFS_DIR}"
fi

# Abort if config for $INSTANCE does not exist
if [ ! -f "${CONF_DIR}/config-${INSTANCE}.yaml" ]; then
  echo "no config for ${INSTANCE} found in ${CONF_DIR}. refusing to start" >&2
  exit 1
fi

# Start meshtasticd with instance parameters
printf "starting meshtasticd-%s..., ${INSTANCE}"
if /usr/bin/meshtasticd --config="${CONF_DIR}/config-${INSTANCE}.yaml" --fsdir="${VFS_DIR}"; then
  echo "ok"
else
  echo "failed"
fi
