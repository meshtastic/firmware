// Channel 0 as [env:coverage-event-policy] configures it. initDefaultChannel() applies a configured
// index as a whole, so a build setting the macros by hand supplies every field, as the generator does.

#pragma once

#define USERPREFS_CHANNEL_0_PSK                                                                                                  \
    {                                                                                                                            \
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f                           \
    }
#define USERPREFS_CHANNEL_0_NAME ""
#define USERPREFS_CHANNEL_0_PRECISION 0
#define USERPREFS_CHANNEL_0_IS_MUTED false
#define USERPREFS_CHANNEL_0_UPLINK_ENABLED false
#define USERPREFS_CHANNEL_0_DOWNLINK_ENABLED false
