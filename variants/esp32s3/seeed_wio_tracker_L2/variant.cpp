#include "variant.h"
#include "TCA9555.h"
#include "AudioBoard.h"
#include "DebugConfiguration.h"
#include "mesh/MeshLED.h"

TCA9535 ioExpander(0x21);
DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

class WioTrackerMeshLED : public MeshLED
{
  public:
    void init() override { ioExpander.write1(10, LOW); } // ensure LED starts off
    void on() override { ioExpander.write1(10, HIGH); }
    void off() override { ioExpander.write1(10, LOW); }
};

static bool initOK = false;

void initVariant()
{
    Wire.begin(I2C_SDA, I2C_SCL);
    if (ioExpander.begin()) {
        ioExpander.pinMode1(0, INPUT); // wakeup button
        ioExpander.pinMode1(1, INPUT); // IIC IRQ
        ioExpander.pinMode1(2, INPUT); // SD detect

        ioExpander.pinMode1(11, OUTPUT); // OTG EN
        ioExpander.write1(11, LOW); // OTG EN low
        delay(10);
        ioExpander.pinMode1(12, OUTPUT); // PA EN
        ioExpander.write1(12, HIGH); // PA EN high
        delay(10);
        ioExpander.pinMode1(14, OUTPUT); // TF EN
        ioExpander.write1(14, HIGH); // TF EN high
        delay(10);
        ioExpander.pinMode1(15, OUTPUT); // BAT ADC EN
        ioExpander.write1(15, HIGH); // BAT ADC EN high
        delay(10);
        ioExpander.pinMode1(13, OUTPUT); // GNSS EN
        ioExpander.write1(13, HIGH); // GNSS EN high
        delay(10);
        ioExpander.pinMode1(9, OUTPUT); // GNSS RST (active HIGH on this board)
        // Expander output defaults to HIGH, so module is already in reset.
        // Hold reset for 10ms, then release LOW so the module starts running.
        delay(10);
        ioExpander.write1(9, LOW); // release reset - module starts running
        ioExpander.pinMode1(10, OUTPUT); // User LED / GNSS wakeup
        ioExpander.write1(10, LOW); // User LED / GNSS wakeup low
        delay(10);
        ioExpander.pinMode1(7, OUTPUT); // GROVE EN
        ioExpander.write1(7, HIGH); // GROVE EN high
        delay(10);

        ioExpander.pinMode1(5, OUTPUT); // LCD EN
        ioExpander.write1(5, HIGH); // LCD EN high
        delay(50);
        ioExpander.pinMode1(6, OUTPUT); // LCD RST
        ioExpander.write1(6, HIGH); // LCD RST high
        delay(5);
        ioExpander.write1(6, LOW); // LCD RST low
        delay(10);
        ioExpander.write1(6, HIGH); // LCD RST high
        delay(500);
        ioExpander.pinMode1(4, OUTPUT); // LCD CS
        ioExpander.write1(4, HIGH); // LCD CS high
        delay(10);

        ioExpander.pinMode1(8, OUTPUT); // TP RST
        ioExpander.write1(8, LOW); // TP RST low
        ioExpander.pinMode1(3, OUTPUT); // TP INT
        ioExpander.write1(3, LOW); // TP INT low
        delay(10);
        ioExpander.write1(3, LOW); // TP INT low
        delay(1);
        ioExpander.write1(8, HIGH); // TP RST high
        delay(60);
        initOK = true;

        meshLED = std::make_shared<WioTrackerMeshLED>();
        meshLED->init();
    }
}

void lateInitVariant()
{
    if (!initOK) {
        LOG_ERROR("TCA9535 initialization failed");
        return;
    }
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
    LOG_INFO("ES8311 Audio board initialized");
}
