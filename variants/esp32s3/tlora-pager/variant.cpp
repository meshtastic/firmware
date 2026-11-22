#include "configuration.h"

#ifdef T_LORA_PAGER

#include "AudioBoard.h"
#include "IoExpanderXL9555.hpp"

extern IoExpanderXL9555 io;

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(KB_INT, INPUT_PULLUP);
    // io expander
    io.begin(Wire, XL9555_SLAVE_ADDRESS0, SDA, SCL);
    io.pinMode(EXPANDS_DRV_EN, OUTPUT);
    io.digitalWrite(EXPANDS_DRV_EN, HIGH);
    io.pinMode(EXPANDS_AMP_EN, OUTPUT);
    io.digitalWrite(EXPANDS_AMP_EN, LOW);
    io.pinMode(EXPANDS_LORA_EN, OUTPUT);
    io.digitalWrite(EXPANDS_LORA_EN, HIGH);
    io.pinMode(EXPANDS_GPS_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPS_EN, HIGH);
    io.pinMode(EXPANDS_KB_EN, OUTPUT);
    io.digitalWrite(EXPANDS_KB_EN, HIGH);
    io.pinMode(EXPANDS_SD_EN, OUTPUT);
    io.digitalWrite(EXPANDS_SD_EN, HIGH);
    io.pinMode(EXPANDS_GPIO_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPIO_EN, HIGH);
    io.pinMode(EXPANDS_SD_PULLEN, INPUT);
}

// TLora Pager specific init
void lateInitVariant()
{
    // AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Debug);
    // I2C: function, scl, sda
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
    board.setVolume(75); // 75% volume
}
#endif
