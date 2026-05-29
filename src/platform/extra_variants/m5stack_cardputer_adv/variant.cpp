#include "configuration.h"

#ifdef M5STACK_CARDPUTER_ADV

#include "AudioBoard.h"
#include <Wire.h>

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

// PI4IOE5V6408 on the optional Cap LoRa-1262 (and Cap LoRa868).
#define PI4IO_ADDR 0x43
#define PI4IO_REG_IO_DIR 0x03
#define PI4IO_REG_OUT_SET 0x05
#define PI4IO_REG_OUT_H_IM 0x07

static TwoWire *findLoraCapBus()
{
    TwoWire *candidates[] = {&Wire1, &Wire};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        candidates[i]->beginTransmission(PI4IO_ADDR);
        if (candidates[i]->endTransmission() == 0) {
            return candidates[i];
        }
    }
    return nullptr;
}

static bool pi4ioWrite(TwoWire *bus, uint8_t reg, uint8_t val)
{
    bus->beginTransmission(PI4IO_ADDR);
    bus->write(reg);
    bus->write(val);
    uint8_t status = bus->endTransmission();
    if (status != 0) {
        LOG_DEBUG("PI4IO write reg=0x%02x val=0x%02x failed, I2C status=%u", reg, val, status);
        return false;
    }
    return true;
}

static void initLoraCap()
{
    TwoWire *bus = findLoraCapBus();
    if (!bus) {
        LOG_ERROR("Cap LoRa-1262 not found");
        return;
    }
    bool ok = pi4ioWrite(bus, PI4IO_REG_IO_DIR, 0b00000001);
    ok = ok && pi4ioWrite(bus, PI4IO_REG_OUT_H_IM, 0b00000000);
    ok = ok && pi4ioWrite(bus, PI4IO_REG_OUT_SET, 0b00000001);
    if (!ok) {
        LOG_ERROR("Antenna switch init failed");
    }
}

// M5stack Cardputer ADV specific init

void lateInitVariant()
{
    initLoraCap();

    // AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Debug);
    //  I2C: function, scl, sda
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    // I2S: function, mclk, bck, ws, data_out, data_in
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    // configure codec
    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    board.begin(cfg);

    // extra ES8311 init
    auto es8311_write_reg = [](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18); // ES8311 i2c address
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    };
    es8311_write_reg(0x00, 0x80); // reset, power on
    es8311_write_reg(0x01, 0xB5); // MCLK = BCLK
    es8311_write_reg(0x02, 0x18); // CLOCK_MANAGER/ MULT_PRE=3
    es8311_write_reg(0x0D, 0x01); // analog power up
    es8311_write_reg(0x12, 0x00); // DAC power up
    es8311_write_reg(0x13, 0x10); // enable HP drive
    es8311_write_reg(0x32, 0xBF); // DAC volume (0dB)
    es8311_write_reg(0x37, 0x08); // EQ bypass
}

#endif
