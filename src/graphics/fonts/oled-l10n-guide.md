# OLED localization guide

## 1. Create an extended ASCII custom font
Use a glyph editor to create a new font file. The easiest way is to use the online [glyph editor][glyphEditor] from the OLED library. [Glyph editor source code][glyphEditorSource]. Copy and paste the existing font, modify it according desired codepage and save the new font file in `graphics/font` folder.

Please note that the used font file format differs from common Adafruit GFX.

## 2. Update the `customFontTableLookup` function in `Screen.h`
To map the double-byte UTF-8 code to the corresponding extended ASCII character of the desired codepage update the `customFontTableLookup` function in the `Screen.h` file. You need to modify the `switch (last)` statement: use left byte from UTF-8 code in the `case` label to map charachter's right byte to its extended ASCII code by specifying an offset.

## 3. Define language and font in `Screen.cpp`
```
#ifdef OLED_{LANG_NAME}
#include "fonts/OLEDDisplayFonts{LANG_NAME}.h"
#endif

...

#ifdef OLED_{LANG_NAME}
#define FONT_SMALL ArialMT_Plain_10_{LANG_NAME}
#else
#define FONT_SMALL ArialMT_Plain_10
#endif
```

## 4. Define language in `variant/*/platformio.ini`
```
build_flags =
  ${esp32_base.build_flags}
  -D xxxxx
  -D OLED_{LANG_NAME}
  -I variants/xxxxx
```

   [glyphEditor]: <https://rawgit.com/ThingPulse/esp8266-oled-ssd1306/master/resources/glyphEditor.html>
   [glyphEditorSource]: <https://github.com/ThingPulse/esp8266-oled-ssd1306/tree/master/resources>