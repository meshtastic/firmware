#include "configuration.h"

#if defined(T_DECK_PRO) && !defined(_VARIANT_T_DECK_PRO_V1_1)

#include "input/TouchScreenImpl1.h"
#include <CSE_CST328.h>
#include <Wire.h>

CSE_CST328 tsPanel = CSE_CST328(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);

static bool is_cst3530 = false;
volatile bool touch_isr = false;
static constexpr uint8_t CST3530_ADDR = 0x1A;

bool read_cst3530_touch(int16_t *x, int16_t *y)
{
    uint8_t buffer[9] = {0};
    uint8_t r_cmd[] = {0xD0, 0x07, 0x00, 0x00};
    uint8_t clear_cmd[] = {0xD0, 0x00, 0x02, 0xAB};

    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(r_cmd, sizeof(r_cmd));
    if (Wire.endTransmission() != 0)
        return false;

    int read_len = Wire.requestFrom(static_cast<int>(CST3530_ADDR), sizeof(buffer));
    if (read_len != sizeof(buffer))
        return false;
    if (Wire.readBytes(buffer, sizeof(buffer)) != sizeof(buffer))
        return false;

    if (buffer[2] != 0xFF)
        return false;

    const uint8_t touch_points = buffer[3] & 0x0F;
    if (touch_points == 0 || touch_points > 1)
        return false;

    *x = buffer[4] + (static_cast<uint16_t>(buffer[7] & 0x0F) << 8);
    *y = buffer[5] + (static_cast<uint16_t>(buffer[7] & 0xF0) << 4);

    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(clear_cmd, sizeof(clear_cmd));
    Wire.endTransmission();
    return true;
}

bool readTouch(int16_t *x, int16_t *y)
{
    if (is_cst3530) {
        if (touch_isr) {
            touch_isr = false;
            return read_cst3530_touch(x, y);
        }
        return false;
    }

    if (!tsPanel.getTouches())
        return false;
    *x = tsPanel.getPoint(0).x;
    *y = tsPanel.getPoint(0).y;
    return true;
}

static void IRAM_ATTR touchInterruptHandler()
{
    touch_isr = true;
}

void tDeckProLateInit()
{
    pinMode(CST328_PIN_RST, OUTPUT);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);
    digitalWrite(CST328_PIN_RST, LOW);
    delay(80);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);

    int retry = 5;
    uint8_t buffer[7];
    uint8_t r_cmd[] = {0x0d0, 0x03, 0x00, 0x00};

    while (retry--) {
        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(r_cmd, sizeof(r_cmd));
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom(static_cast<int>(CST3530_ADDR), 7);
            Wire.readBytes(buffer, sizeof(buffer));
            if (buffer[2] == 0xCA && buffer[3] == 0xCA) {
                LOG_DEBUG("CST3530 detected");
                is_cst3530 = true;
                pinMode(CST328_PIN_INT, INPUT);
                attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchInterruptHandler, FALLING);
                break;
            }
        }

        uint8_t cmd1[] = {0xD0, 0x00, 0x04, 0x00};
        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(cmd1, sizeof(cmd1));
        Wire.endTransmission();
        delay(50);
    }

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}

void lateInitVariant()
{
    tDeckProLateInit();
}

#endif
