#pragma once

// SX128x (SX1280): the calls SX128xInterface makes, minus begin().

#include "RecordingHal.h"
#include "TestUtil.h"

// SX128x sets its SPI framing in begin() (modSetup()), not its constructor, and begin() cannot run
// without a chip. This is the same framing, as of RadioLib 7.8.0, minus the status parser: it is
// private in SX128x, and the HAL never answers with an error status anyway.
static void sx128xFraming(Module &mod)
{
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] = Module::BITS_8;
    mod.spiConfig.statusPos = 0;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX128X_CMD_READ_REGISTER;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX128X_CMD_WRITE_REGISTER;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX128X_CMD_NOP;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX128X_CMD_GET_STATUS;
    mod.spiConfig.stream = true;
    mod.spiConfig.parseStatusCb = nullptr;
}

// The LoRa setters ask for the packet type first. 0x01 is LoRa and matches no SX128x error code.
#define SX128X_RADIO(hal)                                                                                                        \
    RecordingHal &hal = freshHal();                                                                                              \
    hal.reply({RADIOLIB_SX128X_CMD_GET_PACKET_TYPE}, RADIOLIB_SX128X_PACKET_TYPE_LORA);                                          \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, 2);                                                                            \
    SX1280 radio(&mod);                                                                                                          \
    sx128xFraming(mod)

static void test_sx128x_setFrequency_sends_rf_frequency()
{
    SX128X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setFrequency(2440.0));
    const auto *t = hal.first({RADIOLIB_SX128X_CMD_SET_RF_FREQUENCY});
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(4, t->size()); // opcode + 24-bit frf
}

static void test_sx128x_lora_modulation_setters_send_modulation_params()
{
    SX128X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(9));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setBandwidth(812.5));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    TEST_ASSERT_EQUAL_UINT32(3, hal.count({RADIOLIB_SX128X_CMD_SET_MODULATION_PARAMS}));
}

// SX128xInterface passes cr != 7 as the long-interleave flag: 4/7 has no long-interleaver code.
static void test_sx128x_coding_rate_long_interleaves_except_4_7()
{
    SX128X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5, true));
    const auto *t = hal.last({RADIOLIB_SX128X_CMD_SET_MODULATION_PARAMS});
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(4, t->size()); // opcode + sf, bw, cr
    TEST_ASSERT_EQUAL_UINT8(5, (*t)[3]);    // 4/5, long interleaver
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(7, false));
    t = hal.last({RADIOLIB_SX128X_CMD_SET_MODULATION_PARAMS});
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT8(3, (*t)[3]); // 4/7, short interleaver
}

static void test_sx128x_packet_setters_send_packet_params()
{
    SX128X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setPreambleLength(16));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCRC(2));
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX128X_CMD_SET_PACKET_PARAMS}) >= 2);
}

static void test_sx128x_other_setters_and_modes_succeed()
{
    SX128X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSyncWord(0x12));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setOutputPower(10));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.standby());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startReceive());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.sleep());
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX128X_CMD_SET_TX_PARAMS}) >= 1);
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX128X_CMD_SET_RX}) >= 1);
}

// From 7.8.0 (jgromes/RadioLib#1872) the SX128x status is the first byte of every exchange, not the
// second. These run begin(), the call SX128xInterface::init() makes, so they see RadioLib's own
// framing rather than sx128xFraming()'s copy. begin() first reads the version string: opcode,
// address(2) and a status byte, then the string.
#define SX128X_BEGIN_RADIO(hal)                                                                                                  \
    RecordingHal &hal = freshHal();                                                                                              \
    hal.reply({RADIOLIB_SX128X_CMD_GET_PACKET_TYPE}, RADIOLIB_SX128X_PACKET_TYPE_LORA);                                          \
    hal.reply(                                                                                                                   \
        {RADIOLIB_SX128X_CMD_READ_REGISTER, RADIOLIB_SX128X_REG_VERSION_STRING >> 8, RADIOLIB_SX128X_REG_VERSION_STRING & 0xFF}, \
        0x00, {0x04, 0x04, 0x04, 0x04, 'S', 'X', '1', '2', '8', '0'});                                                           \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, 2);                                                                            \
    SX1280 radio(&mod)

static int16_t sx128xBegin(SX1280 &radio)
{
    return radio.begin(2440.0, 812.5, 9, 7, 0x12, 10, 16);
}

// The framing begin() leaves is the one the setter tests above assume.
static void test_sx128x_begin_reads_status_from_the_first_byte()
{
    SX128X_BEGIN_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, sx128xBegin(radio));
    TEST_ASSERT_EQUAL_UINT8(0, mod.spiConfig.statusPos);
    TEST_ASSERT_EQUAL(Module::BITS_8, mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS]);
    TEST_ASSERT_NOT_NULL(mod.spiConfig.parseStatusCb);
}

// A command the chip refuses, reported in the first byte, fails begin() and so the interface's init().
static void test_sx128x_status_error_in_first_byte_fails_begin()
{
    SX128X_BEGIN_RADIO(hal);
    hal.reply({RADIOLIB_SX128X_CMD_SET_REGULATOR_MODE}, 0x04, {RADIOLIB_SX128X_STATUS_CMD_ERROR});
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_SPI_CMD_INVALID, sx128xBegin(radio));
}

// Grows here - each needs replies scripted per opcode:
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   scanChannel(): CAD parameters and the CAD-done IRQ

static void runSx128xTests()
{
    RUN_TEST(test_sx128x_setFrequency_sends_rf_frequency);
    RUN_TEST(test_sx128x_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_sx128x_coding_rate_long_interleaves_except_4_7);
    RUN_TEST(test_sx128x_packet_setters_send_packet_params);
    RUN_TEST(test_sx128x_other_setters_and_modes_succeed);
    if constexpr (RADIOLIB_AT_LEAST(7, 8, 0)) {
        RUN_TEST(test_sx128x_begin_reads_status_from_the_first_byte);
        RUN_TEST(test_sx128x_status_error_in_first_byte_fails_begin);
    }
}
