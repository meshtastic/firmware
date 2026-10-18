#pragma once

// SX127x (SX1276): the calls RF95Interface makes, minus begin(). Register-mapped, so the HAL runs in
// echo mode and the assertions read the resulting register values.

#include "RecordingHal.h"
#include "TestUtil.h"

// The setters check REG_OP_MODE bit 7 for LoRa mode; begin() would have set it.
#define SX127X_RADIO(hal)                                                                                                        \
    RecordingHal &hal = freshHal();                                                                                              \
    hal.registerEcho = true;                                                                                                     \
    hal.registers[RADIOLIB_SX127X_REG_OP_MODE] = RADIOLIB_SX127X_LORA | RADIOLIB_SX127X_STANDBY;                                 \
    Module mod(&hal, 1, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC);                                                                  \
    SX1276 radio(&mod)

static void test_sx127x_setFrequency_writes_the_frf_registers()
{
    SX127X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setFrequency(915.0));
    // frf = 915 MHz * 2^19 / 32 MHz = 0xE4C000
    TEST_ASSERT_EQUAL_HEX8(0xE4, hal.registers[RADIOLIB_SX127X_REG_FRF_MSB]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, hal.registers[RADIOLIB_SX127X_REG_FRF_MID]);
    TEST_ASSERT_EQUAL_HEX8(0x00, hal.registers[RADIOLIB_SX127X_REG_FRF_LSB]);
}

static void test_sx127x_setSpreadingFactor_writes_modem_config_2()
{
    SX127X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(9));
    TEST_ASSERT_EQUAL_HEX8(9, hal.registers[RADIOLIB_SX127X_REG_MODEM_CONFIG_2] >> 4);
}

static void test_sx127x_setSyncWord_writes_the_sync_register()
{
    SX127X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSyncWord(0x2B));
    TEST_ASSERT_EQUAL_HEX8(0x2B, hal.registers[RADIOLIB_SX127X_REG_SYNC_WORD]);
}

static void test_sx127x_setPreambleLength_writes_both_bytes()
{
    SX127X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setPreambleLength(16));
    TEST_ASSERT_EQUAL_HEX8(0x00, hal.registers[RADIOLIB_SX127X_REG_PREAMBLE_MSB]);
    TEST_ASSERT_EQUAL_HEX8(16, hal.registers[RADIOLIB_SX127X_REG_PREAMBLE_LSB]);
}

static void test_sx127x_other_setters_and_modes_succeed()
{
    SX127X_RADIO(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setBandwidth(250.0));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setCodingRate(5));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setOutputPower(17));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.standby());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startReceive());
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.sleep());
}

// Grows here - each needs more than an echoing register file:
//   begin(): REG_VERSION check and the reset sequence
//   readData() / getPacketLength(): REG_RX_NB_BYTES larger than the caller's buffer
//   getRSSI() / getSNR(): decoding of REG_PKT_RSSI_VALUE / REG_PKT_SNR_VALUE
//   scanChannel(): CAD done/detected flags in REG_IRQ_FLAGS

static void runSx127xTests()
{
    RUN_TEST(test_sx127x_setFrequency_writes_the_frf_registers);
    RUN_TEST(test_sx127x_setSpreadingFactor_writes_modem_config_2);
    RUN_TEST(test_sx127x_setSyncWord_writes_the_sync_register);
    RUN_TEST(test_sx127x_setPreambleLength_writes_both_bytes);
    RUN_TEST(test_sx127x_other_setters_and_modes_succeed);
}
