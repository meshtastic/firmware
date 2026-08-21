#include "configuration.h"
#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO
#include "AudioCodebooks.h"
#include "AudioModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioLibInterface.h"
#include "Router.h"
#include "airtime.h"
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
    // Duty cycle is checked in runOnce, not here: the region can change later.

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

    const AudioModeInfo *want = mode;
    mode = nullptr; // setTxMode compares against this
    if (!setTxMode(want)) {
        LOG_ERROR("Audio disabled: codec2 mode %d not built into this firmware", want->codec2Mode);
        disable();
        return;
    }

    stream.begin(emitDecoded, this);

    LOG_INFO("Audio: %u bit/s, %d frames of %d bytes per packet, %u ms of speech", mode->bitrate, framesPerPacket, bytesPerFrame,
             (unsigned)audioMsPerPacket(*mode));
}

// Truncate in place until the string fits, so a long node or channel name does
// not run off a 72 px panel.
static void fitToWidth(OLEDDisplay *display, char *str, uint16_t width)
{
    while (str[0] && display->getStringWidth(str) > width) {
        size_t n = strlen(str);
        str[n - 1] = 0;
    }
}

void AudioModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const bool sending = radio_state == RadioState::tx;

    bool receiving;
    {
        concurrency::LockGuard g(&streamLock);
        receiving = stream.isReceiving();
    }

    char who[32];
    if (sending)
        snprintf(who, sizeof(who), ">%s", destinationLabel());
    else if (receiving)
        snprintf(who, sizeof(who), "<%s", talkerLabel());
    else
        snprintf(who, sizeof(who), ">%s", destinationLabel()); // idle: show who we would talk to

    char status[12];
    if (sending) {
        const uint32_t held = millis() - pttStartMs;
        if (held + AUDIO_PTT_WARN_MS >= AUDIO_MAX_PTT_MS)
            snprintf(status, sizeof(status), "%u", (unsigned)((AUDIO_MAX_PTT_MS - held + 999) / 1000));
        else
            snprintf(status, sizeof(status), "PTT");
    } else {
        if (receiving)
            snprintf(status, sizeof(status), "RX");
        else
            snprintf(status, sizeof(status), "%u", (unsigned)(mode ? mode->bitrate : 0));
    }

    display->setFont(FONT_SMALL);
    fitToWidth(display, who, display->getWidth());

#if defined(OLED_TINY)
    // 72x40 with 13 px lines: two centred rows, no header bar. The compact UI
    // draws no nav chrome either.
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2 + x, y, who);
    display->drawString(display->getWidth() / 2 + x, y + FONT_HEIGHT_SMALL, status);
#else
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
    display->setColor(BLACK);
    display->drawString(0 + x, 0 + y, who);
    display->setColor(WHITE);

    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2 + x, (display->getHeight() - FONT_HEIGHT_SMALL) / 2 + y, status);
#endif
}

void AudioModule::cycleDestination()
{
    if (txDest == NODENUM_BROADCAST) {
        for (uint8_t ch = txChannel + 1; ch < channels.getNumChannels(); ch++) {
            if (channels.getByIndex(ch).role != meshtastic_Channel_Role_DISABLED) {
                txChannel = ch;
                return;
            }
        }
    }

    // Past the last channel, walk the favourites, then wrap to channel 0.
    const size_t total = nodeDB->getNumMeshNodes();
    bool seenCurrent = (txDest == NODENUM_BROADCAST);
    for (size_t i = 0; i < total; i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum() || !nodeInfoLiteIsFavorite(n))
            continue;
        if (seenCurrent) {
            txDest = n->num;
            txChannel = 0;
            return;
        }
        if (n->num == txDest)
            seenCurrent = true;
    }

    txDest = NODENUM_BROADCAST;
    txChannel = 0;
}

const char *AudioModule::destinationLabel()
{
    if (txDest == NODENUM_BROADCAST)
        snprintf(label, sizeof(label), "#%s", channels.getName(txChannel));
    else {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(txDest);
        if (n && nodeInfoLiteHasUser(n) && n->short_name[0])
            snprintf(label, sizeof(label), "%s", n->short_name);
        else
            snprintf(label, sizeof(label), "%08x", (unsigned)txDest);
    }
    return label;
}

// Bare name for a DM, name@#channel otherwise, following MessageRenderer.
const char *AudioModule::talkerLabel()
{
    NodeNum from;
    uint8_t ch;
    bool dm;
    {
        concurrency::LockGuard g(&streamLock);
        from = stream.talker();
        ch = stream.talkerChannel();
        dm = stream.talkerIsDm();
    }

    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(from);
    const char *who = (n && nodeInfoLiteHasUser(n) && n->short_name[0]) ? n->short_name : nullptr;

    if (who)
        snprintf(label, sizeof(label), dm ? "%s" : "%s@#%s", who, channels.getName(ch));
    else
        snprintf(label, sizeof(label), dm ? "%08x" : "%08x@#%s", (unsigned)from, channels.getName(ch));
    return label;
}

