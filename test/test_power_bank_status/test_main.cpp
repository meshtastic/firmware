// Unit tests for the Heltec Mesh Pocket V2's battery and power-bank reporting:
//  - CW2217::decode() in src/power/CW2217.cpp, the fuel gauge's registers 0x00-0x0F;
//  - hlt331MergeStatus() and hlt331Detail() in src/power/HLT331PowerStatus.cpp, which choose between
//    the gauge and the HLT331 power-bank controller;
//  - graphics::PowerBankPage::formatLines() in src/graphics/draw/PowerBankPage.cpp, the page text.
//
// The HLT331 stops sending about 30 s after nothing is plugged in and holds IO2 low; on the first
// hardware test the node then reported no battery, sent battery_level=101 (external power) and
// disabled its low-battery shutdown. The gauge keeps answering, so it is the battery source; the
// HLT331's last frame stands in for it while the HLT331 sleeps, and charging and external power come
// only from a fresh frame. The gauge's scaling (5/16 mV per step, 0.5 C per step from -40 C, integer
// percent in register 0x04) matches what Heltec's own driver applies.
//
// Regressions guarded:
//  - the battery reported absent while the HLT331 sleeps, the first hardware test's bug;
//  - charging or external power held over from a stale frame, which keeps the node in its powered
//    state after unplugging;
//  - the gauge used when it disagrees with a live HLT331 reading, or when it is not running;
//  - a wrong voltage scale, register order or temperature offset in the gauge decode;
//  - C1/C2 states or protocol names swapped on the page, or the page showing a sleeping
//    controller's stale port states as live.
#include "Arduino.h" // declares setup()/loop() with the linkage the framework's main() calls
#include "TestUtil.h"
#include "graphics/draw/PowerBankPage.h"
#include "power/CW2217.h"
#include "power/HLT331PowerStatus.h"
#include <cstdlib>
#include <stdint.h>
#include <string.h>
#include <unity.h>

// Registers of a running CW2217: 4440 mV (0x3780 * 5/16), 87 %, 25.0 C (0x82 * 5 - 400 tenths),
// profile loaded.
static void runningGauge(uint8_t regs[CW2217::REG_COUNT])
{
    memset(regs, 0, CW2217::REG_COUNT);
    regs[CW2217::REG_CHIP_ID] = 0xA0;
    regs[CW2217::REG_VCELL] = 0x37;
    regs[CW2217::REG_VCELL + 1] = 0x80;
    regs[CW2217::REG_SOC] = 87;
    regs[CW2217::REG_SOC + 1] = 0x40;
    regs[CW2217::REG_TEMP] = 0x82;
    regs[CW2217::REG_MODE] = 0x00;
    regs[CW2217::REG_SOC_ALERT] = 0x80;
}

static HLT331Status controllerFrame(uint16_t mv, uint8_t pct, bool charging, uint8_t c1, uint8_t c2)
{
    HLT331Status s;
    s.batteryMv = mv;
    s.batteryPercent = pct;
    s.charging = charging;
    s.c1State = c1;
    s.c2State = c2;
    return s;
}

void setUp(void) {}
void tearDown(void) {}

// --- CW2217::decode ---

void test_gauge_decodes_voltage_percent_and_temperature(void)
{
    uint8_t regs[CW2217::REG_COUNT];
    runningGauge(regs);
    CW2217::Reading r;
    TEST_ASSERT_TRUE(CW2217::decode(regs, r));
    TEST_ASSERT_EQUAL_UINT16(4440, r.voltageMv);
    TEST_ASSERT_EQUAL_UINT8(87, r.percent);
    TEST_ASSERT_EQUAL_INT16(250, r.tempTenthsC);
    TEST_ASSERT_TRUE(r.profileLoaded);
}

void test_gauge_temperature_below_zero(void)
{
    uint8_t regs[CW2217::REG_COUNT];
    runningGauge(regs);
    regs[CW2217::REG_TEMP] = 60; // -10.0 C
    CW2217::Reading r;
    TEST_ASSERT_TRUE(CW2217::decode(regs, r));
    TEST_ASSERT_EQUAL_INT16(-100, r.tempTenthsC);
}

