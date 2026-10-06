#pragma once

// LR2021: the calls LR20x0Interface makes, minus begin(). The DC-DC workaround tests come first:
// under ASan the first overrun aborts the program, and it should abort on the test that names it.

#include "RecordingHal.h"
#include "TestUtil.h"
#include <modules/LR2021/LR2021_registers.h>

#include <type_traits>
#include <utility>

// jgromes/RadioLib#1864 added the DC-DC workaround together with a public setRegulatorDCDC(), so that
// method marks a RadioLib the DC-DC tests apply to. Earlier pins (7.7.1 and before) have neither.
template <typename T, typename = void> struct HasDcdcWorkaround : std::false_type {
};
template <typename T>
struct HasDcdcWorkaround<T, std::void_t<decltype(std::declval<T &>().setRegulatorDCDC())>> : std::true_type {
};

// begin() sets a frequency before anything else; without one the DC-DC workaround's closing
// setFrequency(freqMHz) fails with INVALID_FREQUENCY. The log starts after it.
static void lr2021Tune(RecordingHal &hal, LR2021 &radio)
{
    (void)radio.setFrequency(915.0);
    hal.transactions.clear();
}

// As LR11x0: the packet type comes back in the data byte of the next transaction. 0x00 is LoRa.
#define LR2021_RADIO(hal)                                                                                                        \
    RecordingHal &hal = freshHal();                                                                                              \
    hal.reply(op16(RADIOLIB_LR2021_CMD_GET_PACKET_TYPE), 0x04, {0x04, RADIOLIB_LR2021_PACKET_TYPE_LORA}, true);                  \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, 2);                                                                            \
    LR2021 radio(&mod);                                                                                                          \
    lr2021Tune(hal, radio)

// The first transaction carrying `opcode` for register `addr`: 16-bit opcode, then a 24-bit address.
static const std::vector<uint8_t> *lr2021RegMemAccess(const RecordingHal &hal, uint16_t opcode, uint32_t addr)
{
    return hal.first({static_cast<uint8_t>(opcode >> 8), static_cast<uint8_t>(opcode & 0xFF),
                      static_cast<uint8_t>((addr >> 16) & 0xFF), static_cast<uint8_t>((addr >> 8) & 0xFF),
                      static_cast<uint8_t>(addr & 0xFF)});
}

// setRxBoostedGainMode() -> setRxPath() -> setDCDCworkaround(), the route LR20x0Interface takes.
static void lr2021RunDcdcWorkaround(RecordingHal &hal, LR2021 &radio)
{
    (void)hal;
    (void)radio.setRxBoostedGainMode(0);
}

static void test_lr2021_setRxPath_runs_the_dcdc_workaround()
{
    LR2021_RADIO(hal);
    lr2021RunDcdcWorkaround(hal, radio);
    // Guards the premise: without these the two word-count tests below would pass vacuously.
    TEST_ASSERT_NOT_NULL(lr2021RegMemAccess(hal, RADIOLIB_LR2021_CMD_READ_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_ADC_CTRL));
    TEST_ASSERT_NOT_NULL(lr2021RegMemAccess(hal, RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_FREQ_LF));
}

static void test_lr2021_dcdc_adc_ctrl_read_asks_for_one_word()
{
    LR2021_RADIO(hal);
    lr2021RunDcdcWorkaround(hal, radio);
    const auto *req = lr2021RegMemAccess(hal, RADIOLIB_LR2021_CMD_READ_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_ADC_CTRL);
    TEST_ASSERT_NOT_NULL(req);
    // READ_REG_MEM_32 request: opcode(2) + address(3) + word count(1)
    TEST_ASSERT_EQUAL_UINT32(6, req->size());
    TEST_ASSERT_EQUAL_UINT8(1, (*req)[5]);
}

static void test_lr2021_dcdc_freq_lf_write_sends_one_word()
{
    LR2021_RADIO(hal);
    lr2021RunDcdcWorkaround(hal, radio);
    const auto *wr = lr2021RegMemAccess(hal, RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32, RADIOLIB_LR2021_REG_DCDC_FREQ_LF);
    TEST_ASSERT_NOT_NULL(wr);
    // WRITE_REG_MEM_32: opcode(2) + address(3) + one data word(4); four words is 21 bytes
    TEST_ASSERT_EQUAL_UINT32(2 + 3 + 4, wr->size());
}

static void test_lr2021_setFrequency_sends_hertz()
{
    LR2021_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setFrequency(915.0));
    const auto *t = hal.first(op16(RADIOLIB_LR2021_CMD_SET_RF_FREQUENCY));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(6, t->size());
    const uint8_t hz[] = {0x36, 0x89, 0xCA, 0xC0}; // 915000000
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hz, t->data() + 2, 4);
}

static void test_lr2021_lora_modulation_setters_send_modulation_params()
{
    LR2021_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(9));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setBandwidth(250.0));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    TEST_ASSERT_EQUAL_UINT32(3, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_MODULATION_PARAMS)));
}

// LR20x0Interface passes cr != 7 as the long-interleave flag: 4/7 has no long-interleaver code.
static void test_lr2021_coding_rate_long_interleaves_except_4_7()
{
    LR2021_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    const auto *t = hal.last(op16(RADIOLIB_LR2021_CMD_SET_LORA_MODULATION_PARAMS));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(4, t->size());   // opcode(2) + sf|bw, cr|ldro
    TEST_ASSERT_EQUAL_UINT8(5, (*t)[3] >> 4); // 4/5, long interleaver
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(7, false));
    t = hal.last(op16(RADIOLIB_LR2021_CMD_SET_LORA_MODULATION_PARAMS));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT8(3, (*t)[3] >> 4); // 4/7, short interleaver
}

static void test_lr2021_packet_setters_send_packet_params()
{
    LR2021_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setPreambleLength(16));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCRC(2));
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_PACKET_PARAMS)) >= 2);
}

static void test_lr2021_other_setters_and_modes_succeed()
{
    LR2021_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSyncWord(0x2B));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setOutputPower(22));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.standby());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startReceive());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.sleep());
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_SYNCWORD)) >= 1);
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR2021_CMD_SET_TX_PARAMS)) >= 1);
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR2021_CMD_SET_RX)) >= 1);
}

// Grows here - each needs replies scripted per opcode:
//   begin(): getVersion() check and the calibration sequence
//   resetDCDCworkaround() via setPacketType(): only begin() reaches it; same one-word write as above
//   setPaTable(): custom LF table entry chosen per output power (cf. meshtastic/firmware#11980)
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   scanChannel(): the CAD parameters sent (LR2021 has two CAD commands)

static void runLr2021Tests()
{
    if constexpr (HasDcdcWorkaround<LR2021>::value) {
        RUN_TEST(test_lr2021_setRxPath_runs_the_dcdc_workaround);
        RUN_TEST(test_lr2021_dcdc_adc_ctrl_read_asks_for_one_word);
        RUN_TEST(test_lr2021_dcdc_freq_lf_write_sends_one_word);
    }
    RUN_TEST(test_lr2021_setFrequency_sends_hertz);
    RUN_TEST(test_lr2021_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_lr2021_coding_rate_long_interleaves_except_4_7);
    RUN_TEST(test_lr2021_packet_setters_send_packet_params);
    RUN_TEST(test_lr2021_other_setters_and_modes_succeed);
}
