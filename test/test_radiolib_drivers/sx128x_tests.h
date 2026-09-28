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
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5));
    TEST_ASSERT_EQUAL_UINT32(3, hal.count({RADIOLIB_SX128X_CMD_SET_MODULATION_PARAMS}));
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

// Grows here - each needs replies scripted per opcode:
//   begin(): version string check
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   scanChannel(): CAD parameters and the CAD-done IRQ

static void runSx128xTests()
{
    RUN_TEST(test_sx128x_setFrequency_sends_rf_frequency);
    RUN_TEST(test_sx128x_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_sx128x_packet_setters_send_packet_params);
    RUN_TEST(test_sx128x_other_setters_and_modes_succeed);
}
