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
#define thumbs_height 16
#define thumbs_width 16
extern const unsigned char thumbup[] PROGMEM;
extern const unsigned char thumbdown[] PROGMEM;

#define Smiling_Eyes_height 16
#define Smiling_Eyes_width 16
extern const unsigned char Smiling_Eyes[] PROGMEM;

#define Grinning_height 16
#define Grinning_width 16
extern const unsigned char Grinning[] PROGMEM;

#define Slightly_Smiling_height 16
#define Slightly_Smiling_width 16
extern const unsigned char Slightly_Smiling[] PROGMEM;

#define Winking_Face_height 16
#define Winking_Face_width 16
extern const unsigned char Winking_Face[] PROGMEM;

#define Grinning_Smiling_Eyes_height 16
#define Grinning_Smiling_Eyes_width 16
extern const unsigned char Grinning_Smiling_Eyes[] PROGMEM;

#define heart_smile_height 16
#define heart_smile_width 16
extern const unsigned char heart_smile[] PROGMEM;

#define Heart_eyes_height 16
#define Heart_eyes_width 16
extern const unsigned char Heart_eyes[] PROGMEM;

#define question_height 16
#define question_width 16
extern const unsigned char question[] PROGMEM;

#define bang_height 16
#define bang_width 16
extern const unsigned char bang[] PROGMEM;

#define haha_height 16
#define haha_width 16
extern const unsigned char haha[] PROGMEM;

#define ROFL_height 16
#define ROFL_width 16
extern const unsigned char ROFL[] PROGMEM;

#define Smiling_Closed_Eyes_height 16
#define Smiling_Closed_Eyes_width 16
extern const unsigned char Smiling_Closed_Eyes[] PROGMEM;

#define Grinning_SmilingEyes2_height 16
#define Grinning_SmilingEyes2_width 16
extern const unsigned char Grinning_SmilingEyes2[] PROGMEM;

#define Loudly_Crying_Face_height 16
#define Loudly_Crying_Face_width 16
extern const unsigned char Loudly_Crying_Face[] PROGMEM;

#define wave_icon_height 16
#define wave_icon_width 16
extern const unsigned char wave_icon[] PROGMEM;

#define cowboy_height 16
#define cowboy_width 16
extern const unsigned char cowboy[] PROGMEM;

#define deadmau5_height 16
#define deadmau5_width 16
extern const unsigned char deadmau5[] PROGMEM;

#define sun_height 16
#define sun_width 16
extern const unsigned char sun[] PROGMEM;

#define rain_height 16
#define rain_width 16
extern const unsigned char rain[] PROGMEM;

#define cloud_height 16
#define cloud_width 16
extern const unsigned char cloud[] PROGMEM;

#define fog_height 16
#define fog_width 16
extern const unsigned char fog[] PROGMEM;

#define devil_height 16
#define devil_width 16
extern const unsigned char devil[] PROGMEM;

#define heart_height 16
#define heart_width 16
extern const unsigned char heart[] PROGMEM;

#define poo_height 16
#define poo_width 16
extern const unsigned char poo[] PROGMEM;

#define bell_icon_width 16
#define bell_icon_height 16
extern const unsigned char bell_icon[] PROGMEM;

#define cookie_width 16
#define cookie_height 16
extern const unsigned char cookie[] PROGMEM;

#define Fire_width 16
#define Fire_height 16
extern const unsigned char Fire[] PROGMEM;

#define peace_sign_width 16
#define peace_sign_height 16
extern const unsigned char peace_sign[] PROGMEM;

#define Praying_width 16
#define Praying_height 16
extern const unsigned char Praying[] PROGMEM;

#define Sparkles_width 16
#define Sparkles_height 16
extern const unsigned char Sparkles[] PROGMEM;

#define clown_width 16
#define clown_height 16
extern const unsigned char clown[] PROGMEM;

#define robo_width 16
#define robo_height 16
extern const unsigned char robo[] PROGMEM;

#define hole_width 16
#define hole_height 16
extern const unsigned char hole[] PROGMEM;

#define bowling_width 16
#define bowling_height 16
extern const unsigned char bowling[] PROGMEM;

#define vulcan_salute_width 16
#define vulcan_salute_height 16
extern const unsigned char vulcan_salute[] PROGMEM;
#endif // EXCLUDE_EMOJI

} // namespace graphics