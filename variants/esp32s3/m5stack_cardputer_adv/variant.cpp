#include "AudioBoard.h"
#include "configuration.h"

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

// M5stack Cardputer ADV specific init

void lateInitVariant()
{
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
