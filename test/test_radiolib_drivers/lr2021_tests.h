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

// --- CAD: LR20x0Interface::isChannelActive() -> startChannelScan(cfg) -> startCad() ---
//
// LoRa CAD on the LR2021 is SetLoraCadParams (0x0227) + SetLoraCad (0x0228), 9 bytes:
//   [0-1] opcode  [2] nb_symbols  [3] rfu(7:5) pbl_any(4) pnr_delta(3:0)  [4] exit mode
//   [5-7] timeout, 24-bit big-endian  [8] det_peak & 0x7F            (LR20xx DS 2.2, Table 6-17)
// Exit mode for this command is CAD_ONLY 0x00, CAD_RX 0x01, CAD_LBT 0x10 (Table 6-18). RadioLib's
// RADIOLIB_LR2021_CAD_EXIT_MODE_* hold the SetCadParams (0x021B) values instead, so its _RX is 0x02.
// On hardware the chip rejects 0x02 with CMD_PERR, scanChannel() returns -706, and
// LR20x0Interface::isChannelActive() reads that as a free channel: the node transmits over live
// frames (16/16 trials on the bench). develop sends the default, 0x00, and is not affected.
//
// Never call scanChannel() here: it spins on digitalRead(irq), which RecordingHal holds at 0.

// Every field at its default sentinel, as LR20x0Interface::isChannelActive() builds it.
static ChannelScanConfig_t lr2021CadConfig(uint8_t exitMode = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                           uint8_t detPeak = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                           uint8_t symNum = RADIOLIB_LR2021_CAD_PARAM_DEFAULT, RadioLibTime_t timeout = 0)
{
    return {.cad = {.symNum = symNum,
                    .detPeak = detPeak,
                    .detMin = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                    .exitMode = exitMode,
                    .timeout = timeout,
                    .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
                    .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK}};
}

// LRxxxx::spreadingFactor starts at 0 and startCad() indexes its det_peak table by SF - 5, so a
// default-detPeak scan before any setSpreadingFactor() reads out of bounds. begin() would set it.
static void lr2021SetSf(RecordingHal &hal, LR2021 &radio, uint8_t sf)
{
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.setSpreadingFactor(sf));
    hal.transactions.clear();
}

// The SetLoraCadParams frame of a scan that must have reached the chip.
static const std::vector<uint8_t> &lr2021CadParams(const RecordingHal &hal)
{
    const auto *t = hal.last(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS));
    TEST_ASSERT_NOT_NULL_MESSAGE(t, "no SetLoraCadParams sent");
    TEST_ASSERT_EQUAL_UINT32(9, t->size());
    return *t;
}

static void test_lr2021_cad_exit_defines_are_setcadparams_values()
{
    // These are the SetCadParams (0x021B) values and correct for that command; they must not be
    // "fixed" to the LoRa ones. The bug is startCad() sending them to 0x0227.
    TEST_ASSERT_EQUAL_UINT8(0x00, RADIOLIB_LR2021_CAD_EXIT_MODE_FALLBACK);
    TEST_ASSERT_EQUAL_UINT8(0x01, RADIOLIB_LR2021_CAD_EXIT_MODE_TX);
    TEST_ASSERT_EQUAL_UINT8(0x02, RADIOLIB_LR2021_CAD_EXIT_MODE_RX);
}

// develop's config, SF9. A default exit mode must reach the chip as CAD_ONLY (0x00) on every pin:
// it is the only exit byte develop sends, and the one that works on hardware today.
static void test_lr2021_cad_default_config_frame()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
    const auto &t = lr2021CadParams(hal);
    TEST_ASSERT_EQUAL_UINT8(2, t[2]);    // nb_symbols: sentinel -> 2
    TEST_ASSERT_EQUAL_UINT8(0x00, t[3]); // pbl_any 0, pnr_delta 0
    TEST_ASSERT_EQUAL_UINT8(0x00, t[4]); // CAD_ONLY
    const uint8_t noTimeout[] = {0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(noTimeout, t.data() + 5, 3);
    TEST_ASSERT_EQUAL_UINT8(58, t[8]); // det_peak, SF9 row
}

