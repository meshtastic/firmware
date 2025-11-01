#pragma once

#include "tinylsm_types.h"
#include "tinylsm_utils.h"
#include <algorithm>
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Memtable Entry
// ============================================================================

struct MemtableEntry {
    CompositeKey key;
    ValueBlob value;
    bool is_tombstone; // True if this is a deletion marker

    MemtableEntry() : key(), value(), is_tombstone(false) {}
    MemtableEntry(CompositeKey k, ValueBlob &&v, bool tombstone = false) : key(k), value(std::move(v)), is_tombstone(tombstone) {}

    // Move support
    MemtableEntry(MemtableEntry &&other) noexcept
        : key(other.key), value(std::move(other.value)), is_tombstone(other.is_tombstone)
    {
    }

    MemtableEntry &operator=(MemtableEntry &&other) noexcept
    {
        if (this != &other) {
            key = other.key;
            value = std::move(other.value);
            is_tombstone = other.is_tombstone;
        }
        return *this;
    }

    // Disable copy
    MemtableEntry(const MemtableEntry &) = delete;
    MemtableEntry &operator=(const MemtableEntry &) = delete;
};

// ============================================================================
// Memtable (Sorted Vector)
// ============================================================================

class Memtable
{
  private:
    std::vector<MemtableEntry> entries;
    size_t capacity_bytes;
    size_t current_bytes;
    uint32_t last_flush_time;

  public:
    Memtable(size_t capacity_kb);

    // Insert or update entry
    bool put(CompositeKey key, const uint8_t *value, size_t value_size);

    // Insert tombstone (deletion marker)
    bool del(CompositeKey key);

    // Lookup entry
    bool get(CompositeKey key, uint8_t **value, size_t *value_size, bool *is_tombstone) const;

    // Check if key exists
    bool contains(CompositeKey key) const;

    // Size and capacity
    size_t size_bytes() const { return current_bytes; }
    size_t size_entries() const { return entries.size(); }
    size_t capacity() const { return capacity_bytes; }
    bool is_full() const { return current_bytes >= capacity_bytes; }
    bool is_empty() const { return entries.empty(); }

    // Flush timing
    void set_last_flush_time(uint32_t time) { last_flush_time = time; }
    uint32_t get_last_flush_time() const { return last_flush_time; }
    bool should_flush(uint32_t interval_sec) const;

    // Range query (for compaction/flush)
    struct Iterator {
        const Memtable *table;
        size_t index;

        Iterator(const Memtable *t, size_t i) : table(t), index(i) {}

        bool valid() const { return index < table->entries.size(); }
        void next() { ++index; }
        CompositeKey key() const { return table->entries[index].key; }
        const uint8_t *value() const { return table->entries[index].value.ptr(); }
        size_t value_size() const { return table->entries[index].value.size(); }
        bool is_tombstone() const { return table->entries[index].is_tombstone; }
    };

    Iterator begin() const { return Iterator(this, 0); }
    Iterator end() const { return Iterator(this, entries.size()); }

    // Get key range
    KeyRange get_key_range() const;

    // Clear all entries
    void clear();

  private:
    // Binary search for key position
    size_t find_position(CompositeKey key) const;
};

} // namespace tinylsm
} // namespace meshtastic
