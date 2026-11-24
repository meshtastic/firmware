#pragma once

// LR20x0Interface::isChannelActive() in src/mesh/LR20x0Interface.cpp, run against the modelled LR2021
// in lr2021_tests.h. The tests there pin what RadioLib sends for a given config; these pin the config
// the firmware builds. A detected preamble must be reported busy, and that needs a CAD the chip
// accepts. develop sends exit byte 0x01 from its own local CAD_EXIT_MODE_RX and is fine; an interface
// that sent RadioLib's RADIOLIB_LR2021_CAD_EXIT_MODE_RX (0x02) instead would have its scan refused
// (-706), read the error as a free channel, and transmit over live frames - 16/16 on the bench, and
// 539 scans without a busy verdict on a Mesh-Tracker X1. These tests fail on that change.

#include "RecordingHal.h"
#include "TestUtil.h"
#include "lr2021_tests.h"
#include "mesh/LR2021Interface.h"
#include "mesh/MeshRadio.h"
#include "mesh/NodeDB.h"
#include "platform/portduino/PortduinoGlue.h"

// A LockingArduinoHal, as the interface requires, that forwards everything to the suite's RecordingHal.
// spiBeginTransaction() skips the base class, which takes spiLock: nothing here shares the bus.
class RecordingLockingHal : public LockingArduinoHal
{
  public:
    explicit RecordingLockingHal(RecordingHal &rec) : LockingArduinoHal(SPI, SPISettings()), rec(rec) {}

    void init() override {}
    void term() override {}
    void pinMode(uint32_t pin, uint32_t mode) override { rec.pinMode(pin, mode); }
    void digitalWrite(uint32_t pin, uint32_t value) override { rec.digitalWrite(pin, value); }
    uint32_t digitalRead(uint32_t pin) override { return rec.digitalRead(pin); }
    void attachInterrupt(uint32_t num, void (*cb)(void), uint32_t mode) override { rec.attachInterrupt(num, cb, mode); }
    void detachInterrupt(uint32_t num) override { rec.detachInterrupt(num); }
    void delay(RadioLibTime_t ms) override { rec.delay(ms); }
    void delayMicroseconds(RadioLibTime_t us) override { rec.delayMicroseconds(us); }
    RadioLibTime_t millis() override { return rec.millis(); }
    RadioLibTime_t micros() override { return rec.micros(); }
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override { return rec.pulseIn(pin, state, timeout); }
    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override { rec.spiTransfer(out, len, in); }
    void spiEndTransaction() override {}
    void spiEnd() override {}
    void yield() override {}
    uint32_t pinToInterrupt(uint32_t pin) override { return pin; }

  private:
    RecordingHal &rec;
};

static constexpr uint32_t LR20X0_TEST_IRQ_PIN = 7;

class TestLr2021Interface : public LR2021Interface
{
  public:
    explicit TestLr2021Interface(RecordingLockingHal *hal)
        : LR2021Interface(hal, 1, LR20X0_TEST_IRQ_PIN, RADIOLIB_NC, RADIOLIB_NC)
    {
    }

    using LR20x0Interface<LR2021>::isChannelActive;
    using LR20x0Interface<LR2021>::launchTransmit;

    // init() would set the SF via begin(); RadioLib's default det_peak lookup needs one (see lr2021SetSf).
    void setSpreadingFactor(uint8_t sf) { TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, lora.setSpreadingFactor(sf)); }

    // begin() also tunes before it writes the SF, and that ordering is load-bearing: the SF write ends
    // in the DC-DC workaround's closing setFrequency(freqMHz), so without a frequency it fails with
    // INVALID_FREQUENCY (see lr2021Tune).
    void tune(RecordingHal &hal) { lr2021Tune(hal, lora); }
};

// One interface for the whole suite, never destroyed: the constructor registers a worker thread and
// sets RadioLibInterface::instance, and a failed assertion would skip a test-local destructor anyway.
// Each test gets the shared HAL reset, the chip modelled, and its IRQ line high so scanChannel()
// returns.
static TestLr2021Interface &lr20x0Interface(RecordingHal &hal)
{
    static RecordingLockingHal lockingHal(hal);
    // RadioInterface's constructor computes the slot time from myRegion, which nothing else here sets.
    static TestLr2021Interface *iface = [] {
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
        initRegion();
        return new TestLr2021Interface(&lockingHal);
    }();
    hal.reply(op16(RADIOLIB_LR2021_CMD_GET_PACKET_TYPE), 0x04, {0x04, RADIOLIB_LR2021_PACKET_TYPE_LORA}, 1);
    lr2021ModelCadExitCheck(hal);
    hal.irqPin = LR20X0_TEST_IRQ_PIN;
    iface->tune(hal);
    iface->setSpreadingFactor(9);
    hal.transactions.clear();
    return *iface;
}

