#pragma once

// LR11x0 (LR1121): the calls LR11x0Interface makes, minus begin() and updateFirmware().

#include "RecordingHal.h"
#include "TestUtil.h"

// LRxxxx reads answer in a second transaction: status (0x04 = CMD_OK), then data. The LoRa setters
// ask for the packet type first; 0x02 is LoRa, which as a status byte would decode as CMD_PERR,
// so it goes in the data byte only.
#define LR11X0_RADIO(hal)                                                                                                        \
    RecordingHal &hal = freshHal();                                                                                              \
    hal.reply(op16(RADIOLIB_LR11X0_CMD_GET_PACKET_TYPE), 0x04, {0x04, RADIOLIB_LR11X0_PACKET_TYPE_LORA}, true);                  \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, 2);                                                                            \
    LR1121 radio(&mod)

static void test_lr11x0_setFrequency_sends_hertz()
{
    LR11X0_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setFrequency(915.0));
    const auto *t = hal.first(op16(RADIOLIB_LR11X0_CMD_SET_RF_FREQUENCY));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(6, t->size());
    const uint8_t hz[] = {0x36, 0x89, 0xCA, 0xC0}; // 915000000
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hz, t->data() + 2, 4);
}

static void test_lr11x0_lora_modulation_setters_send_modulation_params()
{
    LR11X0_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(9));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setBandwidth(250.0));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    TEST_ASSERT_EQUAL_UINT32(3, hal.count(op16(RADIOLIB_LR11X0_CMD_SET_MODULATION_PARAMS)));
}

// LR11x0Interface passes cr != 7 as the long-interleave flag: 4/7 has no long-interleaver code.
static void test_lr11x0_coding_rate_long_interleaves_except_4_7()
{
    LR11X0_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    const auto *t = hal.last(op16(RADIOLIB_LR11X0_CMD_SET_MODULATION_PARAMS));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(6, t->size()); // opcode(2) + sf, bw, cr, ldro
    TEST_ASSERT_EQUAL_UINT8(5, (*t)[4]);    // 4/5, long interleaver
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(7, false));
    t = hal.last(op16(RADIOLIB_LR11X0_CMD_SET_MODULATION_PARAMS));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT8(3, (*t)[4]); // 4/7, short interleaver
}

static void test_lr11x0_packet_setters_send_packet_params()
{
    LR11X0_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setPreambleLength(16));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCRC(2));
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR11X0_CMD_SET_PACKET_PARAMS)) >= 2);
}

static void test_lr11x0_other_setters_and_modes_succeed()
{
    LR11X0_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSyncWord(0x2B));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setOutputPower(22));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.standby());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startReceive());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.sleep());
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR11X0_CMD_SET_LORA_SYNC_WORD)) >= 1);
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR11X0_CMD_SET_TX_PARAMS)) >= 1);
    TEST_ASSERT_TRUE(hal.count(op16(RADIOLIB_LR11X0_CMD_SET_RX)) >= 1);
}

// --- CAD: LR11x0Interface::isChannelActive() -> startChannelScan(cfg) -> startCad() ---
//
// SetCadParams (0x020D), 9 bytes: [0-1] opcode [2] symNum [3] detPeak [4] detMin [5] exit mode
// [6-8] timeout, 24-bit big-endian. Unlike the LR2021 (lr2021_tests.h), RadioLib's LR11x0 exit
// defines are already the chip's values (STBY_RC 0x00, RX 0x01, LBT 0x10), so these pin them against
// a regression, e.g. an LR2021 "fix" copied across the LRxxxx family the wrong way round.

static ChannelScanConfig_t lr11x0CadConfig(uint8_t exitMode = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT)
{
    return {.cad = {.symNum = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                    .detPeak = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                    .detMin = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                    .exitMode = exitMode,
                    .timeout = 0,
                    .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
                    .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK}};
}

// As on the LR2021, startCad() indexes det_peak by SF - 5 and the SF starts at 0 without begin().
static void lr11x0SetSf(RecordingHal &hal, LR1121 &radio, uint8_t sf)
{
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(sf));
    hal.transactions.clear();
}