// The exit byte is passed through unmasked and unmapped, whatever the caller asks for. 0x02 is the
// byte the LBT branches send today via RADIOLIB_LR2021_CAD_EXIT_MODE_RX, and the one the chip rejects.
static void test_lr2021_cad_exit_byte_sent_verbatim()
{
    const uint8_t modes[] = {0x00, 0x01, 0x10, RADIOLIB_LR2021_CAD_EXIT_MODE_RX};
    for (uint8_t mode : modes) {
        LR2021_RADIO(hal);
        lr2021SetSf(hal, radio, 9);
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig(mode)));
        TEST_ASSERT_EQUAL_UINT8(mode, lr2021CadParams(hal)[4]);
    }
}

// RadioLib's 2-symbol det_peak row (DS Table 6-19), used when the caller passes the sentinel.
static void test_lr2021_cad_default_det_peak_per_sf()
{
    const uint8_t expected[] = {56, 56, 56, 58, 58, 60, 64, 68};
    for (uint8_t sf = 5; sf <= 12; sf++) {
        LR2021_RADIO(hal);
        lr2021SetSf(hal, radio, sf);
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected[sf - 5], lr2021CadParams(hal)[8], "det_peak for this SF");
    }
}

// Pins today: the default det_peak is the 2-symbol value whatever nb_symbols is. Table 6-19 gives
// 51 for 4 symbols at SF7, so a caller scanning more than 2 symbols must pass its own det_peak.
static void test_lr2021_cad_default_det_peak_ignores_symNum()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 7);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig(RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                                                                      RADIOLIB_LR2021_CAD_PARAM_DEFAULT, 4)));
    const auto &t = lr2021CadParams(hal);
    TEST_ASSERT_EQUAL_UINT8(4, t[2]);
    TEST_ASSERT_EQUAL_UINT8(56, t[8]);
}

// det_peak is 7 bits; 0xFF is the sentinel, so it can never be sent as itself.
static void test_lr2021_cad_explicit_det_peak_sent_masked()
{
    const struct {
        uint8_t in, sent;
    } cases[] = {{60, 60}, {0x85, 0x05}, {RADIOLIB_LR2021_CAD_PARAM_DEFAULT, 56}};
    for (const auto &c : cases) {
        LR2021_RADIO(hal);
        lr2021SetSf(hal, radio, 7);
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE,
                                radio.startChannelScan(lr2021CadConfig(RADIOLIB_LR2021_CAD_PARAM_DEFAULT, c.in)));
        TEST_ASSERT_EQUAL_UINT8(c.sent, lr2021CadParams(hal)[8]);
    }
}

// fastCad sets pnr_delta 8; pbl_any (bit 4, "preamble only") stays off either way, which RadioLib
// does on purpose: the datasheet calls it unreliable.
static void test_lr2021_fast_cad_sets_pnr_delta_only()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    radio.fastCad = true;
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
    TEST_ASSERT_EQUAL_UINT8(RADIOLIB_LR2021_LORA_CAD_PNR_DELTA_FAST, lr2021CadParams(hal)[3]);
}

// Pins RadioLib's arithmetic: timeout_us / 30.52 (32.768 kHz ticks). The timeout only matters under
// CAD_RX / CAD_LBT. DS 2.2 6.3.11 states the unit as "periods of 32 MHz crystal oscillator", which
// this does not obviously match; if this test breaks, check the new unit on hardware before
// updating it.
static void test_lr2021_cad_timeout_in_30_52_us_steps()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE,
                            radio.startChannelScan(lr2021CadConfig(0x01, RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                                                   RADIOLIB_LR2021_CAD_PARAM_DEFAULT, 100000)));
    const uint8_t ticks[] = {0x00, 0x0C, 0xCC}; // 3276
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ticks, lr2021CadParams(hal).data() + 5, 3);
}

