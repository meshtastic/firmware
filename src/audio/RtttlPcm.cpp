#include "RtttlPcm.h"

#include <string.h>

// Equal-tempered note frequencies, C4 through B7, with a leading 0 so that a
// note index of 0 means "rest". Ported verbatim from AudioGeneratorRTTTL so that
// existing ringtones keep their exact pitches.
static const int notes[] = {0,    262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494,  523,  554,  587,  622,
                            659,  698,  740,  784,  831,  880,  932,  988,  1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661,
                            1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951};
static constexpr int notesCount = sizeof(notes) / sizeof(notes[0]);

void RtttlPcm::reset()
{
    _song[0] = 0;
    _len = 0;
    _ptr = 0;
    _toneCount = 0;
    _toneIndex = 0;
    _toneMode = false;
    _samplesPerWaveFP10 = 0;
    _phaseFP10 = 0;
    _noteSamples = 0;
    _samplesSent = 0;
    _done = true;
}

bool RtttlPcm::begin(const char *song, size_t len)
{
    reset();

    if (!song || len == 0)
        return false;

    if (len > sizeof(_song) - 1)
        len = sizeof(_song) - 1;
    memcpy(_song, song, len);
    _song[len] = 0;
    _len = (int)len;

    if (!parseHeader())
        return false;

    // Arm the first note now so isPlaying() is true immediately and a song whose
    // body is empty reports done rather than emitting a stuck note.
    if (!nextNote())
        return false;

    _done = false;
    return true;
}

bool RtttlPcm::beginTones(const ToneDuration *tones, size_t count)
{
    reset();

    if (!tones || count == 0)
        return false;

    if (count > kMaxTones)
        count = kMaxTones;
    memcpy(_tones, tones, count * sizeof(ToneDuration));
    _toneCount = count;
    _toneMode = true;

    if (!nextTone())
        return false;

    _done = false;
    return true;
}

bool RtttlPcm::skipWhitespace()
{
    while ((_ptr < _len) && ((_song[_ptr] == ' ') || (_song[_ptr] == '\t') || (_song[_ptr] == '\r') || (_song[_ptr] == '\n')))
        _ptr++;
    return _ptr < _len;
}

bool RtttlPcm::readInt(int *dest)
{
    if (_ptr >= _len)
        return false;

    skipWhitespace();
    if (_ptr >= _len)
        return false;
    if ((_song[_ptr] < '0') || (_song[_ptr] > '9'))
        return false;

    int t = 0;
    // Unlike upstream, this loop is bounded by _len as well as by the character
    // class, so a song ending in a digit cannot walk off the end.
    while ((_ptr < _len) && (_song[_ptr] >= '0') && (_song[_ptr] <= '9')) {
        t = (t * 10) + (_song[_ptr] - '0');
        _ptr++;
    }
    *dest = t;
    return true;
}

bool RtttlPcm::parseHeader()
{
    // Skip the title, up to and including the first ':'.
    while ((_ptr < _len) && (_song[_ptr] != ':'))
        _ptr++;
    if (_ptr >= _len)
        return false;
    if (_song[_ptr++] != ':')
        return false;

    // The d=, o=, b= fields are required, in that order.
    if (!skipWhitespace())
        return false;
    if ((_song[_ptr] != 'd') && (_song[_ptr] != 'D'))
        return false;
    _ptr++;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != '=')
        return false;
    if (!readInt(&_defaultDuration))
        return false;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != ',')
        return false;

    if (!skipWhitespace())
        return false;
    if ((_song[_ptr] != 'o') && (_song[_ptr] != 'O'))
        return false;
    _ptr++;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != '=')
        return false;
    if (!readInt(&_defaultOctave))
        return false;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != ',')
        return false;

    int bpm = 0;
    if (!skipWhitespace())
        return false;
    if ((_song[_ptr] != 'b') && (_song[_ptr] != 'B'))
        return false;
    _ptr++;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != '=')
        return false;
    if (!readInt(&bpm))
        return false;
    if (!skipWhitespace())
        return false;
    if (_song[_ptr++] != ':')
        return false;

    // Upstream divided by bpm unguarded; "b=0" crashed.
    if (bpm <= 0)
        return false;
    if (_defaultDuration <= 0)
        return false;

    _wholeNoteMs = (60 * 1000 * 4) / bpm;

    return true;
}

