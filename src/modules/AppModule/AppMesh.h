#pragma once

#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <deque>
#include <string>

// Maximum queued mesh events awaiting handler processing
static constexpr size_t APP_MESH_EVENT_QUEUE_MAX = 16;

// Maximum queued state-change notifications awaiting UI delivery
static constexpr size_t APP_STATE_NOTIFY_QUEUE_MAX = 32;

// A key-value pair for state-change notifications
struct AppStateNotification {
    std::string slug;
    std::string key;
    std::string value;
};

// Thread-safe bounded string queue.
// Used for mesh event JSON strings and state-change notifications.
class AppEventQueue
{
  public:
    void push(const std::string &item, size_t maxSize = APP_MESH_EVENT_QUEUE_MAX)
    {
        concurrency::LockGuard guard(&lock);
        if (queue.size() >= maxSize)
            queue.pop_front(); // drop oldest
        queue.push_back(item);
    }

    bool pop(std::string &out)
    {
        concurrency::LockGuard guard(&lock);
        if (queue.empty())
            return false;
        out = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    size_t count()
    {
        concurrency::LockGuard guard(&lock);
        return queue.size();
    }

    void clear()
    {
        concurrency::LockGuard guard(&lock);
        queue.clear();
    }

  private:
    std::deque<std::string> queue;
    concurrency::Lock lock;
};

// Thread-safe bounded queue for state-change notifications
class AppStateNotifyQueue
{
  public:
    void push(const std::string &slug, const std::string &key, const std::string &value)
    {
        concurrency::LockGuard guard(&lock);
        if (queue.size() >= APP_STATE_NOTIFY_QUEUE_MAX)
            queue.pop_front();
        queue.push_back({slug, key, value});
    }

    bool pop(AppStateNotification &out)
    {
        concurrency::LockGuard guard(&lock);
        if (queue.empty())
            return false;
        out = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    void clear()
    {
        concurrency::LockGuard guard(&lock);
        queue.clear();
    }

  private:
    std::deque<AppStateNotification> queue;
    concurrency::Lock lock;
};

// Resolve a mesh-receive permission suffix to a portnum.
// Accepts friendly names ("text", "position", etc.) or numeric strings ("67").
// Returns -1 for unrecognized names or blocked ports (routing=5, admin=6).
int resolveReceivePortnum(const std::string &suffix);

// Serialize mesh packet data to JSON string for the handler Berry VM.
// Returns empty string if the portnum is not supported or decode fails.
std::string serializeMeshEvent(const meshtastic_MeshPacket &mp);

// Escape a string for safe inclusion in JSON values
std::string jsonEscapeString(const char *s, size_t len);
