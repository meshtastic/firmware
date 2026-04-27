#!/usr/bin/env bash
# Generate empty stub headers for the OLED CJK font sizes we did NOT generate.
# Screen.cpp does:  #include <utf8_10x10.h>  ... #include <utf8_24x24.h>
# unconditionally. The OLEDDisplayCJK lib SHIPS these for GB2312 — but we want
# to override the active size with our own (kana+hangul) version, which means
# OUR header takes precedence on the include path. Stub the rest to keep the
# build happy.
#
# Usage: stub_other_sizes.sh <font_dir> <kept_size>

set -e

FONT_DIR="${1:?font dir required}"
KEPT="${2:?kept size required}"

for s in 10 12 16 24; do
    if [ "$s" = "$KEPT" ]; then
        continue
    fi
    F="$FONT_DIR/utf8_${s}x${s}.h"
    if [ -f "$F" ]; then
        # Already provided by the lib; don't clobber.
        continue
    fi
    cat > "$F" <<EOF
#pragma once
#include <Arduino.h>

#define utf8_${s}x${s}_HEIGHT $s
#define utf8_${s}x${s}_WIDTH  $s
#define utf8_${s}x${s}_COUNT  0

static const uint16_t utf8_${s}x${s}_map[1]    PROGMEM = {0};
static const uint32_t utf8_${s}x${s}_offset[1] PROGMEM = {0};
static const uint8_t  utf8_${s}x${s}_data[1]   PROGMEM = {0};

static const struct {
    uint16_t height;
    uint16_t width;
    uint16_t count;
    const uint16_t *map;
    const uint32_t *offset;
    const uint8_t  *data;
} utf8_${s}x${s}_font = {
    $s, $s, 0,
    utf8_${s}x${s}_map,
    utf8_${s}x${s}_offset,
    utf8_${s}x${s}_data,
};
EOF
    echo "Stubbed $F"
done
