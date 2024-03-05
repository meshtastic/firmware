#pragma once

#include <memory>
#include <mutex>
#include <queue>

#ifdef BLOCKING_PACKET_QUEUE
#include <condition_variable>
#endif

/**
 * Generic platform independent and re-entrant queue wrapper that can be used to
 * safely pass (generic) movable objects between threads.
 */
template <typename T> class PacketQueue
{
  public:
    PacketQueue() {}

    PacketQueue(PacketQueue const &other) = delete;

    /**
     * Push movable object into queue
     */
    void push(T &&packet)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(packet.move());
#ifdef BLOCKING_PACKET_QUEUE
        cond.notify_one();
#endif
    }

#ifdef BLOCKING_PACKET_QUEUE
    /**
     * Pop movable object from queue (blocking)
     */
    std::unique_ptr<T> pop(void)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this] { return !queue.empty(); });
        T packet = queue.front()->move();
        queue.pop();
        return packet;
    }
#endif

    /**
     * Pop movable object from queue (non-blocking)
     */
    std::unique_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
            return {nullptr};
        auto packet = queue.front()->move();
        queue.pop();
        return packet;
    }

    uint32_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

  private:
    mutable std::mutex mutex;
    std::queue<std::unique_ptr<T>> queue;
#ifdef BLOCKING_PACKET_QUEUE
    std::condition_variable cond;
#endif
};