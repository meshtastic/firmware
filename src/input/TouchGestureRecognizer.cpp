#include "configuration.h"

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)

#include "TouchGestureRecognizer.h"

#include <algorithm>
#include <cmath>

namespace meshtastic {

TouchGestureRecognizer::TouchGestureRecognizer(uint16_t width, uint16_t height) : width(width), height(height) {}

int16_t TouchGestureRecognizer::median(const int16_t *values, uint8_t count)
{
    int16_t sorted[FILTER_SAMPLES] = {};
    for (uint8_t i = 0; i < count; ++i)
        sorted[i] = values[i];
    std::sort(sorted, sorted + count);
    if ((count & 1U) != 0)
        return sorted[count / 2];
    return static_cast<int16_t>((static_cast<int32_t>(sorted[count / 2 - 1]) + sorted[count / 2]) / 2);
}

bool TouchGestureRecognizer::acceptSample(int16_t rawX, int16_t rawY, int16_t &filteredX, int16_t &filteredY)
{
    if (rawX < 0 || rawY < 0 || rawX >= static_cast<int16_t>(width) || rawY >= static_cast<int16_t>(height))
        return false;

    if (pendingJump) {
        const int16_t confirmX = static_cast<int16_t>(rawX - pendingX);
        const int16_t confirmY = static_cast<int16_t>(rawY - pendingY);
        if (std::abs(confirmX) <= 8 && std::abs(confirmY) <= 8) {
            pendingJump = false;
            confirmedJump = true;
        } else {
            pendingJump = false;
            rejectedJump = true;
        }
    }
    if (haveRaw && !confirmedJump) {
        const int16_t dx = static_cast<int16_t>(rawX - lastRawX);
        const int16_t dy = static_cast<int16_t>(rawY - lastRawY);
        if (std::abs(dx) > MAX_JUMP || std::abs(dy) > MAX_JUMP) {
            pendingJump = true;
            pendingX = rawX;
            pendingY = rawY;
            return false;
        }
    }
    confirmedJump = false;

    haveRaw = true;
    lastRawX = rawX;
    lastRawY = rawY;
    filterX[filterIndex] = rawX;
    filterY[filterIndex] = rawY;
    filterIndex = static_cast<uint8_t>((filterIndex + 1) % FILTER_SAMPLES);
    if (filterCount < FILTER_SAMPLES)
        ++filterCount;

    const int16_t medianX = median(filterX, filterCount);
    const int16_t medianY = median(filterY, filterCount);
    if (!haveFiltered) {
        filteredX = medianX;
        filteredY = medianY;
    } else {
        filteredX = static_cast<int16_t>((static_cast<int32_t>(lastFilteredX) * 3 + medianX) / 4);
        filteredY = static_cast<int16_t>((static_cast<int32_t>(lastFilteredY) * 3 + medianY) / 4);
    }
    lastFilteredX = filteredX;
    lastFilteredY = filteredY;
    haveFiltered = true;
    return true;
}

TouchGesture TouchGestureRecognizer::directionFor(int16_t dx, int16_t dy) const
{
    const int16_t ax = static_cast<int16_t>(std::abs(dx));
    const int16_t ay = static_cast<int16_t>(std::abs(dy));
    if (ax == 0 && ay == 0)
        return TouchGesture::None;
    if (ax >= ay)
        return dx < 0 ? TouchGesture::SwipeLeft : TouchGesture::SwipeRight;
    return dy < 0 ? TouchGesture::SwipeUp : TouchGesture::SwipeDown;
}

bool TouchGestureRecognizer::lockDirection(int16_t dx, int16_t dy)
{
    const int16_t ax = static_cast<int16_t>(std::abs(dx));
    const int16_t ay = static_cast<int16_t>(std::abs(dy));
    const int16_t dominant = std::max(ax, ay);
    const int16_t other = std::min(ax, ay);
    if (dominant < LOCK_DISTANCE)
        return false;
    if (other != 0 && (dominant * 100 < (dominant + other) * 72 || dominant < static_cast<int16_t>(other * 1.4f)))
        return false;
    directionLocked = true;
    lockedGesture = directionFor(dx, dy);
    return true;
}

void TouchGestureRecognizer::emit(TouchGesture gesture, uint32_t duration, TouchGestureEvent &event)
{
    event.gesture = gesture;
    event.x = lastX;
    event.y = lastY;
    event.duration = duration;
}

bool TouchGestureRecognizer::update(const TouchSample &sample, TouchGestureEvent &event, bool allowLongPress)
{
    event = {};
    if (!sample.touched) {
        if (pendingJump || rejectedJump) {
            reset();
            return false;
        }
        if (state == State::Tracking) {
            const uint32_t duration = sample.timestamp - startTime;
            if (stableSamples >= STABLE_SAMPLES_REQUIRED && !longPressRejected && !directionLocked &&
                !pressMoved &&
                (!allowLongPress || duration < LONG_PRESS_MS)) {
                emit(TouchGesture::Tap, duration, event);
                reset();
                return true;
            }
            const bool horizontal = lockedGesture == TouchGesture::SwipeLeft || lockedGesture == TouchGesture::SwipeRight;
            const int16_t peakAxisDistance = horizontal ? maxRawAbsX : maxRawAbsY;
            if (directionLocked && peakAxisDistance >= SWIPE_DISTANCE) {
                emit(lockedGesture, duration, event);
                reset();
                return true;
            }
        }
        reset();
        return false;
    }

    int16_t x = 0;
    int16_t y = 0;
    if (!acceptSample(sample.x, sample.y, x, y))
        return false;

    if (state == State::Idle) {
        state = State::Tracking;
        startTime = sample.timestamp;
        startRawX = sample.x;
        startRawY = sample.y;
        lastX = x;
        lastY = y;
        stableSamples = 1;
        return false;
    }
    if (state == State::Cancelled || state == State::LongPressed) {
        lastX = x;
        lastY = y;
        return false;
    }

    if (stableSamples < STABLE_SAMPLES_REQUIRED)
        ++stableSamples;

    // Keep the gesture sum in the filtered coordinate space. A single noisy
    // raw sample therefore cannot turn a stable press into a move.
    const int16_t filteredDx = static_cast<int16_t>(x - lastX);
    const int16_t filteredDy = static_cast<int16_t>(y - lastY);
    gestureSumX += filteredDx;
    gestureSumY += filteredDy;
    const int32_t filteredDistance = std::max(std::abs(gestureSumX), std::abs(gestureSumY));
    maxFilteredDistance = std::max(maxFilteredDistance, filteredDistance);
    if (filteredDistance > TAP_DEAD_ZONE)
        pressMoved = true;
    lastX = x;
    lastY = y;

    const int16_t rawDx = static_cast<int16_t>(sample.x - startRawX);
    const int16_t rawDy = static_cast<int16_t>(sample.y - startRawY);
    maxRawDistance = std::max(maxRawDistance, static_cast<int16_t>(std::max(std::abs(rawDx), std::abs(rawDy))));
    maxRawAbsX = std::max(maxRawAbsX, static_cast<int16_t>(std::abs(rawDx)));
    maxRawAbsY = std::max(maxRawAbsY, static_cast<int16_t>(std::abs(rawDy)));

    if (directionLocked) {
        const bool horizontal = lockedGesture == TouchGesture::SwipeLeft || lockedGesture == TouchGesture::SwipeRight;
        const int16_t axisDistance = horizontal ? std::abs(rawDx) : std::abs(rawDy);
        const int16_t signedDistance = horizontal ? rawDx : rawDy;
        const bool wrongWay = (lockedGesture == TouchGesture::SwipeLeft && signedDistance > 0) ||
                              (lockedGesture == TouchGesture::SwipeRight && signedDistance < 0) ||
                              (lockedGesture == TouchGesture::SwipeUp && signedDistance > 0) ||
                              (lockedGesture == TouchGesture::SwipeDown && signedDistance < 0);
        if (wrongWay && axisDistance > LOCK_DISTANCE)
            state = State::Cancelled;
    } else if (maxRawDistance >= LOCK_DISTANCE) {
        lockDirection(rawDx, rawDy);
    }

    const uint32_t duration = sample.timestamp - startTime;
    if (allowLongPress && duration >= LONG_PRESS_MS && stableSamples >= STABLE_SAMPLES_REQUIRED) {
        if (maxFilteredDistance <= LONG_PRESS_MOVE) {
            emit(TouchGesture::LongPress, duration, event);
            state = State::LongPressed;
            return true;
        }
        longPressRejected = true;
        if (!directionLocked)
            state = State::Cancelled;
    }
    return false;
}

void TouchGestureRecognizer::reset()
{
    state = State::Idle;
    directionLocked = false;
    pressMoved = false;
    longPressRejected = false;
    stableSamples = 0;
    startTime = 0;
    startRawX = 0;
    startRawY = 0;
    lastX = 0;
    lastY = 0;
    gestureSumX = 0;
    gestureSumY = 0;
    maxFilteredDistance = 0;
    maxRawDistance = 0;
    maxRawAbsX = 0;
    maxRawAbsY = 0;
    lockedGesture = TouchGesture::None;
    haveRaw = false;
    pendingJump = false;
    rejectedJump = false;
    confirmedJump = false;
    lastRawX = 0;
    lastRawY = 0;
    pendingX = 0;
    pendingY = 0;
    filterCount = 0;
    filterIndex = 0;
    lastFilteredX = 0;
    lastFilteredY = 0;
    haveFiltered = false;
}

void TouchGestureRecognizer::cancel()
{
    state = State::Cancelled;
    directionLocked = false;
    longPressRejected = true;
}

bool TouchGestureRecognizer::transformCoordinates(int16_t &x, int16_t &y, uint16_t width, uint16_t height, bool flipScreen,
                                                  int16_t offsetX, int16_t offsetY, float scaleX, float scaleY)
{
    if (width == 0 || height == 0)
        return false;
    const int32_t transformedX = static_cast<int32_t>(std::lround((x + offsetX) * scaleX));
    const int32_t transformedY = static_cast<int32_t>(std::lround((y + offsetY) * scaleY));
    if (transformedX < 0 || transformedY < 0 || transformedX >= width || transformedY >= height)
        return false;
    x = static_cast<int16_t>(flipScreen ? width - 1 - transformedX : transformedX);
    y = static_cast<int16_t>(flipScreen ? height - 1 - transformedY : transformedY);
    return true;
}

} // namespace meshtastic

#endif // T_DECK_MAX || _VARIANT_T_DECK_PRO_V1_1 || MESHTASTIC_T5S3_EPAPER_V2_UI
