#include "A7682Audio.h"

#if defined(HAS_A7682_AUDIO)

#include <Arduino.h>

#include <climits>
#include <cstdio>
#include <cstring>

#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"

namespace
{
static constexpr char A7682_AUDIO_PREFERENCE_FILE[] = "/prefs/a7682_audio.dat";
static constexpr char A7682_AUDIO_PREFERENCE_TEMP_FILE[] = "/prefs/a7682_audio.dat.tmp";
}

A7682Audio *a7682Audio = nullptr;

void initA7682Audio()
{
    if (!a7682Audio)
        a7682Audio = new A7682Audio();
}

A7682Audio::A7682Audio() : concurrency::OSThread("A7682Audio"), cueQueue(CUE_QUEUE_CAPACITY), serial(&Serial2)
{
    cueQueue.setReader(this);
    volume.store(loadSettings(), std::memory_order_relaxed);
    beginModem();
}

uint8_t A7682Audio::getVolume() const
{
    return volume.load(std::memory_order_relaxed);
}

void A7682Audio::setVolume(uint8_t value)
{
    const uint8_t clamped = clampA7682AudioVolume(value);
    const uint8_t previous = volume.exchange(clamped, std::memory_order_relaxed);
    if (previous == clamped)
        return;

    volumeDirty.store(true, std::memory_order_relaxed);
    if (!saveSettings())
        LOG_ERROR("Could not save A7682E audio volume");
    setIntervalFromNow(0);
}

bool A7682Audio::queueCue(A7682AudioCue cue)
{
    if (!acceptingCues.load(std::memory_order_relaxed))
        return false;

    if (!cueQueue.enqueue(cue, 0)) {
        LOG_WARN("A7682E audio cue queue is full");
        return false;
    }
    setIntervalFromNow(0);
    return true;
}

bool A7682Audio::loadSettings()
{
#ifdef FSCom
    A7682AudioPreferenceRecord record = {};
    size_t bytesRead = 0;
    bool fileRead = false;

    {
        concurrency::LockGuard guard(spiLock);
        auto file = FSCom.open(A7682_AUDIO_PREFERENCE_FILE, FILE_O_READ);
        if (file) {
            bytesRead = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record));
            file.close();
            fileRead = true;
        }
    }

    if (fileRead && isValidA7682AudioPreferenceRecord(record, bytesRead))
        return record.volume;

    if (fileRead)
        LOG_WARN("Invalid A7682E audio preference, using default volume");
#endif
    return A7682_AUDIO_DEFAULT_VOLUME;
}

