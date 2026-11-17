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

    // setTxMode has already said which mode and why.
    if (!setTxMode(mode)) {
        disable();
        return;
    }

#ifdef AUDIO_CAPTURE_DUMP
    LOG_WARN("Audio: CAPTURE DUMP build, hold PTT and samples print to serial");
#endif

    stream.begin(emitDecoded, this);
    if (inputBroker)
        inputObserver.observe(inputBroker);

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
    if (sending && txDegraded && mode)
        snprintf(who, sizeof(who), ">%s %u", destinationLabel(), (unsigned)mode->bitrate);
    else if (sending)
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

// Loads a libcodec2 mode into the shared handle. The old instance goes first:
// holding two while the new one allocates needs about 40 KB, which a node that
// has filled its database does not have.
bool AudioModule::ensureCodec(int c2mode)
{
    if (codec2 && codecMode == c2mode)
        return true;

    if (codec2) {
        codec2_destroy(codec2);
        codec2 = nullptr;
        codecMode = AUDIO_C2_NONE;
    }
    codec2 = codec2_create(c2mode);
    if (!codec2)
        return false;

    codec2_set_lpc_post_filter(codec2, 1, 0, 0.8, 0.2);
    codecMode = (int8_t)c2mode;
    return true;
}

bool AudioModule::setTxMode(const AudioModeInfo *m)
{
    // codecMode, not mode: receiving at another rate leaves the handle loaded
    // with the peer's mode while ours is unchanged.
    if (!ensureCodec(m->codec2Mode)) {
        LOG_ERROR("Audio: no codec2 for mode %d, not built in or out of memory", m->codec2Mode);
        return false;
    }

    mode = m;
    frameSamples = codec2_samples_per_frame(codec2);
    bytesPerFrame = codec2_bytes_per_frame(codec2);
    framesPerPacket = audioFramesPerPacket(*m);
    stream.setPacketMs(audioMsPerPacket(*m));
    return true;
}

// pinMode on every frame would be wasteful, so only touch the lines on a change.
void AudioModule::setAudioPower(bool capture, bool playback)
{
    if (capture == micPowered && playback == ampPowered)
        return;

    micPowered = capture;
    ampPowered = playback;
    audioIOEnable(capture, playback);
}

// Runs on the button thread. Only records the request: acting on it here would
// re-create the codec2 state while our own thread is encoding with it.
int AudioModule::handleInputEvent(const InputEvent *event)
{
    switch (event->inputEvent) {
    case INPUT_BROKER_PTT_DOWN:
        pendingPtt = PttRequest::down;
        break;
    case INPUT_BROKER_PTT_UP:
        pendingPtt = PttRequest::up;
        break;
    case INPUT_BROKER_PTT_CYCLE:
        pendingCycle = true;
        break;
    default:
        return 0;
    }
    setIntervalFromNow(0); // key up now, not at the next idle tick
    return 1;
}

