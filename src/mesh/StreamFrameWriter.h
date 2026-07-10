#pragma once

#include "Stream.h"
#include <cstddef>
#include <cstdint>

/**
 * Continues framed writes across bounded Stream::write() calls.
 *
 * Frame storage is owned by the caller and must remain valid and unchanged
 * until isIdle() returns true. Locking and frame construction are also the
 * caller's responsibility.
 */
class StreamFrameWriter
{
  public:
    /// Start a complete frame, or retain a required frame behind pending output.
    bool writeFrame(Stream &stream, uint8_t *frame, size_t frameLen, bool bestEffort);

    /// Make one bounded attempt to finish pending output.
    bool finishPendingFrame(Stream &stream);

    bool isIdle() const { return pendingFrame == nullptr && deferredFrame == nullptr; }

  private:
    uint8_t *pendingFrame = nullptr;
    size_t pendingFrameLen = 0;
    size_t pendingFrameOffset = 0;
    uint8_t *deferredFrame = nullptr;
    size_t deferredFrameLen = 0;
};
