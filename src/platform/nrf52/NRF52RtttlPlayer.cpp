#include "NRF52RtttlPlayer.h"

#if defined(HAS_I2S_SPEAKER_NRF52)

#include "NRF52I2SOutput.h"
#include "mesh/Throttle.h"
#include <cctype>

NRF52RtttlPlayer nrf52RtttlPlayer;

namespace
{
// Index 0 unused, then C4..B4, C5..B5, C6..B6, C7..B7 (48 notes) - same layout as
// the NonBlockingRTTTL library's table, indexed as [(octave - 4) * 12 + note].
const int kNoteFreq[] = {
    0,    262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494,  523,  554,  587,  622,
    659,  698,  740,  784,  831,  880,  932,  988,  1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661,
    1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
};
} // namespace

void NRF52RtttlPlayer::begin(const char *songBuffer, uint8_t loopCountIn, unsigned long loopGapMs)
{
    buffer = songBuffer;
    defaultDuration = 4;
    defaultOctave = 6;
    bpm = 63;
    playing = true;
    noteStartAt = millis();
    noteDurationMs = 0;
    loopCount = loopCountIn;
    loopGap = loopGapMs;

    pinMode(SPEAKER_EN, OUTPUT);
    digitalWrite(SPEAKER_EN, HIGH);
#if defined(SPEAKER_EN_2)
    pinMode(SPEAKER_EN_2, OUTPUT);
    digitalWrite(SPEAKER_EN_2, HIGH);
#endif
    nrf52I2SOutput.begin(SPEAKER_BCLK, SPEAKER_WS_LRCK, SPEAKER_DATA);
    nrf52I2SOutput.stopTone();

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
        buffer++; // skip comma
    }
    if (*buffer == 'o') {
        buffer += 2; // skip "o="
        num = *buffer++ - '0';
        if (num >= 3 && num <= 7)
            defaultOctave = num;
        buffer++; // skip comma
    }
    if (*buffer == 'b') {
        buffer += 2; // skip "b="
        num = 0;
        while (isdigit((unsigned char)*buffer))
            num = (num * 10) + (*buffer++ - '0');
        bpm = num;
        buffer++; // skip colon
    }
    if (bpm <= 0)
        bpm = 63;

    wholeNoteMs = (60 * 1000L / bpm) * 4;
    firstNote = buffer;
}

void NRF52RtttlPlayer::nextNote()
{
    nrf52I2SOutput.stopTone();

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

    if (note && scale >= 4 && scale <= 7) {
        nrf52I2SOutput.startTone(kNoteFreq[(scale - 4) * 12 + note]);
    }
    noteStartAt = millis();
    noteDurationMs = (uint32_t)duration;
}

void NRF52RtttlPlayer::play()
{
    if (!playing)
        return;

    if (Throttle::isWithinTimespanMs(noteStartAt, noteDurationMs))
        return;

    if (*buffer == '\0') {
        if (--loopCount) {
            noteStartAt = millis();
            noteDurationMs = (uint32_t)loopGap;
            buffer = firstNote;
        } else {
            stop();
        }
        return;
    }

    nextNote();
}

void NRF52RtttlPlayer::stop()
{
    if (playing) {
        nrf52I2SOutput.stopTone();
        digitalWrite(SPEAKER_EN, LOW);
#if defined(SPEAKER_EN_2)
        digitalWrite(SPEAKER_EN_2, LOW);
#endif
        playing = false;
    }
}

#endif // HAS_I2S_SPEAKER_NRF52
