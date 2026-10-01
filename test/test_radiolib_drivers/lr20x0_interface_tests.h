#pragma once

// LR20x0Interface::isChannelActive() in src/mesh/LR20x0Interface.cpp, run against the modelled LR2021
// in lr2021_tests.h. The tests there pin what RadioLib sends for a given config; these pin the config
// the firmware builds. A detected preamble must be reported busy, and that needs a CAD the chip
// accepts: an interface that passes RADIOLIB_LR2021_CAD_EXIT_MODE_RX (0x02) has its scan refused
// (-706), reads the error as a free channel, and transmits over live frames (16/16 on the bench, and
// 539 scans without a busy verdict on a Mesh-Tracker X1). These fail on that change.

#include "RecordingHal.h"
#include "TestUtil.h"
#include "lr2021_tests.h"
#include "mesh/LR2021Interface.h"
#include "mesh/MeshRadio.h"
#include "mesh/NodeDB.h"

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

    // init() would set these via begin(): the default det_peak lookup needs an SF (see lr2021SetSf), and
    // from 7.8.0 the SF change runs the DC-DC workaround, which retunes to the set frequency.
    void setSpreadingFactor(uint8_t sf)
    {
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, lora.setFrequency(915.0));
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, lora.setSpreadingFactor(sf));
    }
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

// Grows here:
//   a scan that returns an error (-706, -707, -705) must not read as a free channel. Today every
//   driver's isChannelActive() returns false on it; which verdict replaces that is undecided.

static void runLr20x0InterfaceTests()
{
    RUN_TEST(test_lr20x0_isChannelActive_starts_a_cad_the_chip_accepts);
    RUN_TEST(test_lr20x0_isChannelActive_reports_a_detected_preamble_busy);
    RUN_TEST(test_lr20x0_isChannelActive_reports_cad_done_free);
}
