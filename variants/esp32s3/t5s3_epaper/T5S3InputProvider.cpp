#include "T5S3InputProvider.h"

#include <utility>

#if !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(T5_S3_EPAPER_PRO_V2)

T5S3InputProvider::~T5S3InputProvider() = default;

bool T5S3InputProvider::supportsTextInput() const
{
    return v2;
}

void T5S3InputProvider::startTextInput(const char *header, const char *initialText, uint32_t durationMs,
                                       std::function<void(const std::string &)> callback)
{
    if (!v2)
        return;

    if (!keyboard)
        keyboard = std::make_unique<graphics::T5S3Keyboard>();
    keyboard->start(header, initialText, durationMs, std::move(callback));
}

void T5S3InputProvider::stopTextInput(bool callEmptyCallback)
{
    if (v2 && keyboard)
        keyboard->stop(callEmptyCallback);
}

bool T5S3InputProvider::handleTextInput(const InputEvent &event)
{
    return v2 && keyboard && keyboard->handleInput(event);
}

bool T5S3InputProvider::drawTextInput(OLEDDisplay *display)
{
    if (!v2 || !keyboard)
        return false;
    if (keyboard->isTimedOut()) {
        keyboard->stop(true);
        return false;
    }
    return keyboard->draw(display);
}

bool T5S3InputProvider::textInputIsActive() const
{
    return v2 && keyboard && keyboard->isActive();
}

#else

T5S3InputProvider::~T5S3InputProvider() = default;

bool T5S3InputProvider::supportsTextInput() const
{
    return false;
}

void T5S3InputProvider::startTextInput(const char *, const char *, uint32_t,
                                       std::function<void(const std::string &)> )
{
}

void T5S3InputProvider::stopTextInput(bool)
{
}

bool T5S3InputProvider::handleTextInput(const InputEvent &)
{
    return false;
}

bool T5S3InputProvider::drawTextInput(OLEDDisplay *)
{
    return false;
}

bool T5S3InputProvider::textInputIsActive() const
{
    return false;
}

#endif
