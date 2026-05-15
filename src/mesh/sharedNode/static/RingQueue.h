#pragma once

/**
 * @file StaticRingQueue.h
 * @brief Fixed-capacity FIFO queue with selectable overflow behavior.
 */

#include <array>
#include <cstddef>

/**
 * @brief Overflow policy that rejects a push when the queue is full.
 */
struct RejectNewest {
    /**
     * @brief Handles a full queue before inserting a new value.
     *
     * @return false to reject the new value.
     */
    static bool onFull(size_t &, size_t &, size_t) { return false; }
};

/**
 * @brief Overflow policy that removes the oldest item to make room.
 */
struct DropOldest {
    /**
     * @brief Drops the current front item when the queue is full.
     *
     * @param head Current queue head index, advanced when an item is dropped.
     * @param count Current number of queued items, decremented on success.
     * @param capacity Maximum queue capacity.
     * @return true if space was made for the new value.
     */
    static bool onFull(size_t &head, size_t &count, size_t capacity)
    {
        if (capacity == 0 || count == 0) {
            return false;
        }
        head = (head + 1U) % capacity;
        count--;
        return true;
    }
};

/**
 * @brief Static FIFO queue implemented as a ring buffer.
 *
 * StaticRingQueue stores up to @p Capacity values without dynamic allocation.
 * When full(), push() delegates to @p OverflowPolicy to decide whether the new
 * value is rejected or older data is discarded.
 *
 * @tparam T Value type stored by the queue.
 * @tparam Capacity Maximum number of elements in the queue.
 * @tparam OverflowPolicy Policy type with
 * `static bool onFull(size_t &head, size_t &count, size_t capacity)`.
 */
template <typename T, size_t Capacity, typename OverflowPolicy = RejectNewest> class StaticRingQueue
{
  public:
    /**
     * @brief Adds a value to the back of the queue.
     *
     * @param value Value to copy into the queue.
     * @return true if the value was queued, false if it was rejected.
     */
    bool push(const T &value)
    {
        if (Capacity == 0) {
            return false;
        }

        if (full() && !OverflowPolicy::onFull(head, count, Capacity)) {
            return false;
        }

        const size_t tail = (head + count) % Capacity;
        items[tail] = value;
        count++;
        return true;
    }

    /**
     * @brief Removes the oldest value from the queue.
     *
     * @param value Destination updated with the removed value.
     * @return true if a value was removed, false if the queue was empty.
     */
    bool pop(T &value)
    {
        if (empty()) {
            return false;
        }

        value = items[head];
        head = (head + 1U) % Capacity;
        count--;
        return true;
    }

    /**
     * @brief Returns the oldest queued value without removing it.
     *
     * @return Pointer to the front value, or nullptr when the queue is empty.
     */
    T *front()
    {
        return empty() ? nullptr : &items[head];
    }

    /**
     * @brief Returns the oldest queued value without removing it.
     *
     * @return Pointer to the front value, or nullptr when the queue is empty.
     */
    const T *front() const
    {
        return empty() ? nullptr : &items[head];
    }

    /**
     * @brief Returns the current number of queued values.
     *
     * @return Number of values in the queue.
     */
    size_t size() const { return count; }

    /**
     * @brief Returns the compile-time queue capacity.
     *
     * @return Maximum number of values the queue can hold.
     */
    static constexpr size_t capacity() { return Capacity; }

    /**
     * @brief Checks whether the queue currently has no free slots.
     *
     * @return true when the queue contains @p Capacity values.
     */
    bool full() const { return count >= Capacity; }

    /**
     * @brief Checks whether the queue has no queued values.
     *
     * @return true when size() is zero.
     */
    bool empty() const { return count == 0; }

    /**
     * @brief Removes all queued values.
     *
     * Stored objects remain assigned in the backing array, but are no longer
     * considered queued.
     */
    void clear()
    {
        head = 0;
        count = 0;
    }

  private:
    std::array<T, Capacity> items{};
    size_t head = 0;
    size_t count = 0;
};
