#pragma once

#include "configuration.h"
#include <stddef.h>
#include <stdint.h>

// MemAudit: tiny per-subsystem heap accounting registry.
//
// Subsystems that own a large long-lived allocation report it here under a short
// tag ("nodedb", "pkthist", ...). logBreakdown() then prints one line, e.g.
//   MemAudit[boot]: tmm=2500 warm=4000 pkthist=5824 nodedb=13440 total=25764
// so heap regressions in field reports self-diagnose from the serial log instead
// of needing a hand-built breakdown for every release.
//
// Tags must be string LITERALS (or otherwise immortal strings): the registry
// stores the pointer, compares by pointer first and falls back to strcmp for
// the same text duplicated across translation units.
//
// Concurrency: counters are 32-bit std::atomic accessed with relaxed ordering -
// on ARM Cortex-M aligned 32-bit loads/stores are single instructions and the
// update sites are low-rate, so add() stays a few instructions with no locks
// (the one hot path is the per-packet pool add). Registration claims a table
// slot with a compare-exchange, so first-use racing is safe too. Counts are
// best-effort diagnostics, not exact bookkeeping.
//
// Compiled out (no-op inline stubs, so call sites need no #ifdefs) when
// MESHTASTIC_MEM_AUDIT is 0 - the default on STM32WL, the tightest flash target.
#ifndef MESHTASTIC_MEM_AUDIT
#ifdef ARCH_STM32WL
#define MESHTASTIC_MEM_AUDIT 0
#else
#define MESHTASTIC_MEM_AUDIT 1
#endif
#endif

namespace memaudit
{

// Fixed registry capacity - updates for tags beyond this are dropped (bump if needed).
constexpr size_t kMaxTags = 16;

// One snapshot row, as returned by snapshot().
struct Tag {
    const char *tag; // the literal passed to add()/set()
    int32_t bytes;   // current byte count for that subsystem
};

#if MESHTASTIC_MEM_AUDIT

// Adjust a subsystem's byte count (registers the tag on first use). Safe from
// concurrent threads; this is the form to use on per-object alloc/free paths.
void add(const char *tag, int32_t delta);

// Set a subsystem's byte count outright - for one-shot pool/table allocations
// where the total is known (use 0 on free or allocation failure).
void set(const char *tag, uint32_t bytes);

// Copy up to max registered tags into out; returns the number written.
size_t snapshot(Tag *out, size_t max);

// Log the whole table as a single LOG_INFO line, labeled with `when` ("boot", ...).
void logBreakdown(const char *when);

#else

// No-op stubs so call sites compile away without #ifdefs.
inline void add(const char *, int32_t) {}
inline void set(const char *, uint32_t) {}
inline size_t snapshot(Tag *, size_t)
{
    return 0;
}
inline void logBreakdown(const char *) {}

#endif // MESHTASTIC_MEM_AUDIT

} // namespace memaudit
