#include "configuration.h"

#ifdef MESHNOLOGY_W10

#include <Wire.h>

#ifdef HAS_I2S
// NOTE: do not include main.h / AudioThread.h here. AudioBoard.h does `using namespace audio_driver`,
// which pulls audio_driver::GpioPin into global scope and collides with Meshtastic's class GpioPin if
// GpioLogic.h is also visible in this TU. Keeping this file codec-only (like the other ES8311 boards)
// avoids that.
#include "AudioBoard.h"
#include "platform/esp32/ExtensionIOMCP23017.h" // mcpIoExpander (NS4150 amp enable on EXIO_PA_CTRL)

DriverPins PinsAudioBoardES8311;
AudioBoard audioCodecBoard(AudioDriverES8311, PinsAudioBoardES8311);
#endif

// Meshnology W10 late init: bring up the ES8311 codec so the NS4150 -> speaker path can play
// notification tones over I2S. Called after power->setup() and the radio init, so the I2C bus and
// the MCP23017 are already up. The amp itself is toggled by AudioThread around playback.
void lateInitVariant()
{
#ifdef HAS_I2S
    // Keep the NS4150 amp muted until AudioThread turns it on for playback (avoids idle hiss).
    mcpIoExpander.pinMode(EXIO_PA_CTRL, OUTPUT);
    mcpIoExpander.digitalWrite(EXIO_PA_CTRL, LOW);

    // I2C: function, Wire (shared bus, ES8311 at 0x18); I2S: function, mclk, bck, ws, dout, din
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    audioCodecBoard.begin(cfg);

    // ES8311 register setup (matches the vendor demo / other Meshtastic ES8311 boards)
    auto es8311_write_reg = [](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18); // ES8311 I2C address
        Wire.write(reg);
        Wire.write(val);
        uint8_t err = Wire.endTransmission();
        if (err != 0)
            LOG_WARN("ES8311 reg 0x%02x write failed (err=%d)", reg, err);
    };
    es8311_write_reg(0x00, 0x80); // reset, power on
    es8311_write_reg(0x01, 0xB5); // MCLK = BCLK
    es8311_write_reg(0x02, 0x18); // clock manager, MULT_PRE=3
    es8311_write_reg(0x0D, 0x01); // analog power up
    es8311_write_reg(0x12, 0x00); // DAC power up
    es8311_write_reg(0x13, 0x10); // enable HP drive
    es8311_write_reg(0x32, 0xBF); // DAC volume (0 dB)
    es8311_write_reg(0x37, 0x08); // EQ bypass
    LOG_INFO("Meshnology W10: ES8311 audio codec initialized");
#endif // HAS_I2S
}

#endif // MESHNOLOGY_W10
