#include "StreamFrameWriter.h"

#include <cassert>

/// Start a frame or retain required output behind an incomplete frame.
bool StreamFrameWriter::writeFrame(Stream &stream, uint8_t *frame, size_t frameLen, bool bestEffort)
{
    if (!finishPendingFrame(stream)) {
        // Single required-frame producer invariant: writeStream() is gated on
        // finishPendingFrame(), so a second required frame can never arrive here.
        assert(bestEffort || !deferredFrame);

        // Preserve required output that was encoded while another frame tail
        // was pending. Best-effort output is dropped instead.
        if (!bestEffort && !deferredFrame) {
            deferredFrame = frame;
            deferredFrameLen = frameLen;
        }
        return false;
    }

    // Never start best-effort output unless its complete frame fits.
    if (bestEffort && stream.availableForWrite() < (int)frameLen)
        return false;

    size_t written = stream.write(frame, frameLen);
    if (written == frameLen)
        return true;

    pendingFrame = frame;
    pendingFrameLen = frameLen;
    pendingFrameOffset = written;
    return false;
}

/// Continue the pending tail with at most one Stream::write() call.
bool StreamFrameWriter::finishPendingFrame(Stream &stream)
{
    if (!pendingFrame)
        return true;

    size_t remaining = pendingFrameLen - pendingFrameOffset;
    size_t written = stream.write(pendingFrame + pendingFrameOffset, remaining);
    if (written > remaining)
        written = remaining;
    pendingFrameOffset += written;

    if (pendingFrameOffset < pendingFrameLen)
        return false;

    pendingFrame = nullptr;
    pendingFrameLen = 0;
    pendingFrameOffset = 0;

    // Promote required output without starting another write in this call.
    if (deferredFrame) {
        pendingFrame = deferredFrame;
        pendingFrameLen = deferredFrameLen;
        deferredFrame = nullptr;
        deferredFrameLen = 0;
        return false;
    }

    return true;
}
