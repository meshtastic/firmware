#pragma once

namespace meshtastic
{

/// Dumps out which core we are running on, and min level of remaining stack
/// seen.
void printThreadInfo(const char *extra);

} // namespace meshtastic
