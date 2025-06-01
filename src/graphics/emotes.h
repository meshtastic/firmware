#pragma once
#include <Arduino.h>

namespace graphics
{

// === Emote List ===
struct Emote {
    const char *label;
    const unsigned char *bitmap;
    int width;
    int height;
};

extern const Emote emotes[/* numEmotes */];
extern const int numEmotes;

#ifndef EXCLUDE_EMOJI
// === Emote Bitmaps ===
#define thumbs_height 25
#define thumbs_width 25
extern const unsigned char thumbup[] PROGMEM;
extern const unsigned char thumbdown[] PROGMEM;

#define smiley_height 30
#define smiley_width 30
extern const unsigned char smiley[] PROGMEM;

#define question_height 25
#define question_width 25
extern const unsigned char question[] PROGMEM;

#define bang_height 30
#define bang_width 30
extern const unsigned char bang[] PROGMEM;

#define haha_height 30
#define haha_width 30
extern const unsigned char haha[] PROGMEM;

#define wave_icon_height 30
#define wave_icon_width 30
extern const unsigned char wave_icon[] PROGMEM;

#define cowboy_height 30
#define cowboy_width 30
extern const unsigned char cowboy[] PROGMEM;

#define deadmau5_height 30
#define deadmau5_width 60
extern const unsigned char deadmau5[] PROGMEM;

#define sun_height 30
#define sun_width 30
extern const unsigned char sun[] PROGMEM;

#define rain_height 30
#define rain_width 30
extern const unsigned char rain[] PROGMEM;

#define cloud_height 30
#define cloud_width 30
extern const unsigned char cloud[] PROGMEM;

#define fog_height 25
#define fog_width 25
extern const unsigned char fog[] PROGMEM;

#define devil_height 30
#define devil_width 30
extern const unsigned char devil[] PROGMEM;

#define heart_height 30
#define heart_width 30
extern const unsigned char heart[] PROGMEM;

#define poo_height 30
#define poo_width 30
extern const unsigned char poo[] PROGMEM;

#define bell_icon_width 30
#define bell_icon_height 30
extern const unsigned char bell_icon[] PROGMEM;
#endif // EXCLUDE_EMOJI

} // namespace graphics