// Drains what handleInputEvent recorded, on our own thread.
void AudioModule::serviceBrokerPtt()
{
    if (pendingCycle) {
        pendingCycle = false;
        cycleDestination();
        playClick();
        requestFocus();
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }

    const PttRequest req = pendingPtt;
    pendingPtt = PttRequest::none;
    if (req == PttRequest::down && radio_state == RadioState::rx && !brokerPtt) {
        brokerPtt = onPttDown();
    } else if (req == PttRequest::up && brokerPtt) {
        brokerPtt = false;
        onPttUp();
    }
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
    txDegraded = want->bitrate < audioModeFromConfig(moduleConfig.audio.bitrate)->bitrate;
    if (!setTxMode(want)) {
        playLongBeep();
        return false;
    }
    if (txDegraded) {
        LOG_INFO("Audio degraded to %u bit/s", want->bitrate);
        play4ClickDown();
    }

    setAudioPower(true, false);
    radio_state = RadioState::tx;
    pttStartMs = millis();
    txLen = AUDIO_HEADER_LEN;
    micIndex = 0;
    txSeq = 0;
    txSession = (uint8_t)random(256);

    // REGENERATE_FRAMESET only rebuilds the set; without requestFocus() the
    // screen stays on whatever frame it was already showing.
    requestFocus();
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
    setAudioPower(false, false);

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

#ifdef AUDIO_PCM_STREAM
    // Every frame, raw little-endian int16 between markers. USB CDC ignores the
    // nominal baud, so the limit was Serial.print formatting decimals, not the
    // link: writing bytes keeps up with 8 kHz and gives continuous audio.
    Serial.write("PCM<", 4);
    Serial.write((const uint8_t *)micBuf, (size_t)frameSamples * sizeof(int16_t));
    Serial.write(">PCM", 4);
    return;
#endif

#ifdef AUDIO_CAPTURE_DUMP
    // One 40 ms frame per second, straight out of micBuf. A RAM buffer is not
    // an option: free heap is a few KB and stealing it boot loops the board.
    // The speaker stays silent, so nothing feeds back into the microphone.
    static uint32_t dumpFrames = 0;
    if (++dumpFrames >= 25) {
        dumpFrames = 0;
        Serial.print("CAPDUMP-BEGIN ");
        Serial.println(frameSamples);
        for (int i = 0; i < frameSamples; i++) {
            Serial.print(micBuf[i]);
            Serial.print(',');
            if ((i & 15) == 15)
                Serial.println();
        }
        Serial.println();
        Serial.println("CAPDUMP-END");
        // Clipping is what a too-hot gain looks like; peak alone hides it.
        int clipped = 0, peak = 0;
        for (int i = 0; i < frameSamples; i++) {
            const int a = micBuf[i] < 0 ? -micBuf[i] : micBuf[i];
            if (a > peak)
                peak = a;
            if (a >= 32000)
                clipped++;
        }
        LOG_WARN("Audio: mic peak %d clipped %d/%d", peak, clipped, frameSamples);
        Serial.flush();
    }
    return;
#endif

    for (int i = 0; i < frameSamples; i++)
        speech[i] = (int16_t)hpFilter.Update((float)micBuf[i]);

#ifdef AUDIO_C2_DUMP
    const uint32_t encStart = millis();
#endif
    codec2_encode(codec2, txPacket + txLen, speech);
#ifdef AUDIO_C2_DUMP
    // PDM is unserviced while this runs, and the double buffer holds 16 ms.
    static uint32_t encMax = 0, encFrames = 0;
    const uint32_t took = millis() - encStart;
    if (took > encMax)
        encMax = took;
    if (++encFrames >= 25) {
        // The PDM double buffer holds 2 x 8 ms and goes unserviced while the
        // encoder runs, so overruns here mean the encoder is eating the mic.
        LOG_WARN("Audio: encode max %lu ms / 25 frames, PDM overruns %lu (headroom 16 ms)", (unsigned long)encMax,
                 (unsigned long)audioPdmOverruns);
        encFrames = 0;
        encMax = 0;
    }
#endif
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
    // No relay at all. One hop was meant to extend range without reflooding,
    // but measured on air it costs a second transmission of every packet and
    // the backoff puts it 1 to 2 s late, colliding with the packets that
    // follow. A voice packet that late is past its playout slot and the
    // assembler discards it, so the relay only ever took airtime from the
    // stream it was carrying.
    p->hop_limit = 0;
    p->decoded.payload.size = txLen;
    memcpy(p->decoded.payload.bytes, txPacket, txLen);
#ifdef AUDIO_C2_DUMP
    Serial.print("TXC2 ");
    Serial.print(h.seq);
    Serial.print(' ');
    Serial.print((unsigned)(txLen - AUDIO_HEADER_LEN));
    Serial.print(' ');
    for (uint16_t i = AUDIO_HEADER_LEN; i < txLen; i++) {
        if (txPacket[i] < 16)
            Serial.print('0');
        Serial.print(txPacket[i], HEX);
    }
    Serial.println();
    Serial.flush();
#endif
    service->sendToMesh(p);

    txLen = AUDIO_HEADER_LEN;
}

void AudioModule::emitDecoded(void *ctx, const uint8_t *frames, size_t len, uint8_t mode, bool conceal)
{
#ifdef AUDIO_C2_DUMP
    Serial.print("RXC2 ");
    Serial.print(conceal ? 'C' : 'F');
    Serial.print(' ');
    Serial.print((unsigned)len);
    Serial.print(' ');
    for (size_t i = 0; i < len; i++) {
        if (frames[i] < 16)
            Serial.print('0');
        Serial.print(frames[i], HEX);
    }
    Serial.println();
    Serial.flush();
#endif
    ((AudioModule *)ctx)->decodeAndPlay(frames, len, mode, conceal);
}

