#pragma once

#include <stdint.h>

struct Vec3f {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct QMI8658LiveData {
    bool initialized = false;
    bool ready = false;
    Vec3f acc;  // m/s^2
    Vec3f gyr;  // dps
    uint32_t last_ms = 0;
};

struct QMC6310LiveData {
    bool initialized = false;
    int16_t rawX = 0, rawY = 0, rawZ = 0;
    float offX = 0, offY = 0, offZ = 0;
    float heading = 0; // degrees 0..360
    uint32_t last_ms = 0;
};

extern QMI8658LiveData g_qmi8658Live;
extern QMC6310LiveData g_qmc6310Live;