static const std::vector<uint8_t> &lr11x0CadParams(const RecordingHal &hal)
{
    const auto *t = hal.last(op16(RADIOLIB_LR11X0_CMD_SET_CAD_PARAMS));
    TEST_ASSERT_NOT_NULL_MESSAGE(t, "no SetCadParams sent");
    TEST_ASSERT_EQUAL_UINT32(9, t->size());
    return *t;
}

static void test_lr11x0_cad_exit_defines_match_datasheet()
{
    TEST_ASSERT_EQUAL_UINT8(0x00, RADIOLIB_LR11X0_CAD_EXIT_MODE_STBY_RC);
    TEST_ASSERT_EQUAL_UINT8(0x01, RADIOLIB_LR11X0_CAD_EXIT_MODE_RX);
    TEST_ASSERT_EQUAL_UINT8(0x10, RADIOLIB_LR11X0_CAD_EXIT_MODE_LBT);
}

// develop's config at SF9: the default exit is standby (0x00), the byte LR11x0Interface sends today.
static void test_lr11x0_cad_default_config_frame()
{
    LR11X0_RADIO(hal);
    lr11x0SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr11x0CadConfig()));
    const auto &t = lr11x0CadParams(hal);
    TEST_ASSERT_EQUAL_UINT8(2, t[2]);    // symNum: sentinel -> 2
    TEST_ASSERT_EQUAL_UINT8(55, t[3]);   // detPeak, SF9 entry of RadioLib's table
    TEST_ASSERT_EQUAL_UINT8(10, t[4]);   // detMin
    TEST_ASSERT_EQUAL_UINT8(0x00, t[5]); // STBY_RC
    const uint8_t noTimeout[] = {0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(noTimeout, t.data() + 6, 3);
    TEST_ASSERT_EQUAL_UINT32(1, hal.count(op16(RADIOLIB_LR11X0_CMD_SET_CAD)));
}

// The RX exit define reaches the chip as 0x01, CAD-to-RX, the byte a CAD-to-RX handoff relies on.
static void test_lr11x0_cad_rx_exit_byte_sent()
{
    LR11X0_RADIO(hal);
    lr11x0SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr11x0CadConfig(RADIOLIB_LR11X0_CAD_EXIT_MODE_RX)));
    TEST_ASSERT_EQUAL_UINT8(0x01, lr11x0CadParams(hal)[5]);
}

// A refused SetCadParams (CMD_PERR) returns -706 and starts no CAD, the same contract as the LR2021:
// LR11x0Interface::isChannelActive() also reads any error but WRONG_MODEM as a free channel.
static void test_lr11x0_cad_params_rejected_returns_spi_cmd_invalid()
{
    LR11X0_RADIO(hal);
    lr11x0SetSf(hal, radio, 9);
    hal.reply(op16(RADIOLIB_LR11X0_CMD_SET_CAD_PARAMS), 0x04, {0x02}, 1);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_SPI_CMD_INVALID, radio.startChannelScan(lr11x0CadConfig()));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR11X0_CMD_SET_CAD)));
}

// Grows here - each needs replies scripted per opcode:
//   begin(): getVersion() device-type check
//   updateFirmware(): enter bootloader, report RADIOLIB_LR11X0_DEVICE_BOOT, then normal. Pins every
//     image word written exactly once (an image that is an exact multiple of 64 words loses its last
//     chunk in 7.8.0) and a failed chunk being reported (its result is currently dropped)
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes

static void runLr11x0Tests()
{
    RUN_TEST(test_lr11x0_setFrequency_sends_hertz);
    RUN_TEST(test_lr11x0_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_lr11x0_coding_rate_long_interleaves_except_4_7);
    RUN_TEST(test_lr11x0_packet_setters_send_packet_params);
    RUN_TEST(test_lr11x0_other_setters_and_modes_succeed);
    RUN_TEST(test_lr11x0_cad_exit_defines_match_datasheet);
    RUN_TEST(test_lr11x0_cad_default_config_frame);
    RUN_TEST(test_lr11x0_cad_rx_exit_byte_sent);
    RUN_TEST(test_lr11x0_cad_params_rejected_returns_spi_cmd_invalid);
}