void AudioModule::decodeAndPlay(const uint8_t *frames, size_t len, uint8_t packetMode, bool conceal)
{
    // Peers degrade independently, so the incoming rate is whatever the sender
    // chose. Load it rather than dropping the packet; the next PTT press puts
    // our own mode back.
    if (!codec2 || codecMode != (int8_t)packetMode) {
        if (!ensureCodec(packetMode)) {
            LOG_WARN("Audio: no codec2 for mode %u, not built in or out of memory", packetMode);
            return;
        }
        LOG_INFO("Audio: receiving codec2 mode %u", packetMode);
    }

    // Read off the handle: a stale cached length would run this loop forever.
    const int nSamp = codec2_samples_per_frame(codec2);
    const size_t nBytes = (size_t)codec2_bytes_per_frame(codec2);
    if (!nBytes)
        return;

    // A packet is over a second of speech, so replaying one is not concealment,
    // it is a stuck buffer. codec2 has no PLC; silence for the gap is the least
    // objectionable thing at this granularity.
    if (conceal) {
        memset(playBuf, 0, sizeof(playBuf));
        for (size_t i = 0; i + nBytes <= len; i += nBytes)
            audioIOWrite(playBuf, nSamp, 500);
        playedThisTick = true;
        LOG_DEBUG("Audio: concealed %u frames as silence", (unsigned)(len / nBytes));
        return;
    }

    size_t played = 0;
    for (size_t i = 0; i + nBytes <= len; i += nBytes) {
        codec2_decode(codec2, playBuf, frames + i);
        played += audioIOWrite(playBuf, nSamp, 500);
    }
    playedThisTick = true;
    LOG_DEBUG("Audio: played %u of %u frames, mode %u%s", (unsigned)(played / nSamp), (unsigned)(len / nBytes), packetMode,
              conceal ? " (concealed)" : "");
}

void AudioModule::servicePlayout()
{
    const uint32_t now = millis();
    const bool due = !Throttle::isWithinTimespanMs(nextPlayoutMs, 0);

    concurrency::LockGuard g(&streamLock);
    // Ahead of tick(), so the amplifier is awake before the first frame lands.
    setAudioPower(false, stream.isReceiving());
    playedThisTick = false;
    stream.tick(now, due);
    if (due) {
        // Starved but still receiving: hand the peripheral a frame of silence.
        // Left unfed, EasyDMA repeats whatever it played last, so a dropped
        // packet turns into the final 40 ms looping until the stream times out.
        if (!playedThisTick && stream.isReceiving()) {
            memset(playBuf, 0, sizeof(playBuf));
            // TXPTRUPD comes once per buffer, which is 40 ms of audio, so a
            // shorter wait than that usually writes nothing and leaves the
            // peripheral repeating its last buffer.
            audioIOWrite(playBuf, frameSamples, 60);
            LOG_DEBUG("Audio: playout starved, sending silence");
        }
        // Pace on the mode that arrived, not the one we transmit. A peer at
        // 1400 packs far less speech per packet than our own 700C, and pacing
        // on ours releases the next packet early: playback runs ahead of the
        // audio it is playing.
        const AudioModeInfo *heard = audioModeFromCodec2(stream.streamMode());
        nextPlayoutMs = now + audioMsPerPacket(heard ? *heard : *mode);
    }
}

int32_t AudioModule::runOnce()
{
    if (!audioAllowed()) {
        if (!firstTime) {
            audioIOEnd();
            micPowered = ampPowered = false;
            // Per mode, and re-created on the way back in.
            if (codec2) {
                codec2_destroy(codec2);
                codec2 = nullptr;
                codecMode = AUDIO_C2_NONE;
            }
            firstTime = true; // re-init if the region or the config lets audio back
        }
        return disable();
    }

    if (firstTime) {
        if (!mode || !setTxMode(mode)) {
            LOG_ERROR("Audio disabled: no usable codec2 mode");
            return disable();
        }
        if (!audioIOBegin(8000, frameSamples)) {
            LOG_ERROR("Audio disabled: could not start the audio interface");
            return disable();
        }
        if (pttPin())
            pinMode(pttPin(), INPUT);
#if !defined(PTT_VIA_DOWN_BUTTON)
        else
            LOG_WARN("Audio: no PTT pin, receive only. Set moduleConfig.audio.ptt_pin to transmit.");
#endif
        radio_state = RadioState::rx;
        firstTime = false;
        return AUDIO_IDLE_INTERVAL_MS;
    }

    serviceBrokerPtt();

    const bool pressed = pttPin() && digitalRead(pttPin()) == HIGH;

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
            requestFocus();
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
    } else if (!pressed && !brokerPtt && radio_state == RadioState::tx) {
        onPttUp();
    } else if (radio_state == RadioState::tx && !Throttle::isWithinTimespanMs(pttStartMs, AUDIO_MAX_PTT_MS)) {
        LOG_INFO("PTT time-out after %u ms", (unsigned)AUDIO_MAX_PTT_MS);
        onPttUp();
        playLongBeep();
        brokerPtt = false;
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
