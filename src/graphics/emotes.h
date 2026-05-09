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

#define smiling_eyes_height 16
#define smiling_eyes_width 16
extern const unsigned char smiling_eyes[] PROGMEM;

#define grinning_height 16
#define grinning_width 16
extern const unsigned char grinning[] PROGMEM;

#define slightly_smiling_height 16
#define slightly_smiling_width 16
extern const unsigned char slightly_smiling[] PROGMEM;

#define winking_face_height 16
#define winking_face_width 16
extern const unsigned char winking_face[] PROGMEM;

#define grinning_smiling_eyes_height 16
#define grinning_smiling_eyes_width 16
extern const unsigned char grinning_smiling_eyes[] PROGMEM;

#define heart_smile_height 16
#define heart_smile_width 16
extern const unsigned char heart_smile[] PROGMEM;

#define heart_eyes_height 16
#define heart_eyes_width 16
extern const unsigned char heart_eyes[] PROGMEM;

#define question_height 16
#define question_width 16
extern const unsigned char question[] PROGMEM;

#define bang_height 16
#define bang_width 16
extern const unsigned char bang[] PROGMEM;

#define haha_height 16
#define haha_width 16
extern const unsigned char haha[] PROGMEM;

#define rofl_height 16
#define rofl_width 16
extern const unsigned char rofl[] PROGMEM;

#define smiling_closed_eyes_height 16
#define smiling_closed_eyes_width 16
extern const unsigned char smiling_closed_eyes[] PROGMEM;

#define grinning_smiling_eyes_2_height 16
#define grinning_smiling_eyes_2_width 16
extern const unsigned char grinning_smiling_eyes_2[] PROGMEM;

#define loudly_crying_face_height 16
#define loudly_crying_face_width 16
extern const unsigned char loudly_crying_face[] PROGMEM;

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

#define fire_width 16
#define fire_height 16
extern const unsigned char fire[] PROGMEM;

#define peace_sign_width 16
#define peace_sign_height 16
extern const unsigned char peace_sign[] PROGMEM;

#define praying_width 16
#define praying_height 16
extern const unsigned char praying[] PROGMEM;

#define sparkles_width 16
#define sparkles_height 16
extern const unsigned char sparkles[] PROGMEM;

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

#define jack_o_lantern_width 16
#define jack_o_lantern_height 16
extern const unsigned char jack_o_lantern[] PROGMEM;

#define ghost_width 16
#define ghost_height 16
extern const unsigned char ghost[] PROGMEM;

#define skull_width 16
#define skull_height 16
extern const unsigned char skull[] PROGMEM;

#define vomiting_width 16
#define vomiting_height 16
extern const unsigned char vomiting[] PROGMEM;

#define cool_width 16
#define cool_height 16
extern const unsigned char cool[] PROGMEM;

#define shortcake_width 16
#define shortcake_height 16
extern const unsigned char shortcake[] PROGMEM;

#define caution_width 16
#define caution_height 16
extern const unsigned char caution[] PROGMEM;

#define clipboard_width 16
#define clipboard_height 16
extern const unsigned char clipboard[] PROGMEM;

#define snowflake_width 16
#define snowflake_height 16
extern const unsigned char snowflake[] PROGMEM;

#define drop_width 16
#define drop_height 16
extern const unsigned char drop[] PROGMEM;

#define thermometer_width 16
#define thermometer_height 16
extern const unsigned char thermometer[] PROGMEM;

#define sun_behind_raincloud_width 16
#define sun_behind_raincloud_height 16
extern const unsigned char sun_behind_raincloud[] PROGMEM;

#define sun_behind_cloud_width 16
#define sun_behind_cloud_height 16
extern const unsigned char sun_behind_cloud[] PROGMEM;

#define cloud_with_snow_width 16
#define cloud_with_snow_height 16
extern const unsigned char cloud_with_snow[] PROGMEM;

#define cloud_with_lightning_width 16
#define cloud_with_lightning_height 16
extern const unsigned char cloud_with_lightning[] PROGMEM;

#define cloud_with_lightning_rain_width 16
#define cloud_with_lightning_rain_height 16
extern const unsigned char cloud_with_lightning_rain[] PROGMEM;

#define wind_face_width 16
#define wind_face_height 16
extern const unsigned char wind_face[] PROGMEM;

#define new_moon_width 16
#define new_moon_height 16
extern const unsigned char new_moon[] PROGMEM;

#define waxing_crescent_moon_width 16
#define waxing_crescent_moon_height 16
extern const unsigned char waxing_crescent_moon[] PROGMEM;

#define first_quarter_moon_width 16
#define first_quarter_moon_height 16
extern const unsigned char first_quarter_moon[] PROGMEM;

#define waxing_gibbous_moon_width 16
#define waxing_gibbous_moon_height 16
extern const unsigned char waxing_gibbous_moon[] PROGMEM;

#define full_moon_width 16
#define full_moon_height 16
extern const unsigned char full_moon[] PROGMEM;

#define waning_gibbous_moon_width 16
#define waning_gibbous_moon_height 16
extern const unsigned char waning_gibbous_moon[] PROGMEM;

#define last_quarter_moon_width 16
#define last_quarter_moon_height 16
extern const unsigned char last_quarter_moon[] PROGMEM;

#define waning_crescent_moon_width 16
#define waning_crescent_moon_height 16
extern const unsigned char waning_crescent_moon[] PROGMEM;

#define first_quarter_moon_face_width 16
#define first_quarter_moon_face_height 16
extern const unsigned char first_quarter_moon_face[] PROGMEM;

#define peach_width 16
#define peach_height 16
extern const unsigned char peach[] PROGMEM;

#define turkey_width 16
#define turkey_height 16
extern const unsigned char turkey[] PROGMEM;

#define turkey_leg_width 16
#define turkey_leg_height 16
extern const unsigned char turkey_leg[] PROGMEM;

#define south_west_arrow_width 16
#define south_west_arrow_height 16
extern const unsigned char south_west_arrow[] PROGMEM;

#define south_east_arrow_width 16
#define south_east_arrow_height 16
extern const unsigned char south_east_arrow[] PROGMEM;

#define north_west_arrow_width 16
#define north_west_arrow_height 16
extern const unsigned char north_west_arrow[] PROGMEM;

#define north_east_arrow_width 16
#define north_east_arrow_height 16
extern const unsigned char north_east_arrow[] PROGMEM;

#define downwards_arrow_width 16
#define downwards_arrow_height 16
extern const unsigned char downwards_arrow[] PROGMEM;

#define leftwards_arrow_width 16
#define leftwards_arrow_height 16
extern const unsigned char leftwards_arrow[] PROGMEM;

#define upwards_arrow_width 16
#define upwards_arrow_height 16
extern const unsigned char upwards_arrow[] PROGMEM;

#define rightwards_arrow_width 16
#define rightwards_arrow_height 16
extern const unsigned char rightwards_arrow[] PROGMEM;

#define strong_width 16
#define strong_height 16
extern const unsigned char strong[] PROGMEM;

#define check_mark_width 16
#define check_mark_height 16
extern const unsigned char check_mark[] PROGMEM;

#define house_width 16
#define house_height 16
extern const unsigned char house[] PROGMEM;

#define shrug_width 16
#define shrug_height 16
extern const unsigned char shrug[] PROGMEM;

#define eyes_width 16
#define eyes_height 16
extern const unsigned char eyes[] PROGMEM;

#define eye_width 16
#define eye_height 16
extern const unsigned char eye[] PROGMEM;
#endif // EXCLUDE_EMOJI

} // namespace graphics
