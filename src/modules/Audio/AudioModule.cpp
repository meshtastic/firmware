#include "configuration.h"
#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO
#include "AudioCodebooks.h"
#include "AudioModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "buzz/buzz.h"
#include "concurrency/LockGuard.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"

/*
    Codec2 voice over the mesh. Half duplex, push to talk.

    Codec 2 is a low-bitrate speech codec, patent free and open source,
    developed by David Grant Rowe: http://www.rowetel.com/
*/

static ButterworthFilter hpFilter(240, 8000, ButterworthFilter::ButterworthFilter::Highpass, 1);

AudioModule *audioModule;

// How often runOnce wants to be called while idle.
#define AUDIO_IDLE_INTERVAL_MS 100

static uint8_t pttPin()
{
    return moduleConfig.audio.ptt_pin ? moduleConfig.audio.ptt_pin : PTT_PIN;
}

AudioModule::AudioModule() : SinglePortModule("Audio", meshtastic_PortNum_AUDIO_APP), concurrency::OSThread("Audio")
{
    if (!moduleConfig.audio.codec2_enabled || !myRegion->profile->audioPermitted) {
        disable();
        return;
    }

    // Must happen before codec2_create() when the tables are not in the image.
    const char *bindErr = audioCodebooksBind();
    if (bindErr) {
        LOG_ERROR("Audio disabled: %s", bindErr);
        disable();
        return;
    }

    // Never derive the codec2 mode arithmetically from the config value:
    // codec2 renumbered its modes and bitrate-1 is wrong for 450 only.
    mode = audioModeFromConfig(moduleConfig.audio.bitrate);
    if (!mode) {
        LOG_ERROR("Audio disabled: bitrate %u is not a codec2 mode", moduleConfig.audio.bitrate);
        disable();
        return;
    }

    codec2 = codec2_create(mode->codec2Mode);
    if (!codec2) {
        LOG_ERROR("Audio disabled: codec2 mode %d not built into this firmware", mode->codec2Mode);
        disable();
        return;
    }
    codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);

    frameSamples = codec2_samples_per_frame(codec2);
    bytesPerFrame = codec2_bytes_per_frame(codec2);
    framesPerPacket = audioFramesPerPacket(*mode);

    stream.begin(emitDecoded, this);
    stream.setPacketMs(audioMsPerPacket(*mode));

    LOG_INFO("Audio: %u bit/s, %d frames of %d bytes per packet, %u ms of speech", mode->bitrate, framesPerPacket, bytesPerFrame,
             (unsigned)audioMsPerPacket(*mode));
}

void AudioModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    char buffer[50];

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
    display->setColor(BLACK);
    display->drawStringf(0 + x, 0 + y, buffer, "Codec2 %u bit/s", mode ? mode->bitrate : 0);
    display->setColor(WHITE);
    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char *label = "Receive";
    char countdown[8];
    if (radio_state == RadioState::tx) {
        const uint32_t held = millis() - pttStartMs;
        if (held + AUDIO_PTT_WARN_MS >= AUDIO_MAX_PTT_MS) {
            snprintf(countdown, sizeof(countdown), "%u", (unsigned)((AUDIO_MAX_PTT_MS - held + 999) / 1000));
            label = countdown;
        } else {
            label = "PTT";
        }
    }
    display->drawString(display->getWidth() / 2 + x, (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y, label);
}

bool AudioModule::onPttDown()
{
    {
        concurrency::LockGuard g(&streamLock);
        if (stream.isReceiving()) {
            LOG_INFO("PTT refused, transmission in progress");
            playLongBeep();
            return false;
        }
    }

    radio_state = RadioState::tx;
    pttStartMs = millis();
    txLen = AUDIO_HEADER_LEN;
    micIndex = 0;
    txSeq = 0;
    txSession = (uint8_t)random(256);

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    LOG_INFO("PTT pressed, session %u", txSession);
    return true;
}

void AudioModule::onPttUp()
{
    // Flush whatever is accumulated so the tail of the sentence is not lost,
    // and mark it so the receiver can stop rather than wait out the timeout.
    sendPacket(true);
    radio_state = RadioState::rx;

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    LOG_INFO("PTT released");
}

void AudioModule::captureAndEncode()
{
    micIndex += (int)audioIORead(micBuf + micIndex, (size_t)(frameSamples - micIndex), 40);
    if (micIndex < frameSamples)
        return;
    micIndex = 0;

    for (int i = 0; i < frameSamples; i++)
        speech[i] = (int16_t)hpFilter.Update((float)micBuf[i]);

    codec2_encode(codec2, txPacket + txLen, speech);
    txLen += bytesPerFrame;

    if (txLen >= AUDIO_HEADER_LEN + framesPerPacket * bytesPerFrame)
        sendPacket(false);
}

