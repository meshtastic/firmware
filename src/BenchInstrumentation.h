#pragma once

// -DMESHTASTIC_BENCH_INSTRUMENTATION=1 turns on the loss bench's diagnostic logging: timing and state lines its analysis
// scripts parse, so their formats must not change. It adds the lines and their cost, and changes no behaviour. Default 0.
// It sets each switch below, which can also be set on its own.
#ifndef MESHTASTIC_BENCH_INSTRUMENTATION
#define MESHTASTIC_BENCH_INSTRUMENTATION 0
#endif

#if MESHTASTIC_BENCH_INSTRUMENTATION
// Stamp each API log record with the node's millis(), and print it to the ms on the console
#ifndef MESHTASTIC_LOG_RECORD_MILLIS
#define MESHTASTIC_LOG_RECORD_MILLIS
#endif
// Log each thread run that holds the main loop at least this many ms
#ifndef MESHTASTIC_SLOW_THREAD_MS
#define MESHTASTIC_SLOW_THREAD_MS 10
#endif
// Count the console output the port refuses, and report each USB stall of 1 s or more
#ifndef MESHTASTIC_LOG_USB_STATS
#define MESHTASTIC_LOG_USB_STATS
#endif
// Log each window in which the radio cannot hear, as it ends, and its radio edge lines at DEBUG rather than TRACE
#ifndef MESHTASTIC_LOG_RADIO_EDGES
#define MESHTASTIC_LOG_RADIO_EDGES
#endif
// Time each TX from its channel scan to RX being back, and log where it landed on the slot grid
#ifndef MESHTASTIC_TX_TIMELINE
#define MESHTASTIC_TX_TIMELINE
#endif
#endif
