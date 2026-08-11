#pragma once

namespace graphics::MessageRenderer
{
inline bool shouldShowIncomingMessageBanner(bool messageFrameShown, bool isAlert, bool suppressed)
{
    return !suppressed && (!messageFrameShown || isAlert);
}
} // namespace graphics::MessageRenderer