bool RtttlPcm::nextNote()
{
    int dur, note, scale;

    if (_ptr >= _len)
        return false;

    if (!readInt(&dur) || (dur <= 0))
        dur = _defaultDuration;
    // Truncating twice - once here and again when converting ms to samples - is
    // what upstream did, and existing ringtones depend on the exact result.
    dur = _wholeNoteMs / dur;

    if (_ptr >= _len)
        return false;

    note = 0;
    switch (_song[_ptr++]) {
    case 'c':
    case 'C':
        note = 1;
        break;
    case 'd':
    case 'D':
        note = 3;
        break;
    case 'e':
    case 'E':
        note = 5;
        break;
    case 'f':
    case 'F':
        note = 6;
        break;
    case 'g':
    case 'G':
        note = 8;
        break;
    case 'a':
    case 'A':
        note = 10;
        break;
    case 'b':
    case 'B':
        note = 12;
        break;
    case 'p':
    case 'P':
        note = 0;
        break;
    default:
        // Anything else ends the song, which is also how a trailing separator is
        // absorbed.
        return false;
    }

    if ((_ptr < _len) && (_song[_ptr] == '#')) {
        _ptr++;
        note++;
    }
    // Accept a dot on either side of the octave digit; upstream only looked after
    // it, so the spec-legal "4c#.5" silently desynced and truncated the song.
    bool dotted = false;
    if ((_ptr < _len) && (_song[_ptr] == '.')) {
        _ptr++;
        dotted = true;
    }
    if (!readInt(&scale))
        scale = _defaultOctave;
    if (!dotted && (_ptr < _len) && (_song[_ptr] == '.')) {
        _ptr++;
        dotted = true;
    }
    if (dotted)
        dur += dur / 2;

    skipWhitespace();
    if ((_ptr < _len) && (_song[_ptr] == ','))
        _ptr++;

    if (scale < 4)
        scale = 4;
    if (scale > 7)
        scale = 7;

    int freq = 0;
    if (note) {
        int index = (scale - 4) * 12 + note;
        // "b#7" indexes one past the table upstream; clamp instead of reading OOB.
        if (index >= notesCount)
            index = notesCount - 1;
        freq = notes[index];
    }

    startNote(freq, dur);
    return true;
}

bool RtttlPcm::nextTone()
{
    if (_toneIndex >= _toneCount)
        return false;

    const ToneDuration &t = _tones[_toneIndex++];
    // NOTE_SILENT is 1Hz, which as a square wave would be an audible thump rather
    // than a rest, so treat anything at or below it as silence.
    startNote(t.frequency_khz > 1 ? t.frequency_khz : 0, t.duration_ms);
    return true;
}

void RtttlPcm::startNote(int freqHz, int durationMs)
{
    if (durationMs < 0)
        durationMs = 0;

    _samplesPerWaveFP10 = freqHz > 0 ? (int32_t)((kSampleRate << 10) / (uint32_t)freqHz) : 0;
    _phaseFP10 = 0;
    _noteSamples = (kSampleRate * (uint32_t)durationMs) / 1000;
    _samplesSent = 0;
}

bool RtttlPcm::advance()
{
    return _toneMode ? nextTone() : nextNote();
}

size_t RtttlPcm::generate(int16_t *interleavedLR, size_t maxFrames)
{
    if (!interleavedLR || _done)
        return 0;

    size_t n = 0;
    while (n < maxFrames) {
        if (_samplesSent >= _noteSamples) {
            if (!advance()) {
                _done = true;
                break;
            }
            // A zero-length note would otherwise spin without making progress.
            if (_noteSamples == 0)
                continue;
        }

        if (_samplesPerWaveFP10 == 0) {
            while ((n < maxFrames) && (_samplesSent < _noteSamples)) {
                interleavedLR[2 * n] = 0;
                interleavedLR[2 * n + 1] = 0;
                _samplesSent++;
                n++;
            }
        } else {
            while ((n < maxFrames) && (_samplesSent < _noteSamples)) {
                int16_t v = (_phaseFP10 > (_samplesPerWaveFP10 / 2)) ? kAmplitude : -kAmplitude;
                interleavedLR[2 * n] = v;
                interleavedLR[2 * n + 1] = v;
                _phaseFP10 += 1024;
                if (_phaseFP10 >= _samplesPerWaveFP10)
                    _phaseFP10 -= _samplesPerWaveFP10;
                _samplesSent++;
                n++;
            }
        }
    }

    return n;
}