// The LoRa CAD pair, never the RSSI-based SetCadParams/SetCad (0x021B/0x021C), in this order.
// The packet-type reads and status checks are opcode-less (NOP) transactions and are skipped.
static void test_lr2021_cad_command_sequence()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
    std::vector<uint16_t> ops;
    for (const auto &t : hal.transactions)
        if (t.size() >= 2 && (t[0] | t[1]))
            ops.push_back(static_cast<uint16_t>(t[0] << 8 | t[1]));
    const uint16_t expected[] = {RADIOLIB_LR2021_CMD_GET_PACKET_TYPE,    RADIOLIB_LR2021_CMD_SET_STANDBY,
                                 RADIOLIB_LR2021_CMD_SET_DIO_IRQ_CONFIG, RADIOLIB_LR2021_CMD_CLEAR_IRQ,
                                 RADIOLIB_LR2021_CMD_GET_PACKET_TYPE,    RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS,
                                 RADIOLIB_LR2021_CMD_SET_LORA_CAD};
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]), ops.size());
    TEST_ASSERT_EQUAL_HEX16_ARRAY(expected, ops.data(), ops.size());
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_CAD_PARAMS)));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_CAD)));
}

// CAD_DONE and CAD_DETECTED routed to DIO5 (irqDioNum), the line the interface's ISR watches.
static void test_lr2021_cad_irq_routed_to_dio()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
    const auto *t = hal.first(op16(RADIOLIB_LR2021_CMD_SET_DIO_IRQ_CONFIG));
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT32(7, t->size()); // opcode(2) + dio + irq(4)
    TEST_ASSERT_EQUAL_UINT8(5, (*t)[2]);
    const uint32_t irq = (uint32_t)(*t)[3] << 24 | (uint32_t)(*t)[4] << 16 | (uint32_t)(*t)[5] << 8 | (*t)[6];
    TEST_ASSERT_EQUAL_HEX32(RADIOLIB_LR2021_IRQ_CAD_DONE | RADIOLIB_LR2021_IRQ_CAD_DETECTED, irq);
}

static void test_lr2021_cad_wrong_modem_sends_no_cad()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    hal.replies.clear();
    hal.reply(op16(RADIOLIB_LR2021_CMD_GET_PACKET_TYPE), 0x04, {0x04, RADIOLIB_LR2021_PACKET_TYPE_GFSK}, 1);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_WRONG_MODEM, radio.startChannelScan(lr2021CadConfig()));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS)));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD)));
}

// Answers getChannelScanResult()'s IRQ read with `irq`. That read is an opcode-less 6-byte transfer
// (status(2), irq(4) big-endian), the second after a packet-type request. The packet type is also
// asked before standby and before SetLoraCadParams; those writes get the same bytes, which as a
// status byte (0x04) read as CMD_OK and leave them unaffected.
static void lr2021AnswerIrq(RecordingHal &hal, uint32_t irq)
{
    hal.reply(op16(RADIOLIB_LR2021_CMD_GET_PACKET_TYPE), 0x04,
              {0x04, 0x04, (uint8_t)(irq >> 24), (uint8_t)(irq >> 16), (uint8_t)(irq >> 8), (uint8_t)irq}, 2);
}

// The LR2021 as measured on the bench: SetLoraCadParams takes exit byte 0x00, 0x01 or 0x10 and
// refuses anything else with CMD_PERR (stat1 0x02), which RadioLib reports as -706.
static bool lr2021AcceptsLoraCadExit(uint8_t exitMode)
{
    return exitMode == 0x00 || exitMode == 0x01 || exitMode == 0x10;
}

static void lr2021ModelCadExitCheck(RecordingHal &hal)
{
    hal.rejectUnless(
        op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS),
        [](const std::vector<uint8_t> &t) { return t.size() > 4 && lr2021AcceptsLoraCadExit(t[4]); }, 0x02);
}

// A started scan, then getChannelScanResult() with `irq` as the chip's IRQ word.
static int16_t lr2021CadResultFor(uint32_t irq)
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    lr2021AnswerIrq(hal, irq);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig()));
    return radio.getChannelScanResult();
}

// Only CAD_DONE without CAD_DETECTED is a free channel; no CAD_DONE at all is an error, not "free".
static void test_lr2021_cad_result_decoding()
{
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_LORA_DETECTED, lr2021CadResultFor(RADIOLIB_LR2021_IRQ_CAD_DETECTED));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_LORA_DETECTED,
                            lr2021CadResultFor(RADIOLIB_LR2021_IRQ_CAD_DETECTED | RADIOLIB_LR2021_IRQ_CAD_DONE));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_CHANNEL_FREE, lr2021CadResultFor(RADIOLIB_LR2021_IRQ_CAD_DONE));
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_UNKNOWN, lr2021CadResultFor(0));
}

