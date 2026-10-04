// Unit tests for RadioLib's LR2021 DC-DC sensitivity workaround, LR2021::setDCDCworkaround() in
// RadioLib's src/modules/LR2021/LR2021_cmds_chip_control.cpp, as pinned in platformio.ini.
//
// RadioLib runs the workaround itself, from setRxPath(), after every successful SET_RX_PATH, on
// either regulator; LR20x0Interface reaches it through setRxBoostedGainMode(). It reads DCDC_ADC_CTRL
// with readRegMem32() and writes DCDC_FREQ_LF with writeRegMem32(). Both count `len` in 32-bit
// words, and each call here moves exactly one: one register, one uint32_t local.
//
// The regression guarded is the one jgromes/RadioLib#1864 introduced: `sizeof(uint32_t)` passed as
// that word count. The read then writes 12 bytes of stack past the local, and the write sends 12
// bytes of adjacent stack to the three registers after DCDC_FREQ_LF. Under [env:coverage]
// (-fsanitize=address) the call itself aborts with stack-buffer-overflow. Without ASan the
// assertions below still fail, on the word count the chip is asked for.
//
// No chip is needed: a recording HAL answers every SPI byte with a CMD_OK status, which is all the
// driver checks before running the workaround. resetDCDCworkaround() makes the same write call but is
// only reachable through begin(), whose chip bring-up this HAL does not emulate.
#include "TestUtil.h"
#include <RadioLib.h>
#include <modules/LR2021/LR2021_registers.h>
#include <unity.h>

#include <cstring>
#include <vector>

// Records every SPI transaction and answers each byte with an LRxxxx CMD_OK status (0x04).
class RecordingHal : public RadioLibHal
{
  public:
    RecordingHal() : RadioLibHal(0, 1, 0, 1, 2, 3) {}

    std::vector<std::vector<uint8_t>> transactions;

    void pinMode(uint32_t, uint32_t) override {}
    void digitalWrite(uint32_t, uint32_t) override {}
    uint32_t digitalRead(uint32_t) override { return 0; } // BUSY low
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override {}
    void detachInterrupt(uint32_t) override {}
    void delay(RadioLibTime_t) override {}
    void delayMicroseconds(RadioLibTime_t) override {}
    RadioLibTime_t millis() override { return 0; }
    RadioLibTime_t micros() override { return 0; }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }
    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override
    {
        transactions.emplace_back(out, out + len);
        memset(in, 0x04, len);
    }
    void spiEndTransaction() override {}
    void spiEnd() override {}
};

static constexpr uint32_t kCs = 1, kBusy = 2;

// The first transaction carrying `opcode` for register `addr`: 16-bit opcode, then a 24-bit address.
static const std::vector<uint8_t> *findRegMemAccess(const RecordingHal &hal, uint16_t opcode, uint32_t addr)
{
    for (const auto &t : hal.transactions) {
        if (t.size() >= 5 && t[0] == (opcode >> 8) && t[1] == (opcode & 0xFF) && t[2] == ((addr >> 16) & 0xFF) &&
            t[3] == ((addr >> 8) & 0xFF) && t[4] == (addr & 0xFF))
            return &t;
    }
    return nullptr;
}

static void runWorkaround(RecordingHal &hal)
{
    Module mod(&hal, kCs, RADIOLIB_NC, RADIOLIB_NC, kBusy);
    LR2021 radio(&mod);
    // setRxBoostedGainMode() -> setRxPath() -> setDCDCworkaround(). Its return is not the point:
    // the workaround ends by re-applying a frequency this unconfigured instance never set.
    (void)radio.setRxBoostedGainMode(0);
}

void setUp(void) {}
void tearDown(void) {}

static void test_setRxPath_runs_the_dcdc_workaround()
{
    RecordingHal hal;
    runWorkaround(hal);
    // Guards the premise: if RadioLib stops calling the workaround from setRxPath(), the two tests
    // below would pass vacuously on the null checks.
    TEST_ASSERT_NOT_NULL(findRegMemAccess(hal, RADIOLIB_LR2021_CMD_READ_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_ADC_CTRL));
    TEST_ASSERT_NOT_NULL(findRegMemAccess(hal, RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_FREQ_LF));
}

static void test_dcdc_adc_ctrl_read_asks_for_one_word()
{
    RecordingHal hal;
    runWorkaround(hal);
    const auto *req = findRegMemAccess(hal, RADIOLIB_LR2021_CMD_READ_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_ADC_CTRL);
    TEST_ASSERT_NOT_NULL(req);
    // READ_REG_MEM_32 request: opcode(2) + address(3) + word count(1).
    TEST_ASSERT_EQUAL_UINT32(6, req->size());
    TEST_ASSERT_EQUAL_UINT8(1, (*req)[5]);
}

static void test_dcdc_freq_lf_write_sends_one_word()
{
    RecordingHal hal;
    runWorkaround(hal);
    const auto *wr = findRegMemAccess(hal, RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_FREQ_LF);
    TEST_ASSERT_NOT_NULL(wr);
    // WRITE_REG_MEM_32: opcode(2) + address(3) + one data word(4). Four words is 21 bytes.
    TEST_ASSERT_EQUAL_UINT32(2 + 3 + 4, wr->size());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_setRxPath_runs_the_dcdc_workaround);
    RUN_TEST(test_dcdc_adc_ctrl_read_asks_for_one_word);
    RUN_TEST(test_dcdc_freq_lf_write_sends_one_word);
    exit(UNITY_END());
}

void loop() {}
