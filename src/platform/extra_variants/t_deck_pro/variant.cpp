#include "configuration.h"

#ifdef T_DECK_PRO

#include "InkHUDInput.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "input/TouchScreenImpl1.h"
#include <CSE_CST328.h>
#include <Wire.h>

static bool isV11()
{
#ifdef MESHTASTIC_INCLUDE_INKHUD
    return tDeckProIsV11();
#elif defined(T_DECK_PRO_V1_0)
    return false;
#else
    return true;
#endif
}

static CSE_CST328 &touchPanel()
{
    static CSE_CST328 panel(EINK_WIDTH, EINK_HEIGHT, &Wire, isV11() ? 38 : CST328_PIN_RST, CST328_PIN_INT);
    return panel;
}

static bool is_cst3530 = false;
volatile bool touch_isr = false;
#define CST3530_ADDR 0x1A
constexpr uint32_t CST3530_FAIL_HOLD_MS = 100;

bool read_cst3530_touch(int16_t *x, int16_t *y, bool &readSucceeded)
{
    readSucceeded = false;
    uint8_t buffer[9] = {0};
    uint8_t r_cmd[] = {0xD0, 0x07, 0x00, 0x00};
    uint8_t clear_cmd[] = {0xD0, 0x00, 0x02, 0xAB};

    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(r_cmd, sizeof(r_cmd));
    if (Wire.endTransmission() != 0) {
        LOG_DEBUG("CST3530 I2C send addr failed");
        return false;
    }

    int read_len = Wire.requestFrom((int)CST3530_ADDR, sizeof(buffer));
    if (read_len != sizeof(buffer)) {
        LOG_DEBUG("CST3530 read len error: %d (expect 9)", read_len);
        return false;
    }
    int actual_read = Wire.readBytes(buffer, sizeof(buffer));
    if (actual_read != sizeof(buffer)) {
        LOG_DEBUG("CST3530 read bytes error: %d (expect 9)", actual_read);
        return false;
    }
    readSucceeded = true;

    // Acknowledge release and malformed reports too, so the IRQ can assert again.
    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(clear_cmd, sizeof(clear_cmd));
    Wire.endTransmission();

    uint8_t report_typ = buffer[2];
    if (report_typ != 0xFF) {
        return false;
    }

    uint8_t touch_points = buffer[3] & 0x0F;
    if (touch_points == 0 || touch_points > 1) {
        LOG_DEBUG("CST3530 touch points invalid: %d", touch_points);
        return false;
    }
    // A lift report can still contain one finger.
    if ((buffer[8] >> 4) == 0)
        return false;

    *x = buffer[4] + ((uint16_t)(buffer[7] & 0x0F) << 8);
    *y = buffer[5] + ((uint16_t)(buffer[7] & 0xF0) << 4);

    // LOG_DEBUG("CST3530 touch: num:%d x=%d,y=%d", touch_points, *x, *y);

    return true;
}

bool readTouch(int16_t *x, int16_t *y)
{
    *x = *y = 0;
    if (is_cst3530) {
        static bool touched = false;
        static int16_t lastX = 0, lastY = 0;
        static uint32_t lastReadMs = 0;
        static uint32_t lastGoodReadMs = 0;
        // Recover a missed release without polling the controller while idle.
        if (touch_isr || (touched && Throttle::hasElapsed(lastReadMs, 50))) {
            touch_isr = false;
            bool readSucceeded;
            const bool contact = read_cst3530_touch(&lastX, &lastY, readSucceeded);
            lastReadMs = Time::getMillis();
            if (readSucceeded) {
                touched = contact;
                lastGoodReadMs = lastReadMs;
            } else if (touched && Throttle::hasElapsed(lastGoodReadMs, CST3530_FAIL_HOLD_MS)) {
                touched = false;
            }
        }
        *x = lastX;
        *y = lastY;
        return touched;
    } else {
        if (touchPanel().getTouches()) {
            *x = touchPanel().getPoint(0).x;
            *y = touchPanel().getPoint(0).y;
            return true;
        }
    }
    return false;
}

static void IRAM_ATTR touchInterruptHandler()
{
    touch_isr = true;
}

void attachTDeckProTouchInterrupt()
{
    if (is_cst3530) {
        pinMode(CST328_PIN_INT, INPUT);
        attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchInterruptHandler, FALLING);
        touch_isr = true;
    }
}

// T-Deck Pro specific init
void lateInitVariant()
{
    if (isV11()) {
        // GPIO38 powers the V1.0 sensor rail, so only pulse it on V1.1.
        pinMode(38, OUTPUT);
        digitalWrite(38, HIGH);
        delay(20);
        digitalWrite(38, LOW);
        delay(80);
        digitalWrite(38, HIGH);
        delay(20);

        int retry = 5;
        uint8_t buffer[7] = {};
        uint8_t r_cmd[] = {0x0d0, 0x03, 0x00, 0x00};

        // Probe touch chip
        while (retry--) {
            Wire.beginTransmission(CST3530_ADDR);
            Wire.write(r_cmd, sizeof(r_cmd));
            if (Wire.endTransmission() == 0) {
                const int received = Wire.requestFrom((int)CST3530_ADDR, 7);
                const int read = Wire.readBytes(buffer, 7);
                if (received == 7 && read == 7 && buffer[2] == 0xCA && buffer[3] == 0xCA) {
                    LOG_DEBUG("CST3530 detected");
                    is_cst3530 = true;

                    // The CST3530 will automatically enter sleep mode;
                    // polling should not be used, but rather an interrupt method should be employed.
                    attachTDeckProTouchInterrupt();

                    break;
                } else {
                    LOG_DEBUG("CST3530 no response");
                }
            }
            uint8_t cmd1[] = {0xD0, 0x00, 0x04, 0x00};
            Wire.beginTransmission(CST3530_ADDR);
            Wire.write(cmd1, sizeof(cmd1));
            Wire.endTransmission();
            delay(50);
        }
    }

    if (!is_cst3530 && !touchPanel().begin())
        LOG_WARN("CST328 initialization failed");

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
#ifdef MESHTASTIC_INCLUDE_INKHUD
    static TDeckProInkHUDInput input;
    input.begin();
#endif
}
#endif
