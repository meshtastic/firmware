#include "MemAudit.h"

#if MESHTASTIC_MEM_AUDIT

#include "DebugConfiguration.h"
#include <atomic>
#include <stdio.h>
#include <string.h>

namespace memaudit
{

namespace
{

struct Entry {
    std::atomic<const char *> tag; // registered literal; nullptr = free slot
    std::atomic<int32_t> bytes;
};

// Static storage only - the accounting registry must never itself allocate.
// Zero-initialized (BSS), so it is usable from constructors of static objects.
Entry table[kMaxTags];

// Find the slot for a tag, registering it on first use.
// Returns nullptr for a null tag or when the table is full (update dropped).
Entry *findOrRegister(const char *tag)
{
    if (!tag)
        return nullptr;

    // Fast path: same literal, pointer compare only. This is all the hot
    // per-packet add() ever executes once the tag is registered.
    size_t used = 0;
    for (; used < kMaxTags; used++) {
        const char *cur = table[used].tag.load(std::memory_order_acquire);
        if (!cur)
            break; // slots fill in order - first empty slot ends the table
        if (cur == tag)
            return &table[used];
    }

    // Slow path: same text from a different literal (duplicated across
    // translation units, so not pointer-identical).
    for (size_t i = 0; i < used; i++) {
        if (strcmp(table[i].tag.load(std::memory_order_relaxed), tag) == 0)
            return &table[i];
    }

    // First use: claim a free slot. compare_exchange keeps a registration race
    // from double-claiming; the loser re-checks what the winner wrote.
    for (size_t i = used; i < kMaxTags; i++) {
        const char *expected = nullptr;
        if (table[i].tag.compare_exchange_strong(expected, tag, std::memory_order_acq_rel))
            return &table[i];
        if (expected == tag || strcmp(expected, tag) == 0)
            return &table[i];
    }

    return nullptr; // table full - bump kMaxTags if this ever happens
}

} // namespace

void add(const char *tag, int32_t delta)
{
    Entry *e = findOrRegister(tag);
    if (e)
        e->bytes.fetch_add(delta, std::memory_order_relaxed);
}

void set(const char *tag, uint32_t bytes)
{
    Entry *e = findOrRegister(tag);
    if (e)
        e->bytes.store((int32_t)bytes, std::memory_order_relaxed);
}

size_t snapshot(Tag *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < kMaxTags && n < max; i++) {
        const char *tag = table[i].tag.load(std::memory_order_acquire);
        if (!tag)
            break;
        out[n].tag = tag;
        out[n].bytes = table[i].bytes.load(std::memory_order_relaxed);
        n++;
    }
    return n;
}

void logBreakdown(const char *when)
{
    Tag rows[kMaxTags];
    size_t n = snapshot(rows, kMaxTags);
    if (n == 0)
        return;

    // Worst case per row: 16-char tag + '=' + "-2147483648" + ' ' = 29 bytes.
    char line[kMaxTags * 30 + 1];
    size_t pos = 0;
    int32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        int written = snprintf(line + pos, sizeof(line) - pos, "%s%s=%ld", pos ? " " : "", rows[i].tag, (long)rows[i].bytes);
        if (written < 0 || pos + written >= sizeof(line))
            break;
        pos += written;
        total += rows[i].bytes;
    }
    LOG_INFO("MemAudit[%s]: %s total=%ld", when ? when : "?", line, (long)total);
}

} // namespace memaudit

#endif // MESHTASTIC_MEM_AUDIT