void test_gauge_percent_is_capped_at_100(void)
{
    uint8_t regs[CW2217::REG_COUNT];
    runningGauge(regs);
    regs[CW2217::REG_SOC] = 104;
    CW2217::Reading r;
    TEST_ASSERT_TRUE(CW2217::decode(regs, r));
    TEST_ASSERT_EQUAL_UINT8(100, r.percent);
}

void test_gauge_rejected_unless_a_running_cw2217_with_a_plausible_voltage(void)
{
    uint8_t regs[CW2217::REG_COUNT];
    CW2217::Reading r;

    runningGauge(regs);
    regs[CW2217::REG_CHIP_ID] = 0x73; // a CW2015
    TEST_ASSERT_FALSE(CW2217::decode(regs, r));

    runningGauge(regs);
    regs[CW2217::REG_MODE] = 0xF0; // asleep
    TEST_ASSERT_FALSE(CW2217::decode(regs, r));

    runningGauge(regs);
    regs[CW2217::REG_VCELL] = 0x00; // 40 mV
    TEST_ASSERT_FALSE(CW2217::decode(regs, r));

    runningGauge(regs);
    regs[CW2217::REG_VCELL] = 0x40; // 5160 mV
    TEST_ASSERT_FALSE(CW2217::decode(regs, r));
}

// --- hlt331MergeStatus ---

void test_gauge_is_the_battery_source_when_it_agrees_with_the_controller(void)
{
    HLT331Sources in;
    in.gaugeValid = true;
    in.gaugeMv = 4100;
    in.gaugePercent = 80;
    in.haveFrame = in.fresh = true;
    in.controller = controllerFrame(4150, 85, true, HLT331Status::PORT_NOT_CONNECTED, HLT331Status::PORT_CHARGING);
    PowerBankStatus out;
    TEST_ASSERT_TRUE(hlt331MergeStatus(in, out));
    TEST_ASSERT_EQUAL_UINT16(4100, out.batteryMv);
    TEST_ASSERT_EQUAL_INT8(80, out.batteryPercent);
    TEST_ASSERT_TRUE(out.charging);
    TEST_ASSERT_TRUE(out.inputPresent);
}

void test_gauge_ignored_when_it_disagrees_with_a_live_controller(void)
{
    HLT331Sources in;
    in.gaugeValid = true;
    in.gaugeMv = 3800;
    in.gaugePercent = 40;
    in.haveFrame = in.fresh = true;
    in.controller = controllerFrame(4100, 85, false, 0, 0);
    PowerBankStatus out;
    TEST_ASSERT_TRUE(hlt331MergeStatus(in, out));
    TEST_ASSERT_EQUAL_UINT16(4100, out.batteryMv);
    TEST_ASSERT_EQUAL_INT8(85, out.batteryPercent);
}

// The first hardware test's bug: unplugged, the controller sleeps and the battery must stay.
void test_sleeping_controller_keeps_the_battery_and_clears_charging(void)
{
    HLT331Sources in;
    in.haveFrame = true;
    in.fresh = false;
    in.asleep = true;
    in.controller = controllerFrame(4426, 100, true, 0, HLT331Status::PORT_CHARGING);
    PowerBankStatus out;
    TEST_ASSERT_TRUE(hlt331MergeStatus(in, out));
    TEST_ASSERT_EQUAL_UINT16(4426, out.batteryMv);
    TEST_ASSERT_EQUAL_INT8(100, out.batteryPercent);
    TEST_ASSERT_FALSE(out.charging);
    TEST_ASSERT_FALSE(out.inputPresent);
}

void test_sleeping_controller_with_a_gauge_reports_the_gauge(void)
{
    HLT331Sources in;
    in.gaugeValid = true;
    in.gaugeMv = 4300;
    in.gaugePercent = 92;
    in.haveFrame = true;
    in.asleep = true;
    in.controller = controllerFrame(4426, 100, false, 0, 0);
    PowerBankStatus out;
    TEST_ASSERT_TRUE(hlt331MergeStatus(in, out));
    TEST_ASSERT_EQUAL_UINT16(4300, out.batteryMv);
    TEST_ASSERT_EQUAL_INT8(92, out.batteryPercent);
}

