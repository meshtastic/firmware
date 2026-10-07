#pragma once

#include "configuration.h"

#if HAS_HOST_POWEROFF

/**
 * Power off the machine meshtasticd is running on, via systemd-logind.
 *
 * Unlike every other platform's shutdown, a portduino node is a process on a computer that
 * outlives it: Power.cpp's shutdown() calls exit(), and a unit with Restart=always simply brings
 * it back. This asks logind to take the whole host down instead.
 *
 * Returns false if the call could not be made or was refused - most often because polkit did not
 * authorise it. meshtasticd runs unprivileged and with no session, so logind's default
 * (allow_any: auth_admin_keep) denies it; bin/polkit-1/meshtasticd.rules grants just this action
 * to just this user. Callers should keep running on false rather than assume the host is going
 * away.
 */
bool linuxPowerOffHost();

/**
 * Set when the user has asked for the host to go down, so shutdown() halts the machine instead of
 * just exiting the process. Raised before the normal shutdown sequence is triggered, so the node
 * still saves its NodeDB, message store and waypoints on the way out.
 */
extern bool hostPowerOffRequested;

#endif
