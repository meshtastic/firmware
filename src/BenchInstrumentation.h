#pragma once

// -DMESHTASTIC_BENCH_INSTRUMENTATION=1 turns on the loss bench's diagnostic logging: timing and state lines its analysis
// scripts parse, so their formats must not change. It adds the lines and their cost, and changes no behaviour. Default 0.
// It sets each switch below, which can also be set on its own, and MESHTASTIC_RADIO_CHIP_STATS (DebugConfiguration.h).
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
// RadioLibInterface::rxCounters(), the firmware's receive counts, logged by the DMShell test client for each session
#ifndef MESHTASTIC_BENCH_RX_COUNTERS
#define MESHTASTIC_BENCH_RX_COUNTERS 1
#endif
// At a failed readout, read the chip's packet type, mode and IRQ flags back and log them (LR11x0 and LR2021, with
// RADIOLIB_GODMODE)
#ifndef MESHTASTIC_RX_FAIL_PROBE
#define MESHTASTIC_RX_FAIL_PROBE
#endif
#endif

// Not set here: -DSX126X_STATE_SAMPLER_MS=<n> logs each change in an SX126x's mode and IRQ flags, looking every n ms from
// the main loop, or with -DSX126X_STATE_SAMPLER_TASK from a FreeRTOS task. The looks are SPI traffic of their own, so
// they change the radio timing the other lines measure.