// The exit byte the interface sends is one the chip takes, and the CAD it asks for actually starts.
static void test_lr20x0_isChannelActive_starts_a_cad_the_chip_accepts()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    lr2021AnswerIrq(hal, RADIOLIB_LR2021_IRQ_CAD_DONE);
    (void)iface.isChannelActive();
    const auto &t = lr2021CadParams(hal);
    TEST_ASSERT_TRUE_MESSAGE(lr2021AcceptsLoraCadExit(t[4]), "SetLoraCadParams exit byte the LR2021 refuses (0x02 -> -706)");
    TEST_ASSERT_EQUAL_UINT32(1, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD)));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_CAD)));
}

// The bench failure as a test: a preamble on the air must read as busy, so the caller defers.
static void test_lr20x0_isChannelActive_reports_a_detected_preamble_busy()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    lr2021AnswerIrq(hal, RADIOLIB_LR2021_IRQ_CAD_DETECTED | RADIOLIB_LR2021_IRQ_CAD_DONE);
    TEST_ASSERT_TRUE_MESSAGE(iface.isChannelActive(), "CAD detected a preamble, interface reported the channel free");
}

static void test_lr20x0_isChannelActive_reports_cad_done_free()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    lr2021AnswerIrq(hal, RADIOLIB_LR2021_IRQ_CAD_DONE);
    TEST_ASSERT_FALSE(iface.isChannelActive());
}

// A SetTx the chip refuses must fail the launch, so startSend() takes its error path at once instead of
// waiting for a TX_DONE that never comes. Without paranoid SPI, only launchTransmit()'s status read finds it.
static void test_lr20x0_launchTransmit_reports_a_refused_settx()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    hal.rejectUnless(op16(RADIOLIB_LR2021_CMD_SET_TX), [](const std::vector<uint8_t> &) { return false; }, 0x02);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_SPI_CMD_INVALID, iface.launchTransmit(8));
    TEST_ASSERT_EQUAL_UINT32(1, hal.count(op16(RADIOLIB_LR2021_CMD_SET_TX)));
}

// RadioLibInterface::randomBytes() (src/mesh/RadioLibInterface.cpp) behind the serial HAL (Lora.spidev:
// serial). Each byte is four GetRandomNumber commands, and behind the HAL each command is several serial
// round trips, so PKI key generation on a fresh host stalled for minutes. randomBytes() must decline
// without touching the chip; HardwareRNG::fill() has already filled the buffer from the host OS RNG.
// The control keeps that zero meaningful: on any other spidev the same call does reach the modem.
static void test_randomBytes_declines_behind_the_serial_hal()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    const std::string savedSpiDev = portduino_config.lora_spi_dev;
    portduino_config.lora_spi_dev = "serial";
    uint8_t buf[32] = {};
    const bool produced = iface.randomBytes(buf, sizeof(buf));
    const size_t randomCommands = hal.count(op16(RADIOLIB_LR2021_CMD_GET_RANDOM_NUMBER));
    portduino_config.lora_spi_dev = savedSpiDev; // before asserting: a failure longjmps past this line
    TEST_ASSERT_FALSE_MESSAGE(produced, "randomBytes() reported modem entropy behind the serial HAL");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, randomCommands, "randomBytes() queried the chip behind the serial HAL");
}

static void test_randomBytes_reads_the_modem_on_other_spidevs()
{
    RecordingHal &hal = freshHal();
    auto &iface = lr20x0Interface(hal);
    const std::string savedSpiDev = portduino_config.lora_spi_dev;
    portduino_config.lora_spi_dev = "/dev/spidev0.0";
    uint8_t buf[1] = {};
    (void)iface.randomBytes(buf, sizeof(buf));
    const size_t randomCommands = hal.count(op16(RADIOLIB_LR2021_CMD_GET_RANDOM_NUMBER));
    portduino_config.lora_spi_dev = savedSpiDev;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, randomCommands, "one random byte is four GetRandomNumber commands");
}

// Grows here:
//   a scan that returns an error (-706, -707, -705) must not read as a free channel. Today every
//   driver's isChannelActive() returns false on it; which verdict replaces that is undecided.

static void runLr20x0InterfaceTests()
{
    RUN_TEST(test_lr20x0_isChannelActive_starts_a_cad_the_chip_accepts);
    RUN_TEST(test_lr20x0_isChannelActive_reports_a_detected_preamble_busy);
    RUN_TEST(test_lr20x0_isChannelActive_reports_cad_done_free);
    RUN_TEST(test_lr20x0_launchTransmit_reports_a_refused_settx);
    RUN_TEST(test_randomBytes_declines_behind_the_serial_hal);
    RUN_TEST(test_randomBytes_reads_the_modem_on_other_spidevs);
}
