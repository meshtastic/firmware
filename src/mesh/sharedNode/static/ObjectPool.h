#pragma once

/**
 * @file StaticObjectPool.h
 * @brief Fixed-capacity keyed object pool backed by static storage.
 */

#include <array>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

/**
 * @brief Stores keyed objects in preallocated, fixed-size storage.
 *
 * StaticObjectPool avoids heap allocation by reserving storage for @p Capacity
 * instances of @p T. Objects are constructed lazily with placement new when
 * ensure() is called for a new key, and are destroyed by remove() or clear().
 *
 * @tparam T Object type stored in the pool.
 * @tparam Capacity Maximum number of keyed objects that can be stored.
 * @tparam KeyT Key type used to identify slots.
 *
 * @warning The pool does not define a destructor that clears constructed
 * objects. Call clear() or remove() before the pool is destroyed if @p T owns
 * resources that must be released.
 */
template <typename T, size_t Capacity, typename KeyT = uint16_t> class StaticObjectPool
{
  public:
    /**
     * @brief Finds a constructed object by key.
     *
     * @param key Key associated with the object.
     * @return Pointer to the object, or nullptr when the key is not present.
     */
    T *find(const KeyT &key)
    {
        Slot *slot = findSlot(key);
        return (slot && slot->constructed) ? slot->object() : nullptr;
    }

    /**
     * @brief Finds or creates an object for a key.
     *
     * If a slot for @p key already exists, the existing object is returned and
     * @p args are ignored. Otherwise, a free slot is reserved and @p T is
     * constructed in-place using the forwarded arguments.
     *
     * @tparam Args Constructor argument types for @p T.
     * @param key Key associated with the object.
     * @param args Arguments forwarded to the @p T constructor for new objects.
     * @return Pointer to the object, or nullptr when the pool is full.
     */
    template <typename... Args> T *ensure(const KeyT &key, Args &&...args)
    {
        Slot *slot = findSlot(key);
        if (!slot) {
            slot = allocateSlot(key);
        }
        if (!slot) {
            return nullptr;
        }
        if (!slot->constructed) {
            new (&slot->storage) T(std::forward<Args>(args)...);
            slot->constructed = true;
        }
        return slot->object();
    }

    /**
     * @brief Removes an object and reports it to a visitor before destruction.
     *
     * The visitor is called only when the slot contains a constructed object.
     * After removal the slot becomes available for another key.
     *
     * @tparam Visitor Callable with signature compatible with
     * `void(T *object, const KeyT &key)`.
     * @param key Key associated with the object to remove.
     * @param visitor Callable invoked before the object destructor.
     * @return true if a slot for @p key existed, false otherwise.
     */
    template <typename Visitor> bool remove(const KeyT &key, Visitor visitor)
    {
        Slot *slot = findSlot(key);
        if (!slot) {
            return false;
        }

        if (slot->constructed) {
            visitor(slot->object(), slot->key);
            slot->object()->~T();
        }

        *slot = Slot{};
        return true;
    }

    /**
     * @brief Removes an object without a visitor callback.
     *
     * @param key Key associated with the object to remove.
     * @return true if a slot for @p key existed, false otherwise.
     */
    bool remove(const KeyT &key)
    {
        return remove(key, NoOpVisitor{});
    }

    /**
     * @brief Destroys all constructed objects and frees every slot.
     *
     * The visitor is called once for each constructed object before that object
     * is destroyed.
     *
     * @tparam Visitor Callable with signature compatible with
     * `void(T *object, const KeyT &key)`.
     * @param visitor Callable invoked before each object destructor.
     */
    template <typename Visitor> void clear(Visitor visitor)
    {
        for (Slot &slot : slots) {
            if (slot.used && slot.constructed) {
                visitor(slot.object(), slot.key);
                slot.object()->~T();
            }
            slot = Slot{};
        }
    }

    /**
     * @brief Destroys all constructed objects and frees every slot.
     */
    void clear()
    {
        clear(NoOpVisitor{});
    }

  private:
    /**
     * @brief Visitor used when the caller does not need a removal callback.
     */
    struct NoOpVisitor {
        void operator()(T *, const KeyT &) const {}
    };

    /**
     * @brief Single keyed storage cell in the pool.
     */
    struct Slot {
        bool used = false;
        KeyT key{};
        bool constructed = false;
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;

        /**
         * @brief Returns the storage interpreted as a @p T pointer.
         *
         * @return Pointer to the object storage.
         */
        T *object() { return reinterpret_cast<T *>(&storage); }
    };

    /**
     * @brief Finds the slot associated with a key.
     *
     * @param key Key to search for.
     * @return Matching slot, or nullptr when the key is not present.
     */
    Slot *findSlot(const KeyT &key)
    {
        for (Slot &slot : slots) {
            if (slot.used && slot.key == key) {
                return &slot;
            }
        }
        return nullptr;
    }

    /**
     * @brief Reserves the first free slot for a key.
     *
     * @param key Key to assign to the reserved slot.
     * @return Reserved slot, or nullptr when the pool is full.
     */
    Slot *allocateSlot(const KeyT &key)
    {
        for (Slot &slot : slots) {
            if (!slot.used) {
                slot.used = true;
                slot.key = key;
                return &slot;
            }
        }
        return nullptr;
    }

    std::array<Slot, Capacity> slots{};
};
