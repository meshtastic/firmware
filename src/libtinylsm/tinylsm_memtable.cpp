#include "tinylsm_memtable.h"
#include "configuration.h"
#include "tinylsm_config.h"

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Memtable Implementation
// ============================================================================

Memtable::Memtable(size_t capacity_kb) : capacity_bytes(capacity_kb * 1024), current_bytes(0), last_flush_time(0)
{
    // Reserve some initial capacity to reduce reallocations
    entries.reserve(capacity_kb / 4); // Rough estimate: avg 256 bytes per entry

    // Initialize last_flush_time to current time (avoid huge "time since flush" on first flush)
    last_flush_time = get_epoch_time();
}

bool Memtable::put(CompositeKey key, const uint8_t *value, size_t value_size)
{
    const size_t max_val_size = 4096; // constants::MAX_VALUE_SIZE
    if (value_size > max_val_size) {
        LOG_ERROR("Value size %zu exceeds maximum %zu", value_size, max_val_size);
        return false;
    }

    // Find insertion position
    size_t pos = find_position(key);

    // Check if key already exists
    if (pos < entries.size() && entries[pos].key == key) {
        // Update existing entry
        size_t old_size = entries[pos].value.size();
        ValueBlob new_value(value, value_size, true);

        // Update size accounting
        current_bytes = current_bytes - old_size + value_size;

        entries[pos].value = std::move(new_value);
        entries[pos].is_tombstone = false;
        return true;
    }

    // Check capacity before insertion
    size_t new_entry_size = sizeof(MemtableEntry) + value_size;
    if (current_bytes + new_entry_size > capacity_bytes) {
        LOG_WARN("Memtable full, cannot insert (current: %zu, capacity: %zu)", current_bytes, capacity_bytes);
        return false;
    }

    // Insert new entry
    ValueBlob blob(value, value_size, true);
    MemtableEntry entry(key, std::move(blob), false);

    entries.insert(entries.begin() + pos, std::move(entry));
    current_bytes += new_entry_size;

    return true;
}

bool Memtable::del(CompositeKey key)
{
    // Find insertion position
    size_t pos = find_position(key);

    // Check if key already exists
    if (pos < entries.size() && entries[pos].key == key) {
        // Mark as tombstone
        entries[pos].is_tombstone = true;
        return true;
    }

    // Insert tombstone
    size_t new_entry_size = sizeof(MemtableEntry);
    if (current_bytes + new_entry_size > capacity_bytes) {
        LOG_WARN("Memtable full, cannot insert tombstone");
        return false;
    }

    ValueBlob empty_blob;
    MemtableEntry entry(key, std::move(empty_blob), true);

    entries.insert(entries.begin() + pos, std::move(entry));
    current_bytes += new_entry_size;

    return true;
}

bool Memtable::get(CompositeKey key, uint8_t **value, size_t *value_size, bool *is_tombstone) const
{
    size_t pos = find_position(key);

    if (pos < entries.size() && entries[pos].key == key) {
        *value = const_cast<uint8_t *>(entries[pos].value.ptr());
        *value_size = entries[pos].value.size();
        *is_tombstone = entries[pos].is_tombstone;
        return true;
    }

    return false;
}

bool Memtable::contains(CompositeKey key) const
{
    size_t pos = find_position(key);
    return pos < entries.size() && entries[pos].key == key;
}

bool Memtable::should_flush(uint32_t interval_sec) const
{
    if (is_empty()) {
        return false;
    }

    uint32_t now = get_epoch_time();
    if (now < last_flush_time) {
        return false; // Clock skew
    }

    return (now - last_flush_time) >= interval_sec;
}

KeyRange Memtable::get_key_range() const
{
    if (entries.empty()) {
        return KeyRange();
    }
    return KeyRange(entries.front().key, entries.back().key);
}

void Memtable::clear()
{
    entries.clear();
    current_bytes = 0;
}

size_t Memtable::find_position(CompositeKey key) const
{
    // Binary search for insertion position
    auto it = std::lower_bound(entries.begin(), entries.end(), key,
                               [](const MemtableEntry &entry, CompositeKey k) { return entry.key < k; });

    return it - entries.begin();
}

} // namespace tinylsm
} // namespace meshtastic