bool A7682Audio::saveSettings() const
{
#ifdef FSCom
    const A7682AudioPreferenceRecord record = {
        A7682_AUDIO_PREFERENCE_MAGIC,
        A7682_AUDIO_PREFERENCE_VERSION,
        volume.load(std::memory_order_relaxed),
        0,
    };

    concurrency::LockGuard guard(spiLock);

    if (!FSCom.exists("/prefs") && !FSCom.mkdir("/prefs"))
        return false;

    if (FSCom.exists(A7682_AUDIO_PREFERENCE_TEMP_FILE) && !FSCom.remove(A7682_AUDIO_PREFERENCE_TEMP_FILE))
        return false;

    auto file = FSCom.open(A7682_AUDIO_PREFERENCE_TEMP_FILE, FILE_O_WRITE);
    if (!file)
        return false;

    const size_t bytesWritten = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    file.flush();
    file.close();

    if (bytesWritten != sizeof(record)) {
        FSCom.remove(A7682_AUDIO_PREFERENCE_TEMP_FILE);
        return false;
    }

    if (FSCom.exists(A7682_AUDIO_PREFERENCE_FILE) && !FSCom.remove(A7682_AUDIO_PREFERENCE_FILE)) {
        FSCom.remove(A7682_AUDIO_PREFERENCE_TEMP_FILE);
        return false;
    }

    if (!FSCom.rename(A7682_AUDIO_PREFERENCE_TEMP_FILE, A7682_AUDIO_PREFERENCE_FILE)) {
        FSCom.remove(A7682_AUDIO_PREFERENCE_TEMP_FILE);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void A7682Audio::beginModem()
{
    pinMode(MODEM_POWER_EN, OUTPUT);
    digitalWrite(MODEM_POWER_EN, HIGH);

    pinMode(MODEM_PWRKEY, OUTPUT);
    digitalWrite(MODEM_PWRKEY, LOW);

    pinMode(MODEM_RST, OUTPUT);
    digitalWrite(MODEM_RST, HIGH);

    pinMode(MODEM_DTR, OUTPUT);
    digitalWrite(MODEM_DTR, LOW);

    // MODEM_RX/MODEM_TX name the A7682E side of the connection. The Arduino
    // API takes the MCU-side RX pin first, matching the LilyGO examples.
    serial->begin(115200, SERIAL_8N1, MODEM_TX, MODEM_RX);
    while (serial->available() > 0)
        serial->read();

    state = State::STARTUP_WAIT;
    stateStartedAt = millis();
    setIntervalFromNow(10);
}

void A7682Audio::startResetSequence()
{
    resetTried = true;
    command = Command::NONE;
    digitalWrite(MODEM_RST, LOW);
    state = State::RESET_LOW;
    stateStartedAt = millis();
    setIntervalFromNow(10);
}

void A7682Audio::startPowerKeyPulse()
{
    powerKeyTried = true;
    command = Command::NONE;
    digitalWrite(MODEM_PWRKEY, LOW);
    state = State::POWER_KEY_LOW;
    stateStartedAt = millis();
    setIntervalFromNow(5);
}

void A7682Audio::pollModem()
{
    while (serial && serial->available() > 0) {
        const char value = static_cast<char>(serial->read());
        if (value == '\r')
            continue;

        if (value == '\n') {
            if (lineLength > 0) {
                lineBuffer[lineLength] = '\0';
                processLine(lineBuffer);
                lineLength = 0;
            }
            continue;
        }

        if (lineLength < sizeof(lineBuffer) - 1)
            lineBuffer[lineLength++] = value;
    }
}

void A7682Audio::processLine(const char *line)
{
    LOG_DEBUG("A7682E RX: %s", line);

    if (strstr(line, "+AUDIOSTATE:audio playstop") != nullptr) {
        audioStarted = false;
        if (state == State::WAIT_PLAY_START || state == State::PLAYING) {
            command = Command::NONE;
            state = State::READY;
            setIntervalFromNow(0);
        }
        return;
    }

    if (strstr(line, "+AUDIOSTATE:audio play") != nullptr) {
        if (command == Command::PLAY || state == State::WAIT_PLAY_START) {
            command = Command::NONE;
            audioStarted = true;
            playStartedAt = millis();
            state = State::PLAYING;
            setIntervalFromNow(25);
        }
        return;
    }

    if (strstr(line, "ERROR") != nullptr) {
        if (command != Command::NONE)
            commandFailed();
        return;
    }

    if (command == Command::PLAY && (strstr(line, "+CCMXPLAY: OK") != nullptr || strcmp(line, "OK") == 0)) {
        commandSucceeded();
        return;
    }

    if (command != Command::NONE && strcmp(line, "OK") == 0)
        commandSucceeded();
}

void A7682Audio::sendCommand(Command commandType, const char *text, uint32_t timeoutMs)
{
    if (!serial || !text)
        return;

    LOG_DEBUG("A7682E AT: %s", text);
    serial->print(text);
    serial->print("\r\n");
    command = commandType;
    commandStartedAt = millis();
    commandTimeoutMs = timeoutMs;
    setIntervalFromNow(10);
}

void A7682Audio::sendGainCommand()
{
    commandVolume = volume.load(std::memory_order_relaxed);
    char commandText[32];
    snprintf(commandText, sizeof(commandText), "AT+COUTGAIN=%u", commandVolume);
    sendCommand(Command::GAIN, commandText);
}

void A7682Audio::startPlayback(A7682AudioCue cue)
{
    activeCue = cue;
    audioStarted = false;
    LOG_DEBUG("A7682E play %s", a7682AudioPathForCue(cue));
    serial->print("AT+CCMXPLAY=\"");
    serial->print(a7682AudioPathForCue(cue));
    serial->print("\",0,0\r\n");
    command = Command::PLAY;
    commandStartedAt = millis();
    commandTimeoutMs = COMMAND_TIMEOUT_MS;
    setIntervalFromNow(10);
}

void A7682Audio::commandSucceeded()
{
    const Command completed = command;
    command = Command::NONE;

    switch (completed) {
    case Command::PROBE:
#if defined(A7682_AUDIO_DEBUG_FILESYSTEM)
        sendCommand(Command::FILESYSTEM_CD, "AT+FSCD=C:/");
#else
        sendGainCommand();
#endif
        break;

#if defined(A7682_AUDIO_DEBUG_FILESYSTEM)
    case Command::FILESYSTEM_CD:
        sendCommand(Command::FILESYSTEM_LS, "AT+FSLS=2");
        break;

    case Command::FILESYSTEM_LS:
        LOG_INFO("A7682E filesystem listing complete");
        sendGainCommand();
        break;
#endif

    case Command::GAIN:
        if (commandVolume == volume.load(std::memory_order_relaxed))
            volumeDirty.store(false, std::memory_order_relaxed);
        state = State::READY;
        retryCount = 0;
        setIntervalFromNow(0);
        break;

    case Command::PLAY:
        state = State::WAIT_PLAY_START;
        stateStartedAt = millis();
        setIntervalFromNow(10);
        break;

    case Command::NONE:
        break;
    }
}

void A7682Audio::commandFailed()
{
    const Command failed = command;
    command = Command::NONE;

#if defined(A7682_AUDIO_DEBUG_FILESYSTEM)
    if (failed == Command::FILESYSTEM_CD || failed == Command::FILESYSTEM_LS) {
        LOG_WARN("A7682E filesystem debug command failed");
        sendGainCommand();
        return;
    }
#endif

    if (failed == Command::PLAY) {
        LOG_WARN("A7682E audio playback command failed");
        audioStarted = false;
        state = State::READY;
        setIntervalFromNow(0);
        return;
    }

    if (failed == Command::PROBE) {
        if (!resetTried) {
            startResetSequence();
            return;
        }
        if (!powerKeyTried) {
            startPowerKeyPulse();
            return;
        }
    }

    enterBackoff();
}

void A7682Audio::enterBackoff()
{
    command = Command::NONE;
    state = State::BACKOFF;
    retryStartedAt = millis();
    if (retryCount < 4)
        retryCount++;

    retryDelayMs = RETRY_INITIAL_MS;
    for (uint8_t i = 1; i < retryCount; ++i) {
        if (retryDelayMs >= RETRY_MAXIMUM_MS / 2) {
            retryDelayMs = RETRY_MAXIMUM_MS;
            break;
        }
        retryDelayMs *= 2;
    }
    if (retryDelayMs > RETRY_MAXIMUM_MS)
        retryDelayMs = RETRY_MAXIMUM_MS;
    LOG_WARN("A7682E unavailable, retry in %lu ms", static_cast<unsigned long>(retryDelayMs));
    setIntervalFromNow(100);
}

int32_t A7682Audio::runOnce()
{
    if (state == State::OFF)
        return INT32_MAX;

    pollModem();

    if (command != Command::NONE && !Throttle::isWithinTimespanMs(commandStartedAt, commandTimeoutMs)) {
        LOG_WARN("A7682E AT command timeout");
        commandFailed();
    }

    switch (state) {
    case State::STARTUP_WAIT:
        if (command == Command::NONE && !Throttle::isWithinTimespanMs(stateStartedAt, STARTUP_WAIT_MS))
            sendCommand(Command::PROBE, "AT");
        return 25;

    case State::RESET_LOW:
        if (!Throttle::isWithinTimespanMs(stateStartedAt, RESET_LOW_MS)) {
            digitalWrite(MODEM_RST, HIGH);
            state = State::RESET_HIGH;
            stateStartedAt = millis();
        }
        return 25;

    case State::RESET_HIGH:
        if (!Throttle::isWithinTimespanMs(stateStartedAt, RESET_HIGH_MS)) {
            state = State::WAIT_BOOT;
            stateStartedAt = millis();
        }
        return 10;

    case State::POWER_KEY_LOW:
        if (!Throttle::isWithinTimespanMs(stateStartedAt, POWER_KEY_LOW_MS)) {
            digitalWrite(MODEM_PWRKEY, HIGH);
            state = State::POWER_KEY_HIGH;
            stateStartedAt = millis();
        }
        return 5;

    case State::POWER_KEY_HIGH:
        if (!Throttle::isWithinTimespanMs(stateStartedAt, POWER_KEY_HIGH_MS)) {
            digitalWrite(MODEM_PWRKEY, LOW);
            state = State::WAIT_BOOT;
            stateStartedAt = millis();
        }
        return 10;

    case State::WAIT_BOOT:
        if (command == Command::NONE && !Throttle::isWithinTimespanMs(stateStartedAt, MODEM_BOOT_WAIT_MS))
            sendCommand(Command::PROBE, "AT");
        return 25;

    case State::READY:
        if (volumeDirty.load(std::memory_order_relaxed)) {
            sendGainCommand();
            return 10;
        }
        if (!cueQueue.dequeue(&activeCue, 0))
            return 1000;
        startPlayback(activeCue);
        return 10;

    case State::WAIT_PLAY_START:
        if (!audioStarted && !Throttle::isWithinTimespanMs(stateStartedAt, PLAY_START_TIMEOUT_MS)) {
            LOG_WARN("A7682E audio playback did not start");
            state = State::READY;
            setIntervalFromNow(0);
            return 10;
        }
        return 10;

    case State::PLAYING:
        if (!Throttle::isWithinTimespanMs(playStartedAt, PLAY_MAXIMUM_MS)) {
            LOG_WARN("A7682E audio playback timeout");
            serial->print("AT+CCMXSTOP\r\n");
            audioStarted = false;
            state = State::READY;
            setIntervalFromNow(0);
            return 10;
        }
        return 25;

    case State::BACKOFF:
        if (command == Command::NONE && !Throttle::isWithinTimespanMs(retryStartedAt, retryDelayMs))
            sendCommand(Command::PROBE, "AT");
        return 100;

    case State::OFF:
        return INT32_MAX;
    }

    return 1000;
}

void A7682Audio::shutdown()
{
    if (!acceptingCues.exchange(false, std::memory_order_relaxed))
        return;

    if (serial) {
        serial->print("AT+CCMXSTOP\r\n");
        serial->flush();
        delay(20);
    }

    digitalWrite(MODEM_PWRKEY, LOW);
    delay(10);
    digitalWrite(MODEM_PWRKEY, HIGH);
    delay(3000);
    digitalWrite(MODEM_PWRKEY, LOW);
    delay(10);
    digitalWrite(MODEM_POWER_EN, LOW);

    if (serial)
        serial->end();
    state = State::OFF;
    disable();
}

#endif // HAS_A7682_AUDIO
