#pragma once

// SX126x (SX1262): the calls SX126xInterface makes, minus begin().

#include "RecordingHal.h"
#include "TestUtil.h"

// The LoRa setters ask the chip for its packet type first. 0x01 is LoRa, and as a status byte it
// matches no SX126x error code, so the whole reply can be 0x01.
static void sx126xAnswerLora(RecordingHal &hal)
{
    hal.reply({RADIOLIB_SX126X_CMD_GET_PACKET_TYPE}, RADIOLIB_SX126X_PACKET_TYPE_LORA);
}

// Exposes RadioLib's own status parser, which is protected in SX126x.
struct TestSX1262 : public SX1262 {
    using SX1262::SX1262;
    static int16_t parseStatus(uint8_t in) { return SPIparseStatus(in); }
};

// SX126x sets its SPI framing in begin() (modSetup()), not its constructor, and begin() cannot run
// without a chip. This is the same framing, as of RadioLib 7.8.0.
static void sx126xFraming(Module &mod)
{
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    mod.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] = Module::BITS_8;
    mod.spiConfig.statusPos = 1;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX126X_CMD_READ_REGISTER;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX126X_CMD_WRITE_REGISTER;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX126X_CMD_NOP;
    mod.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX126X_CMD_GET_STATUS;
    mod.spiConfig.stream = true;
    mod.spiConfig.parseStatusCb = TestSX1262::parseStatus;
}

#define SX126X_RADIO(hal)                                                                                                        \
    RecordingHal &hal = freshHal();                                                                                              \
    sx126xAnswerLora(hal);                                                                                                       \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, 2);                                                                            \
    TestSX1262 radio(&mod);                                                                                                      \
    sx126xFraming(mod)

static void test_sx126x_setFrequency_sends_the_frf_word()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setFrequency(915.0));
    // frf = 915 MHz * 2^25 / 32 MHz = 0x39300000
    const auto *t = hal.first({RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY});
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(5, t->size());
    const uint8_t frf[] = {0x39, 0x30, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frf, t->data() + 1, 4);
}

static void test_sx126x_lora_modulation_setters_send_modulation_params()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(9));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setBandwidth(250.0));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5));
    TEST_ASSERT_EQUAL_UINT32(3, hal.count({RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS}));
}

static void test_sx126x_packet_setters_send_packet_params()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setPreambleLength(16));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCRC(2));
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS}) >= 2);
}

static void test_sx126x_setSyncWord_writes_a_register()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSyncWord(0x2B));
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_WRITE_REGISTER}) >= 1);
}

static void test_sx126x_setOutputPower_sends_pa_config_and_tx_params()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setOutputPower(22));
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_PA_CONFIG}) >= 1);
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_TX_PARAMS}) >= 1);
}

static void test_sx126x_mode_commands_reach_the_chip()
{
    SX126X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.standby());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startReceive());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.sleep());
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_STANDBY}) >= 1);
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_RX}) >= 1);
    TEST_ASSERT_TRUE(hal.count({RADIOLIB_SX126X_CMD_SET_SLEEP}) >= 1);
}

// Grows here - each needs replies scripted per opcode, not just a status byte:
//   begin(): version string check (SX126X_REG_VERSION_STRING)
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   scanChannel(): the CAD parameters sent, per the part's symNum encoding
//   startReceiveDutyCycleAuto(): the RX and sleep periods derived from preamble length

static void runSx126xTests()
{
    RUN_TEST(test_sx126x_setFrequency_sends_the_frf_word);
    RUN_TEST(test_sx126x_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_sx126x_packet_setters_send_packet_params);
    RUN_TEST(test_sx126x_setSyncWord_writes_a_register);
    RUN_TEST(test_sx126x_setOutputPower_sends_pa_config_and_tx_params);
    RUN_TEST(test_sx126x_mode_commands_reach_the_chip);
}