void AudioModule::sendPacket(bool last)
{
    if (txLen <= AUDIO_HEADER_LEN) {
        if (!last)
            return;
        // Nothing buffered but the receiver still needs the end marker.
    }

    AudioHeader h{AUDIO_MAGIC, (uint8_t)mode->codec2Mode, txSession, (uint8_t)(last ? 1 : 0), txSeq++};
    memcpy(txPacket, &h, sizeof(h));

    meshtastic_MeshPacket *p = allocReply();
    if (!p)
        return;
    p->to = NODENUM_BROADCAST;
    p->decoded.want_response = false;
    p->want_ack = false; // shoot and forget, an ACK would arrive far too late
    p->priority = meshtastic_MeshPacket_Priority_MAX;
    // One relay, not the mesh-wide hop limit: a voice stream at 25% occupancy
    // reflooded three hops out would swamp the network.
    p->hop_limit = 1;
    p->decoded.payload.size = txLen;
    memcpy(p->decoded.payload.bytes, txPacket, txLen);
    service->sendToMesh(p);

    txLen = AUDIO_HEADER_LEN;
}

void AudioModule::emitDecoded(void *ctx, const uint8_t *frames, size_t len, uint8_t mode, bool conceal)
{
    (void)conceal;
    ((AudioModule *)ctx)->decodeAndPlay(frames, len, mode);
}

void AudioModule::decodeAndPlay(const uint8_t *frames, size_t len, uint8_t packetMode)
{
    if (packetMode != mode->codec2Mode) {
        // A talker on another bitrate. Decoding would need a second codec2
        // instance; drop with a note rather than emit noise.
        LOG_WARN("Audio: ignoring packet in codec2 mode %u, we are in %d", packetMode, mode->codec2Mode);
        return;
    }

    for (size_t i = 0; i + bytesPerFrame <= len; i += bytesPerFrame) {
        codec2_decode(codec2, playBuf, frames + i);
        audioIOWrite(playBuf, frameSamples, 500);
    }
}

void AudioModule::servicePlayout()
{
    const uint32_t now = millis();
    const bool due = !Throttle::isWithinTimespanMs(nextPlayoutMs, 0);

    concurrency::LockGuard g(&streamLock);
    stream.tick(now, due);
    if (due)
        nextPlayoutMs = now + audioMsPerPacket(*mode);
}

int32_t AudioModule::runOnce()
{
    if (!moduleConfig.audio.codec2_enabled || !myRegion->profile->audioPermitted)
        return disable();

    if (firstTime) {
        if (!audioIOBegin(8000, frameSamples)) {
            LOG_ERROR("Audio disabled: could not start the audio interface");
            return disable();
        }
        pinMode(pttPin(), INPUT);
        radio_state = RadioState::rx;
        firstTime = false;
        return AUDIO_IDLE_INTERVAL_MS;
    }

    const bool pressed = digitalRead(pttPin()) == HIGH;
    if (!pressed)
        pttLatched = false;

    if (pressed && radio_state == RadioState::rx && !pttLatched) {
        if (!onPttDown())
            pttLatched = true; // refused; wait for a release before asking again
    } else if (!pressed && radio_state == RadioState::tx) {
        onPttUp();
    } else if (radio_state == RadioState::tx && !Throttle::isWithinTimespanMs(pttStartMs, AUDIO_MAX_PTT_MS)) {
        LOG_INFO("PTT time-out after %u ms", (unsigned)AUDIO_MAX_PTT_MS);
        onPttUp();
        playLongBeep();
        pttLatched = true; // no restart until the button is let go
    }

    if (radio_state == RadioState::tx) {
        captureAndEncode();
        // Capture blocks for up to a frame, so come straight back for the next.
        return 0;
    }

    servicePlayout();
    return AUDIO_IDLE_INTERVAL_MS;
}

meshtastic_MeshPacket *AudioModule::allocReply()
{
    return allocDataPacket();
}

bool AudioModule::shouldDraw()
{
    return moduleConfig.audio.codec2_enabled && radio_state == RadioState::tx;
}

ProcessMessage AudioModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!moduleConfig.audio.codec2_enabled || !myRegion->profile->audioPermitted || isFromUs(&mp))
        return ProcessMessage::CONTINUE;

    // Runs on the Router thread; runOnce drains the stream on ours.
    concurrency::LockGuard g(&streamLock);
    stream.accept(getFrom(&mp), mp.decoded.payload.bytes, mp.decoded.payload.size, millis());

    return ProcessMessage::CONTINUE;
}

#endif
