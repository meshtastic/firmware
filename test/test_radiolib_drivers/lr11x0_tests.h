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

// Grows here - each needs replies scripted per opcode:
//   begin(): getVersion() device-type check
//   updateFirmware(): enter bootloader, report RADIOLIB_LR11X0_DEVICE_BOOT, then normal. Pins every
//     image word written exactly once (an image that is an exact multiple of 64 words loses its last
//     chunk in 7.8.0) and a failed chunk being reported (its result is currently dropped)
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   scanChannel(): the CAD parameters sent

static void runLr11x0Tests()
{
    RUN_TEST(test_lr11x0_setFrequency_sends_hertz);
    RUN_TEST(test_lr11x0_lora_modulation_setters_send_modulation_params);
    RUN_TEST(test_lr11x0_coding_rate_long_interleaves_except_4_7);
    RUN_TEST(test_lr11x0_packet_setters_send_packet_params);
    RUN_TEST(test_lr11x0_other_setters_and_modes_succeed);
}
