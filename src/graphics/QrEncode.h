#pragma once

#include <stdint.h>

// Minimal QR encoder: ECC level M, versions 1-6 (21-41 modules, up to 106 bytes), byte mode only.

#define QR_ENCODE_MAX_VERSION 6
#define QR_ENCODE_SIZE(version) (4 * (version) + 17)
#define QR_ENCODE_BUFFER_SIZE(version) ((QR_ENCODE_SIZE(version) * QR_ENCODE_SIZE(version) + 7) / 8)

struct QrEncode {
    uint8_t version;
    uint8_t size;
    uint8_t mask;
    uint8_t *modules;
};

// modules must hold QR_ENCODE_BUFFER_SIZE(QR_ENCODE_MAX_VERSION) bytes; picks the smallest version that fits.
bool qrEncodeText(QrEncode *qr, uint8_t *modules, const char *text);

// true for a dark module; false outside the symbol
bool qrGetModule(const QrEncode *qr, uint8_t x, uint8_t y);
