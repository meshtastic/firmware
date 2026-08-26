// What bin/platformio-custom.py emits for a userPrefs.jsonc configuring five channels. -include'd
// rather than passed as -D flags, which would overrun the Windows command-line limit.

#pragma once

#define USERPREFS_CHANNELS_TO_WRITE 5

// Every field set, with is_muted and uplink on but downlink off, so a test can tell an applied
// value from a zeroed one.
#define USERPREFS_CHANNEL_0_PSK                                                                                                  \
    {                                                                                                                            \
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16                                                                    \
    }
#define USERPREFS_CHANNEL_0_NAME "Ops"
#define USERPREFS_CHANNEL_0_PRECISION 14
#define USERPREFS_CHANNEL_0_IS_MUTED true
#define USERPREFS_CHANNEL_0_UPLINK_ENABLED true
#define USERPREFS_CHANNEL_0_DOWNLINK_ENABLED false

// Name longer than ChannelSettings.name (char[12]); the switch this table replaces used strcpy().
#define USERPREFS_CHANNEL_2_PSK                                                                                                  \
    {                                                                                                                            \
        1                                                                                                                        \
    }
#define USERPREFS_CHANNEL_2_NAME "LongerThanTwelve"
#define USERPREFS_CHANNEL_2_PRECISION 0
#define USERPREFS_CHANNEL_2_IS_MUTED false
#define USERPREFS_CHANNEL_2_UPLINK_ENABLED false
#define USERPREFS_CHANNEL_2_DOWNLINK_ENABLED false

// The index the old switch could not reach at all.
#define USERPREFS_CHANNEL_3_PSK                                                                                                  \
    {                                                                                                                            \
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1                                                                    \
    }
#define USERPREFS_CHANNEL_3_NAME "Three"
#define USERPREFS_CHANNEL_3_PRECISION 0
#define USERPREFS_CHANNEL_3_IS_MUTED false
#define USERPREFS_CHANNEL_3_UPLINK_ENABLED false
#define USERPREFS_CHANNEL_3_DOWNLINK_ENABLED false

#define USERPREFS_CHANNEL_4_PSK                                                                                                  \
    {                                                                                                                            \
        1                                                                                                                        \
    }
#define USERPREFS_CHANNEL_4_NAME "Four"
#define USERPREFS_CHANNEL_4_PRECISION 0
#define USERPREFS_CHANNEL_4_IS_MUTED false
#define USERPREFS_CHANNEL_4_UPLINK_ENABLED false
#define USERPREFS_CHANNEL_4_DOWNLINK_ENABLED false