// Awake but silent is a broken link, not a quiet bank: without a gauge there is nothing to report.
void test_silent_awake_controller_without_a_gauge_reports_nothing(void)
{
    HLT331Sources in;
    in.haveFrame = true;
    in.fresh = false;
    in.asleep = false;
    in.controller = controllerFrame(4100, 80, true, 0, HLT331Status::PORT_CHARGING);
    PowerBankStatus out;
    TEST_ASSERT_FALSE(hlt331MergeStatus(in, out));
}

void test_silent_awake_controller_with_a_gauge_reports_no_charging(void)
{
    HLT331Sources in;
    in.gaugeValid = true;
    in.gaugeMv = 4100;
    in.gaugePercent = 80;
    in.haveFrame = true;
    in.controller = controllerFrame(4100, 80, true, 0, HLT331Status::PORT_CHARGING);
    PowerBankStatus out;
    TEST_ASSERT_TRUE(hlt331MergeStatus(in, out));
    TEST_ASSERT_FALSE(out.charging);
    TEST_ASSERT_FALSE(out.inputPresent);
}

void test_no_gauge_and_no_frame_reports_nothing(void)
{
    HLT331Sources in;
    in.asleep = true;
    PowerBankStatus out;
    TEST_ASSERT_FALSE(hlt331MergeStatus(in, out));
}

// --- hlt331Detail ---

void test_detail_maps_ports_protocols_and_temperatures(void)
{
    HLT331Status s = controllerFrame(4100, 80, true, HLT331Status::PORT_CHARGING, HLT331Status::PORT_DISCHARGING);
    s.fastChargeMode = 6;
    s.wirelessActive = true;
    s.wirelessProtocol = 8;
    s.wirelessMw = 17250;
    s.busMv = 9100;
    s.busMa = 1000;
    s.ntc1TempC = -10;
    s.ntc2TempC = 45;
    PowerBankDetail d;
    hlt331Detail(s, true, d);
    TEST_ASSERT_TRUE(d.live);
    TEST_ASSERT_EQUAL_UINT8(2, d.numPorts);
    TEST_ASSERT_EQUAL_STRING("C1", d.portNames[0]);
    TEST_ASSERT_EQUAL(PowerBankDetail::PORT_CHARGING_BANK, d.portStates[0]);
    TEST_ASSERT_EQUAL(PowerBankDetail::PORT_POWERING_DEVICE, d.portStates[1]);
    TEST_ASSERT_EQUAL_STRING("PD", d.fastCharge);
    TEST_ASSERT_EQUAL_STRING("Qi2", d.wirelessProtocol);
    TEST_ASSERT_EQUAL_INT8(-10, d.temperaturesC[0]);
    TEST_ASSERT_EQUAL_INT8(45, d.temperaturesC[1]);
}

void test_protocol_names_have_no_entry_for_none_or_out_of_range(void)
{
    TEST_ASSERT_NULL(hlt331FastChargeName(0));
    TEST_ASSERT_EQUAL_STRING("AFC", hlt331FastChargeName(11));
    TEST_ASSERT_EQUAL_STRING("?", hlt331FastChargeName(15));
    TEST_ASSERT_NULL(hlt331WirelessProtocolName(0));
    TEST_ASSERT_EQUAL_STRING("BPP", hlt331WirelessProtocolName(1));
    TEST_ASSERT_EQUAL_STRING("?", hlt331WirelessProtocolName(15));
}

// --- PowerBankPage::formatLines ---

void test_page_shows_live_ports_wireless_bus_and_temperatures(void)
{
    PowerBankStatus st;
    st.batteryMv = 4106;
    st.batteryPercent = 87;
    HLT331Status s = controllerFrame(4106, 87, false, 0, HLT331Status::PORT_DISCHARGING);
    s.fastChargeMode = 6;
    s.wirelessActive = true;
    s.wirelessProtocol = 8;
    s.wirelessMw = 17250;
    s.busMv = 13832;
    s.busMa = 1300;
    s.ntc1TempC = 51;
    s.ntc2TempC = 56;
    PowerBankDetail d;
    hlt331Detail(s, true, d);
    char lines[graphics::PowerBankPage::PAGE_LINES][graphics::PowerBankPage::PAGE_LINE_CHARS];
    TEST_ASSERT_EQUAL_UINT8(4, graphics::PowerBankPage::formatLines(true, st, true, d, lines));
    TEST_ASSERT_EQUAL_STRING("Battery 87% 4.10V", lines[0]);
    TEST_ASSERT_EQUAL_STRING("C1 -  C2 Out  PD", lines[1]);
    TEST_ASSERT_EQUAL_STRING("Wireless 17.2W Qi2", lines[2]);
    TEST_ASSERT_EQUAL_STRING("Bus 13.8V 1.30A  51/56C", lines[3]);
}