// The bench failure at the SPI level: against the modelled chip, RadioLib's own RX exit define is
// refused and the scan returns -706. That is neither LORA_DETECTED nor CHANNEL_FREE, so the
// interface has an error to refuse to transmit on. If this starts passing a scan through, RadioLib
// has changed CAD_EXIT_MODE_RX or what startCad() sends; check against DS 2.2 Table 6-18.
static void test_lr2021_cad_rx_exit_define_rejected_returns_spi_cmd_invalid()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    lr2021ModelCadExitCheck(hal);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_SPI_CMD_INVALID,
                            radio.startChannelScan(lr2021CadConfig(RADIOLIB_LR2021_CAD_EXIT_MODE_RX)));
}

// A CAD must not start with parameters the chip refused.
static void test_lr2021_cad_not_started_after_rejected_params()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    lr2021ModelCadExitCheck(hal);
    (void)radio.startChannelScan(lr2021CadConfig(RADIOLIB_LR2021_CAD_EXIT_MODE_RX));
    TEST_ASSERT_EQUAL_UINT32(1, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS)));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD)));
}

// The exit bytes the chip accepts start a CAD; guards the model itself, so the rejection tests
// above cannot pass because the model refuses everything.
static void test_lr2021_cad_accepted_exit_bytes_start_a_cad()
{
    const uint8_t modes[] = {0x00, 0x01, 0x10};
    for (uint8_t mode : modes) {
        LR2021_RADIO(hal);
        lr2021SetSf(hal, radio, 9);
        lr2021ModelCadExitCheck(hal);
        TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_NONE, radio.startChannelScan(lr2021CadConfig(mode)));
        TEST_ASSERT_EQUAL_UINT32(1, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD)));
    }
}

// The same path with the other failure status: (stat1 & 0x0E) == 0 is CMD_FAIL, -707.
static void test_lr2021_cad_params_failed_returns_spi_cmd_failed()
{
    LR2021_RADIO(hal);
    lr2021SetSf(hal, radio, 9);
    hal.reply(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS), 0x04, {0x01}, 1);
    TEST_ASSERT_EQUAL_INT16(RADIOLIB_ERR_SPI_CMD_FAILED, radio.startChannelScan(lr2021CadConfig()));
    TEST_ASSERT_EQUAL_UINT32(0, hal.count(op16(RADIOLIB_LR2021_CMD_SET_LORA_CAD)));
}

// Grows here - each needs replies scripted per opcode:
//   begin(): getVersion() check and the calibration sequence
//   resetDCDCworkaround() via setPacketType(): only begin() reaches it; same one-word write as above
//   setPaTable(): custom LF table entry chosen per output power (cf. meshtastic/firmware#11980)
//   readData() / getPacketLength(): a chip-reported length longer than the caller's buffer
//   getRSSI() / getSNR() / getPacketStatus(): decoding of known reply bytes
//   LR20x0Interface::isChannelActive() is driven in lr20x0_interface_tests.h

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
    RUN_TEST(test_lr2021_cad_exit_defines_are_setcadparams_values);
    RUN_TEST(test_lr2021_cad_default_config_frame);
    RUN_TEST(test_lr2021_cad_exit_byte_sent_verbatim);
    RUN_TEST(test_lr2021_cad_default_det_peak_per_sf);
    RUN_TEST(test_lr2021_cad_default_det_peak_ignores_symNum);
    RUN_TEST(test_lr2021_cad_explicit_det_peak_sent_masked);
    RUN_TEST(test_lr2021_fast_cad_sets_pnr_delta_only);
    RUN_TEST(test_lr2021_cad_timeout_in_30_52_us_steps);
    RUN_TEST(test_lr2021_cad_command_sequence);
    RUN_TEST(test_lr2021_cad_irq_routed_to_dio);
    RUN_TEST(test_lr2021_cad_wrong_modem_sends_no_cad);
    RUN_TEST(test_lr2021_cad_result_decoding);
    RUN_TEST(test_lr2021_cad_rx_exit_define_rejected_returns_spi_cmd_invalid);
    RUN_TEST(test_lr2021_cad_not_started_after_rejected_params);
    RUN_TEST(test_lr2021_cad_accepted_exit_bytes_start_a_cad);
    RUN_TEST(test_lr2021_cad_params_failed_returns_spi_cmd_failed);
}
