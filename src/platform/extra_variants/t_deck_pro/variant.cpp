#include "configuration.h"

#ifdef T_DECK_PRO

#include "input/TouchScreenImpl1.h"
#include <CSE_CST328.h>
#include <Wire.h>

CSE_CST328 tsPanel = CSE_CST328(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);

static bool is_cst3530 = false;
volatile bool touch_isr = false;
#define CST3530_ADDR        0x1A

bool read_cst3530_touch(int16_t *x, int16_t *y) {
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

    uint8_t report_typ = buffer[2];
    if (report_typ != 0xFF) {
        return false;
    }

    uint8_t touch_points = buffer[3] & 0x0F;
    if (touch_points == 0 || touch_points > 1) { 
        LOG_DEBUG("CST3530 touch points invalid: %d", touch_points);
        return false;
    }

    *x = buffer[4] + ((uint16_t)(buffer[7] & 0x0F) << 8); 
    *y = buffer[5] + ((uint16_t)(buffer[7] & 0xF0) << 4); 

    // LOG_DEBUG("CST3530 touch: num:%d x=%d,y=%d", touch_points, *x, *y);

    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(clear_cmd, sizeof(clear_cmd));
    if (Wire.endTransmission() != 0) {
        LOG_DEBUG("CST3530 clear cmd failed");
    }

    return true;
}

bool readTouch(int16_t *x, int16_t *y)
{

    if(is_cst3530){
        if(touch_isr){
            touch_isr = false;
            return read_cst3530_touch(x, y);
        }
        return false;
    }else{
        if (tsPanel.getTouches()) {
            *x = tsPanel.getPoint(0).x;
            *y = tsPanel.getPoint(0).y;
            return true;
        }
    }
    return false;
}


static void touchInterruptHandler(){
    touch_isr = true;
}

// T-Deck Pro specific init
void lateInitVariant()
{
    // Reset touch
    pinMode(CST328_PIN_RST, OUTPUT);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);
    digitalWrite(CST328_PIN_RST, LOW);
    delay(80);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);

    int retry = 5;
    uint8_t buffer[7];
    uint8_t r_cmd[] = {0x0d0,0x03,0x00,0x00};

    // Probe touch chip
    while(retry--) {
        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(r_cmd, sizeof(r_cmd));
        if(Wire.endTransmission() == 0){
            Wire.requestFrom((int)CST3530_ADDR,7);
            Wire.readBytes(buffer,7);
            if(buffer[2] == 0xCA && buffer[3] == 0xCA){
                LOG_DEBUG("CST3530 detected");
                is_cst3530 = true;

                // The CST3530 will automatically enter sleep mode; 
                // polling should not be used, but rather an interrupt method should be employed. 
                pinMode(CST328_PIN_INT, INPUT);
                attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchInterruptHandler, FALLING);

                break;
            }else{
                LOG_DEBUG("CST3530 not response ~!");
            }
        }
        uint8_t cmd1[] = {0xD0,0x00,0x04,0x00};
        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(cmd1, sizeof(cmd1));
        Wire.endTransmission();
        delay(50);
    }

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}
#endif