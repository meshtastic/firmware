#include "driver/gpio.h"
#include <Arduino.h>
#include <Wire.h>
// I2C device addr
#define PI4IO_M_ADDR 0x43

// PI4IO registers
#define PI4IO_REG_CHIP_RESET 0x01
#define PI4IO_REG_IO_DIR 0x03
#define PI4IO_REG_OUT_SET 0x05
#define PI4IO_REG_OUT_H_IM 0x07
#define PI4IO_REG_IN_DEF_STA 0x09
#define PI4IO_REG_PULL_EN 0x0B
#define PI4IO_REG_PULL_SEL 0x0D
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_INT_MASK 0x11
#define PI4IO_REG_IRQ_STA 0x13
// PI4IO

#define setbit(x, y) x |= (0x01 << y)
#define clrbit(x, y) x &= ~(0x01 << y)
#define reversebit(x, y) x ^= (0x01 << y)
#define getbit(x, y) ((x) >> (y)&0x01)

void i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(addr, 1);
    *value = Wire.read();
}

/*******************************************************************/
void i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}
/*******************************************************************/
void c6l_init()
{
    // P7 LoRa Reset
    // P6 RF Switch
    // P5 LNA Enable

    printf("pi4io_init\n");
    uint8_t in_data;
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_CHIP_RESET, 0xFF);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_read_byte(PI4IO_M_ADDR, PI4IO_REG_CHIP_RESET, &in_data);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_IO_DIR, 0b11100000); // P5,P6,P7 as outputs
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_OUT_H_IM, 0b00011100); // High-Impedance
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_PULL_SEL, 0b11100011); // pull up/down select, 0 down, 1 up
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_PULL_EN, 0b11100011); // pull up/down enable, 0 disable, 1 enable
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_IN_DEF_STA, 0b00000011); // P0 P1 default to high level; button press triggers the interrupt
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_INT_MASK, 0b11111100); // P0 P1 interrupts enabled (0 = enable, 1 = disable)
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_OUT_SET, 0b10000000); // default output is 0
    vTaskDelay(10 / portTICK_PERIOD_MS);
    i2c_read_byte(PI4IO_M_ADDR, PI4IO_REG_IRQ_STA, &in_data); // read IRQ_STA to clear the flag
    vTaskDelay(10 / portTICK_PERIOD_MS);

    i2c_read_byte(PI4IO_M_ADDR, PI4IO_REG_OUT_SET, &in_data);
    setbit(in_data, 6); // enable SX_ANT_SW
    setbit(in_data, 5); // enable SX_LNA_EN
    i2c_write_byte(PI4IO_M_ADDR, PI4IO_REG_OUT_SET, in_data);
}