// Regulatory and practicality gates. The radio does not exist when the module
// is constructed, so none of this can move into the constructor.
bool AudioModule::audioAllowed()
{
    if (!moduleConfig.audio.codec2_enabled || !myRegion->profile->audioPermitted)
        return false;
    // A duty-cycled region cannot carry a continuous voice stream at all.
    return getEffectiveDutyCycle() >= 100;
}

// Degrade until the mode fits the preset, one rung further if the channel is
// already busy. Null when even the lowest rate cannot fit.
const AudioModeInfo *AudioModule::pickMode()
{
    if (!RadioLibInterface::instance)
        return audioModeFromConfig(moduleConfig.audio.bitrate);

    const uint32_t airMs =
        RadioLibInterface::instance->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader));
    const float util = airTime ? airTime->channelUtilizationPercent() : 0.0f;
    return chooseAudioMode(moduleConfig.audio.bitrate, airMs, util);
}

bool AudioModule::setTxMode(const AudioModeInfo *m)
{
    if (m == mode && codec2)
        return true;

    struct CODEC2 *c = codec2_create(m->codec2Mode);
    if (!c) {
        LOG_ERROR("Audio: codec2 mode %d not built in", m->codec2Mode);
        return false;
    }
    codec2_set_lpc_post_filter(c, 1, 0, 0.8, 0.2);
    if (codec2)
        codec2_destroy(codec2);

    codec2 = c;
    mode = m;
    frameSamples = codec2_samples_per_frame(c);
    bytesPerFrame = codec2_bytes_per_frame(c);
    framesPerPacket = audioFramesPerPacket(*m);
    stream.setPacketMs(audioMsPerPacket(*m));
    return true;
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

    const AudioModeInfo *want = pickMode();
    if (!want) {
        LOG_WARN("PTT refused, no codec2 rate fits this preset");
        playLongBeep();
        return false;
    }
    const bool degraded = want->bitrate < audioModeFromConfig(moduleConfig.audio.bitrate)->bitrate;
    if (!setTxMode(want)) {
        playLongBeep();
        return false;
    }
    if (degraded) {
        LOG_INFO("Audio degraded to %u bit/s", want->bitrate);
        play4ClickDown();
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
    LOG_INFO("PTT pressed, session %u to 0x%08x", txSession, (unsigned)txDest);
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
    p->to = txDest;
    p->channel = txChannel;
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
    // Peers degrade independently, so the incoming rate is whatever the sender
    // chose. Keep a decoder for it rather than dropping the packet.
    if ((int8_t)packetMode != rxCodecMode) {
        if (rxCodec)
            codec2_destroy(rxCodec);
        rxCodec = codec2_create(packetMode);
        if (!rxCodec) {
            LOG_WARN("Audio: cannot decode codec2 mode %u, not built in", packetMode);
            rxCodecMode = AUDIO_C2_NONE;
            return;
        }
        codec2_set_lpc_post_filter(rxCodec, 1, 0, 0.8, 0.2);
        rxCodecMode = (int8_t)packetMode;
        rxFrameSamples = codec2_samples_per_frame(rxCodec);
        rxBytesPerFrame = codec2_bytes_per_frame(rxCodec);
        LOG_INFO("Audio: receiving codec2 mode %u", packetMode);
    }
    if (!rxCodec)
        return;

    for (size_t i = 0; i + rxBytesPerFrame <= len; i += rxBytesPerFrame) {
        codec2_decode(rxCodec, playBuf, frames + i);
        audioIOWrite(playBuf, rxFrameSamples, 500);
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
    if (!audioAllowed())
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

    if (pressed && !pttHeld) {
        pttHeld = true;
        pttDownMs = millis();
    } else if (!pressed && pttHeld) {
        pttHeld = false;
        // Released before arming and never transmitted: a short press, which
        // picks who we would be talking to.
        if (radio_state != RadioState::tx && !pttLatched) {
            cycleDestination();
            playClick();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
        }
        pttLatched = false;
    }

    const bool armed = pttHeld && !Throttle::isWithinTimespanMs(pttDownMs, AUDIO_PTT_ARM_MS);

    if (armed && radio_state == RadioState::rx && !pttLatched) {
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
    if (!audioAllowed() || isFromUs(&mp))
        return ProcessMessage::CONTINUE;

    // Runs on the Router thread; runOnce drains the stream on ours.
    concurrency::LockGuard g(&streamLock);
    const bool isDm = mp.to != NODENUM_BROADCAST;
    stream.accept(getFrom(&mp), mp.channel, isDm, mp.decoded.payload.bytes, mp.decoded.payload.size, millis());

    return ProcessMessage::CONTINUE;
}

#endif
