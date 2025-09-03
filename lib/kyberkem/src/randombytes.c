#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "randombytes.h"
#include "esp_random.h"

void esp_randombytes(uint8_t *out, size_t outlen) {
    esp_fill_random(out, outlen);
}