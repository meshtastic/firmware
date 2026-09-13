#include "I2CBuzzer.h"

#if !MESHTASTIC_EXCLUDE_I2C

#include "detect/ScanI2CTwoWire.h"
#include "mesh/Throttle.h"
#include "meshUtils.h"
#include <cctype>

I2CBuzzer *i2cBuzzer = nullptr;

namespace
{
// Index 0 unused, then C4..B4, C5..B5, C6..B6, C7..B7 (48 notes) - same layout as
// the NonBlockingRTTTL library's table, indexed as [(octave - 4) * 12 + note].
const uint16_t kNoteFreq[] = {
    0,    262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494,  523,  554,  587,  622,
    659,  698,  740,  784,  831,  880,  932,  988,  1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661,
    1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
};
} // namespace

// SparkFun Qwiic Buzzer register map (SparkFun_Qwiic_Buzzer_Arduino_Library)
static constexpr uint8_t kQwiicRegToneFrequencyMsb = 0x03; // MSB, LSB, volume, duration MSB, duration LSB
static constexpr uint8_t kQwiicRegActive = 0x08;
static constexpr uint8_t kQwiicVolumeMax = 4;

I2CBuzzer::I2CBuzzer(const ScanI2C::FoundDevice &device)
    : wire(ScanI2CTwoWire::fetchI2CBus(device.address)), address(device.address.address),
      protocol(device.type == ScanI2C::DeviceType::QWIIC_BUZZER ? Protocol::Qwiic : Protocol::Modulino)
{
    LOG_INFO("%s buzzer at address 0x%02X", protocol == Protocol::Qwiic ? "Qwiic" : "Modulino", address);
}

bool I2CBuzzer::writeBytes(const uint8_t *buf, size_t len)
{
    wire->beginTransmission(address);
    wire->write(buf, len);
    uint8_t result = wire->endTransmission();
    if (result != 0)
        LOG_WARN("I2C buzzer write failed: %u", result);
    return result == 0;
}

// Modulino command: frequency (Hz) then duration (ms), both little-endian uint32
bool I2CBuzzer::writeModulino(uint32_t frequencyHz, uint32_t durationMs)
{
    uint8_t buf[8];
    for (int i = 0; i < 4; i++) {
        buf[i] = (uint8_t)(frequencyHz >> (8 * i));
        buf[4 + i] = (uint8_t)(durationMs >> (8 * i));
    }
    return writeBytes(buf, sizeof(buf));
}

// Qwiic: load frequency/volume/duration (big-endian uint16), then toggle ACTIVE. Volume defaults to
// 0 (silent) on the device, and the buzzer resets itself once a non-zero duration has elapsed.
bool I2CBuzzer::writeQwiic(uint16_t frequencyHz, uint16_t durationMs, bool active)
{
    const uint8_t off[2] = {kQwiicRegActive, 0};
    if (!writeBytes(off, sizeof(off)))
        return false;
    if (!active)
        return true;
    const uint8_t cfg[6] = {kQwiicRegToneFrequencyMsb, (uint8_t)(frequencyHz >> 8), (uint8_t)frequencyHz,
                            kQwiicVolumeMax,           (uint8_t)(durationMs >> 8),  (uint8_t)durationMs};
    const uint8_t on[2] = {kQwiicRegActive, 1};
    return writeBytes(cfg, sizeof(cfg)) && writeBytes(on, sizeof(on));
}

void I2CBuzzer::tone(uint32_t frequencyHz, uint32_t durationMs)
{
    if (protocol == Protocol::Qwiic)
        writeQwiic(clamp<uint32_t>(frequencyHz, 0, UINT16_MAX), clamp<uint32_t>(durationMs, 0, UINT16_MAX), true);
    else
        writeModulino(frequencyHz, durationMs);
}

void I2CBuzzer::noTone()
{
    if (protocol == Protocol::Qwiic)
        writeQwiic(0, 0, false);
    else
        writeModulino(0, 0);
}

void I2CBuzzer::beginRtttl(const char *songBuffer)
{
    buffer = songBuffer;
    defaultDuration = 4;
    defaultOctave = 6;
    bpm = 63;
    playing = true;
    noteStartAt = millis();
    noteDurationMs = 0;

    // format: name:d=N,o=N,b=NNN:notes...
    while (*buffer != ':' && *buffer != '\0')
        buffer++;
    if (*buffer == ':')
        buffer++;

    int num;
    if (*buffer == 'd') {
        buffer += 2; // skip "d="
        num = 0;
        while (isdigit((unsigned char)*buffer))
            num = (num * 10) + (*buffer++ - '0');
        if (num > 0)
            defaultDuration = num;
        if (*buffer == ',')
            buffer++;
    }
    if (*buffer == 'o') {
        buffer += 2; // skip "o="
        num = *buffer++ - '0';
        if (num >= 3 && num <= 7)
            defaultOctave = num;
        if (*buffer == ',')
            buffer++;
    }
    if (*buffer == 'b') {
        buffer += 2; // skip "b="
        num = 0;
        while (isdigit((unsigned char)*buffer))
            num = (num * 10) + (*buffer++ - '0');
        bpm = num;
        if (*buffer == ':')
            buffer++;
    }
    if (bpm <= 0)
        bpm = 63;

    wholeNoteMs = (60 * 1000L / bpm) * 4;
}

void I2CBuzzer::nextNote()
{
    int num = 0;
    while (isdigit((unsigned char)*buffer))
        num = (num * 10) + (*buffer++ - '0');
    long duration = num ? (wholeNoteMs / num) : (wholeNoteMs / defaultDuration);

    int note = 0;
    switch (*buffer) {
    case 'c':
        note = 1;
        break;
    case 'd':
        note = 3;
        break;
    case 'e':
        note = 5;
        break;
    case 'f':
        note = 6;
        break;
    case 'g':
        note = 8;
        break;
    case 'a':
        note = 10;
        break;
    case 'b':
        note = 12;
        break;
    case 'p':
    default:
        note = 0;
        break;
    }
    if (*buffer)
        buffer++;

    if (*buffer == '#') {
        note++;
        buffer++;
    }
    if (*buffer == '.') {
        duration += duration / 2;
        buffer++;
    }

    int scale;
    if (isdigit((unsigned char)*buffer)) {
        scale = *buffer - '0';
        buffer++;
    } else {
        scale = defaultOctave;
    }
    if (*buffer == '.') {
        duration += duration / 2;
        buffer++;
    }
    if (*buffer == ',')
        buffer++;

    // The buzzer stops on its own after the duration, so rests just wait.
    if (note && scale >= 4 && scale <= 7)
        tone(kNoteFreq[(scale - 4) * 12 + note], (uint32_t)duration);
    noteStartAt = millis();
    noteDurationMs = (uint32_t)duration;
}

void I2CBuzzer::playRtttl()
{
    if (!playing)
        return;

    if (Throttle::isWithinTimespanMs(noteStartAt, noteDurationMs))
        return;

    if (*buffer == '\0') {
        playing = false;
        return;
    }

    nextNote();
}

void I2CBuzzer::stopRtttl()
{
    if (playing) {
        playing = false;
        noTone();
    }
}

#endif // !MESHTASTIC_EXCLUDE_I2C
