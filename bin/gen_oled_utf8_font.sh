#!/bin/bash

python ./gen_oled_utf8_font.py --size 12 --charset gb2312 --kana --ttf "./font/simsun.ttc" --ttc-index 0 --output ../src/graphics/fonts/utf8_12x12.h --prefix utf8_12x12_
python ./gen_oled_utf8_font.py --size 16 --charset gb2312 --kana --ttf "./font/simsun.ttc" --ttc-index 0 --output ../src/graphics/fonts/utf8_16x16.h --prefix utf8_16x16_
python ./gen_oled_utf8_font.py --size 24 --charset gb2312 --kana --ttf "./font/simsun.ttc" --ttc-index 0 --output ../src/graphics/fonts/utf8_24x24.h --prefix utf8_24x24_