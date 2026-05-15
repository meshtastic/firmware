#pragma once

/**
 * @file StaticSlotTable.h
 * @brief Predicate-based helper for fixed-size slot arrays.
 */

#include <array>
#include <cstddef>
#include <stdint.h>

/**
 * @brief Wraps an externally owned static array with slot search helpers.
 *
 * StaticSlotTable does not define what makes a record used or free. Callers
 * provide predicates for find() and allocate(), and invalidate() resets a slot
 * by assigning a default-constructed @p Record.
 *
 * @tparam Record Record type stored in the table.
 * @tparam Capacity Number of records in the backing storage.
 *
 * @warning This helper is intended for small tables indexed by uint8_t.
 * findIndex() returns int8_t, so only capacities whose valid indexes fit in
 * int8_t can be represented by that method.
 */
template <typename Record, size_t Capacity> class StaticSlotTable
{
  public:
    /**
     * @brief Backing storage type used by the table.
     */
    using Storage = std::array<Record, Capacity>;

    /**
     * @brief Creates a table wrapper over caller-owned storage.
     *
     * @param storage_ Array that stores all records for the lifetime of this
     * table.
     */
    explicit StaticSlotTable(Storage &storage_) : storage(storage_) {}

    /**
     * @brief Finds the first mutable record accepted by a predicate.
     *
     * @tparam Predicate Callable with signature compatible with
     * `bool(const Record &record)`.
     * @param predicate Predicate used to select a record.
     * @param startIndex First index to inspect.
     * @return Pointer to the matching record, or nullptr when none is found.
     */
    template <typename Predicate> Record *find(Predicate predicate, uint8_t startIndex = 0)
    {
        for (uint8_t i = startIndex; i < Capacity; ++i) {
            if (predicate(storage[i])) {
                return &storage[i];
            }
        }
        return nullptr;
    }

    /**
     * @brief Finds the first const record accepted by a predicate.
     *
     * @tparam Predicate Callable with signature compatible with
     * `bool(const Record &record)`.
     * @param predicate Predicate used to select a record.
     * @param startIndex First index to inspect.
     * @return Pointer to the matching record, or nullptr when none is found.
     */
    template <typename Predicate> const Record *find(Predicate predicate, uint8_t startIndex = 0) const
    {
        for (uint8_t i = startIndex; i < Capacity; ++i) {
            if (predicate(storage[i])) {
                return &storage[i];
            }
        }
        return nullptr;
    }

    /**
     * @brief Finds the index of the first record accepted by a predicate.
     *
     * @tparam Predicate Callable with signature compatible with
     * `bool(const Record &record)`.
     * @param predicate Predicate used to select a record.
     * @param startIndex First index to inspect.
     * @return Matching index, or -1 when none is found.
     */
    template <typename Predicate> int8_t findIndex(Predicate predicate, uint8_t startIndex = 0) const
    {
        for (uint8_t i = startIndex; i < Capacity; ++i) {
            if (predicate(storage[i])) {
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

    /**
     * @brief Returns the first candidate slot accepted by a predicate.
     *
     * allocate() does not mark the returned record as used. The caller should
     * update any used/free fields in @p Record after a non-null return.
     *
     * @tparam Predicate Callable with signature compatible with
     * `bool(const Record &record)`.
     * @param predicate Predicate used to select an allocatable record.
     * @param startIndex First index to inspect.
     * @param slotIndex Optional output receiving the matching index.
     * @return Pointer to the selected record, or nullptr when none is found.
     */
    template <typename Predicate> Record *allocate(Predicate predicate, uint8_t startIndex = 0, uint8_t *slotIndex = nullptr)
    {
        for (uint8_t i = startIndex; i < Capacity; ++i) {
            if (predicate(storage[i])) {
                if (slotIndex) {
                    *slotIndex = i;
                }
                return &storage[i];
            }
        }
        return nullptr;
    }

    /**
     * @brief Returns a mutable record by index.
     *
     * @param index Record index.
     * @return Pointer to the record, or nullptr when @p index is out of range.
     */
    Record *at(uint8_t index)
    {
        return index < Capacity ? &storage[index] : nullptr;
    }

    /**
     * @brief Returns a const record by index.
     *
     * @param index Record index.
     * @return Pointer to the record, or nullptr when @p index is out of range.
     */
    const Record *at(uint8_t index) const
    {
        return index < Capacity ? &storage[index] : nullptr;
    }

    /**
     * @brief Resets one record to its default value.
     *
     * @param index Record index to reset.
     */
    void invalidate(uint8_t index)
    {
        if (index < Capacity) {
            storage[index] = Record{};
        }
    }

    /**
     * @brief Resets every record to its default value.
     */
    void clear()
    {
        for (Record &record : storage) {
            record = Record{};
        }
    }

    /**
     * @brief Returns mutable access to the backing storage.
     *
     * @return Reference to the caller-owned record array.
     */
    Storage &records() { return storage; }

    /**
     * @brief Returns const access to the backing storage.
     *
     * @return Reference to the caller-owned record array.
     */
    const Storage &records() const { return storage; }

  private:
    Storage &storage;
};
