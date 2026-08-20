#include "configuration.h"

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)

#include "TouchTargetRegistry.h"

#include <algorithm>

namespace meshtastic {

TouchTargetRegistry::TouchTargetRegistry(uint16_t width, uint16_t height) : width(width), height(height) {}

bool TouchTargetRegistry::clip(TouchRect &rect) const
{
    rect.left = std::max<int16_t>(0, rect.left);
    rect.top = std::max<int16_t>(0, rect.top);
    rect.right = std::min<int16_t>(static_cast<int16_t>(width), rect.right);
    rect.bottom = std::min<int16_t>(static_cast<int16_t>(height), rect.bottom);
    return rect.right > rect.left && rect.bottom > rect.top;
}

void TouchTargetRegistry::beginFrame(uint32_t pageGeneration)
{
    stagingGeneration = pageGeneration;
    stagingCount = 0;
    stagingMapped = false;
}

void TouchTargetRegistry::clear()
{
    stagingCount = 0;
    stagingMapped = false;
}

void TouchTargetRegistry::markFrameMapped()
{
    stagingMapped = true;
}

bool TouchTargetRegistry::addFullScreen(input_broker_event tapAction, input_broker_event longPressAction)
{
    return add({0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height)}, TouchTargetKind::LegacyFallback, 0,
               tapAction, longPressAction);
}

bool TouchTargetRegistry::add(TouchRect rect, TouchTargetKind kind, uint32_t value, input_broker_event tapAction,
                              input_broker_event longPressAction)
{
    if (stagingCount >= MAX_TARGETS || !clip(rect))
        return false;

    stagingTargets[stagingCount] = {rect, kind, value, tapAction, longPressAction, stagingGeneration};
    ++stagingCount;
    stagingMapped = true;
    return true;
}

void TouchTargetRegistry::publishFrame()
{
    concurrency::LockGuard guard(&lock);

    TouchTarget capturedTarget{};
    bool preserveCapture = false;
    if (captureValid && captured >= 0 && captured < activeCount && activeGeneration == stagingGeneration) {
        capturedTarget = activeTargets[captured];
        preserveCapture = true;
    }

    std::copy(stagingTargets, stagingTargets + stagingCount, activeTargets);
    activeCount = stagingCount;
    activeGeneration = stagingGeneration;
    activeMapped = stagingMapped;
    captured = -1;
    captureValid = false;

    if (preserveCapture) {
        for (uint8_t index = 0; index < activeCount; index++) {
            if (activeTargets[index].kind == capturedTarget.kind && activeTargets[index].value == capturedTarget.value) {
                captured = static_cast<int8_t>(index);
                captureValid = true;
                break;
            }
        }
    }
}

bool TouchTargetRegistry::hasTargets() const
{
    concurrency::LockGuard guard(&lock);
    return activeCount > 0;
}

bool TouchTargetRegistry::isStagingFrameMapped() const
{
    return stagingMapped;
}

bool TouchTargetRegistry::isFrameMapped() const
{
    concurrency::LockGuard guard(&lock);
    return activeMapped;
}

bool TouchTargetRegistry::hitTestLocked(int16_t x, int16_t y, uint8_t &index) const
{
    int bestIndex = -1;
    for (int current = activeCount - 1; current >= 0; current--) {
        const TouchTarget &candidate = activeTargets[current];
        if (candidate.pageGeneration != activeGeneration || !candidate.rect.contains(x, y))
            continue;

        // Registration order remains the priority between different target kinds
        // (for example, a notification option over the page underneath it). For
        // adjacent targets of the same kind, use the nearest center to avoid
        // selecting a neighboring row when expanded hit boxes overlap.
        if (bestIndex < 0) {
            bestIndex = current;
            continue;
        }
        if (candidate.kind != activeTargets[bestIndex].kind)
            continue;

        const int32_t candidateCenterX = static_cast<int32_t>(candidate.rect.left) + candidate.rect.right;
        const int32_t candidateCenterY = static_cast<int32_t>(candidate.rect.top) + candidate.rect.bottom;
        const int32_t bestCenterX = static_cast<int32_t>(activeTargets[bestIndex].rect.left) +
                                    activeTargets[bestIndex].rect.right;
        const int32_t bestCenterY = static_cast<int32_t>(activeTargets[bestIndex].rect.top) +
                                    activeTargets[bestIndex].rect.bottom;
        const int32_t candidateDx = (static_cast<int32_t>(x) * 2) - candidateCenterX;
        const int32_t candidateDy = (static_cast<int32_t>(y) * 2) - candidateCenterY;
        const int32_t bestDx = (static_cast<int32_t>(x) * 2) - bestCenterX;
        const int32_t bestDy = (static_cast<int32_t>(y) * 2) - bestCenterY;
        const uint32_t candidateDistance = static_cast<uint32_t>(candidateDx * candidateDx + candidateDy * candidateDy);
        const uint32_t currentBestDistance = static_cast<uint32_t>(bestDx * bestDx + bestDy * bestDy);
        if (candidateDistance < currentBestDistance) {
            bestIndex = current;
        }
    }
    if (bestIndex < 0)
        return false;
    index = static_cast<uint8_t>(bestIndex);
    return true;
}

bool TouchTargetRegistry::hitTest(int16_t x, int16_t y, TouchTarget &out) const
{
    concurrency::LockGuard guard(&lock);
    uint8_t index = 0;
    if (!hitTestLocked(x, y, index))
        return false;
    out = activeTargets[index];
    return true;
}

bool TouchTargetRegistry::capture(int16_t x, int16_t y)
{
    concurrency::LockGuard guard(&lock);
    uint8_t index = 0;
    if (!hitTestLocked(x, y, index))
        return false;
    captured = static_cast<int8_t>(index);
    captureValid = true;
    return true;
}

bool TouchTargetRegistry::updateCapture(int16_t x, int16_t y)
{
    concurrency::LockGuard guard(&lock);
    if (captured < 0 || captured >= activeCount || !captureValid)
        return false;
    captureValid = activeTargets[captured].rect.contains(x, y);
    return captureValid;
}

bool TouchTargetRegistry::release(int16_t x, int16_t y, bool longPress, TouchTarget &out)
{
    concurrency::LockGuard guard(&lock);
    bool result = false;
    if (captured >= 0 && captured < activeCount && captureValid && activeTargets[captured].rect.contains(x, y)) {
        const input_broker_event action = longPress ? activeTargets[captured].longPressAction : activeTargets[captured].tapAction;
        if (action != INPUT_BROKER_NONE || activeTargets[captured].kind != TouchTargetKind::None) {
            out = activeTargets[captured];
            result = true;
        }
    }
    captured = -1;
    captureValid = false;
    return result;
}

void TouchTargetRegistry::cancelCapture()
{
    concurrency::LockGuard guard(&lock);
    captured = -1;
    captureValid = false;
}

} // namespace meshtastic

#endif // T_DECK_MAX || _VARIANT_T_DECK_PRO_V1_1