void test_page_flags_charging_foreign_object_and_heat(void)
{
    PowerBankStatus st;
    st.batteryMv = 4444;
    st.batteryPercent = 100;
    st.charging = true;
    HLT331Status s = controllerFrame(4444, 100, true, 0, HLT331Status::PORT_CHARGING);
    s.wirelessForeignObject = true;
    s.ntcOverTemp = true;
    PowerBankDetail d;
    hlt331Detail(s, true, d);
    char lines[graphics::PowerBankPage::PAGE_LINES][graphics::PowerBankPage::PAGE_LINE_CHARS];
    graphics::PowerBankPage::formatLines(true, st, true, d, lines);
    TEST_ASSERT_EQUAL_STRING("Battery 100% 4.44V charging", lines[0]);
    TEST_ASSERT_EQUAL_STRING("C1 -  C2 In", lines[1]);
    TEST_ASSERT_EQUAL_STRING("Wireless: object on pad!", lines[2]);
    TEST_ASSERT_NOT_NULL(strstr(lines[3], " HOT"));
}

// A sleeping controller's last frame is history: the page must not show its ports as current.
void test_page_shows_idle_rather_than_stale_ports(void)
{
    PowerBankStatus st;
    st.batteryMv = 4426;
    st.batteryPercent = 100;
    PowerBankDetail d;
    hlt331Detail(controllerFrame(4426, 100, true, 0, HLT331Status::PORT_CHARGING), false, d);
    char lines[graphics::PowerBankPage::PAGE_LINES][graphics::PowerBankPage::PAGE_LINE_CHARS];
    TEST_ASSERT_EQUAL_UINT8(2, graphics::PowerBankPage::formatLines(true, st, true, d, lines));
    TEST_ASSERT_EQUAL_STRING("Idle, nothing plugged in", lines[1]);
}

void test_page_without_status_or_detail(void)
{
    PowerBankStatus st;
    PowerBankDetail d;
    char lines[graphics::PowerBankPage::PAGE_LINES][graphics::PowerBankPage::PAGE_LINE_CHARS];
    TEST_ASSERT_EQUAL_UINT8(2, graphics::PowerBankPage::formatLines(false, st, false, d, lines));
    TEST_ASSERT_EQUAL_STRING("Battery --", lines[0]);
    TEST_ASSERT_EQUAL_STRING("Waiting for power bank", lines[1]);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_gauge_decodes_voltage_percent_and_temperature);
    RUN_TEST(test_gauge_temperature_below_zero);
    RUN_TEST(test_gauge_percent_is_capped_at_100);
    RUN_TEST(test_gauge_rejected_unless_a_running_cw2217_with_a_plausible_voltage);
    RUN_TEST(test_gauge_is_the_battery_source_when_it_agrees_with_the_controller);
    RUN_TEST(test_gauge_ignored_when_it_disagrees_with_a_live_controller);
    RUN_TEST(test_sleeping_controller_keeps_the_battery_and_clears_charging);
    RUN_TEST(test_sleeping_controller_with_a_gauge_reports_the_gauge);
    RUN_TEST(test_silent_awake_controller_without_a_gauge_reports_nothing);
    RUN_TEST(test_silent_awake_controller_with_a_gauge_reports_no_charging);
    RUN_TEST(test_no_gauge_and_no_frame_reports_nothing);
    RUN_TEST(test_detail_maps_ports_protocols_and_temperatures);
    RUN_TEST(test_protocol_names_have_no_entry_for_none_or_out_of_range);
    RUN_TEST(test_page_shows_live_ports_wireless_bus_and_temperatures);
    RUN_TEST(test_page_flags_charging_foreign_object_and_heat);
    RUN_TEST(test_page_shows_idle_rather_than_stale_ports);
    RUN_TEST(test_page_without_status_or_detail);
    exit(UNITY_END());
}

void loop() {}
