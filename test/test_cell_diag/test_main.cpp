// Unit tests for the cellular diagnostic response parsers in src/mesh/cell/cellDiagParse.cpp.
// Response text is taken from an Air780E running V1179.
#include "Arduino.h"
#include "TestUtil.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unity.h>

// Forward-declared rather than included so the test does not pull in
// configuration.h and the HAS_CELLULAR guard. Defined in cellDiagParse.cpp.
bool parseCesq(const char *resp, int16_t *rsrpDbm, float *rsrqDb);
int parseCeregStat(const char *resp);
bool parseBandInd(const char *resp, uint8_t *band, uint8_t *act);
bool parseCbcMillivolts(const char *resp, uint16_t *mv);
bool parseCopsName(const char *resp, char *out, size_t outLen);
bool parseCopsNumeric(const char *resp, char *out, size_t outLen);
bool parseSysConfig(const char *resp, uint8_t *mode, uint8_t *acqorder, uint8_t *srvdomain);

void setUp(void) {}
void tearDown(void) {}

// --- AT+CESQ ---

void test_cesq_typical()
{
    // rxlev,ber,rscp,ecno,rsrq,rsrp - only the last two carry LTE readings.
    int16_t rsrp = 0;
    float rsrq = 0;
    TEST_ASSERT_TRUE(parseCesq("+CESQ: 99,99,255,255,20,45", &rsrp, &rsrq));
    TEST_ASSERT_EQUAL_INT16(-95, rsrp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, rsrq);
}

void test_cesq_not_available()
{
    // 255 in either LTE field means no reading, not a very good one.
    int16_t rsrp = 0;
    float rsrq = 0;
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,255,255", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,255,45", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,20,255", &rsrp, &rsrq));
}

void test_cesq_boundaries()
{
    int16_t rsrp = 0;
    float rsrq = 0;
    TEST_ASSERT_TRUE(parseCesq("+CESQ: 99,99,255,255,0,0", &rsrp, &rsrq));
    TEST_ASSERT_EQUAL_INT16(-140, rsrp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, rsrq);

    TEST_ASSERT_TRUE(parseCesq("+CESQ: 99,99,255,255,34,97", &rsrp, &rsrq));
    TEST_ASSERT_EQUAL_INT16(-43, rsrp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, rsrq);
}

void test_cesq_out_of_range()
{
    // <rsrq> is valid 0-34 and <rsrp> is valid 0-97; anything above that is not
    // the 255 "not known" sentinel either, and must not be reported as a reading.
    int16_t rsrp = 0;
    float rsrq = 0;
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,35,45", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,20,98", &rsrp, &rsrq));
}

void test_cesq_multiline_response()
{
    // The parser is handed a whole response, which may carry other lines.
    int16_t rsrp = 0;
    TEST_ASSERT_TRUE(parseCesq("+CIEV: 1\n+CESQ: 99,99,255,255,20,45", &rsrp, nullptr));
    TEST_ASSERT_EQUAL_INT16(-95, rsrp);
}

void test_cesq_malformed()
{
    int16_t rsrp = 0;
    float rsrq = 0;
    TEST_ASSERT_FALSE(parseCesq("", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("ERROR", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("+CESQ:", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255", &rsrp, &rsrq)); // truncated
    TEST_ASSERT_FALSE(parseCesq("+CESQ: 99,99,255,255,20,xx", &rsrp, &rsrq));
    TEST_ASSERT_FALSE(parseCesq(nullptr, &rsrp, &rsrq));
}

// --- AT+CEREG? ---

void test_cereg_states()
{
    TEST_ASSERT_EQUAL_INT(1, parseCeregStat("+CEREG: 2,1"));
    TEST_ASSERT_EQUAL_INT(2, parseCeregStat("+CEREG: 2,2"));
    TEST_ASSERT_EQUAL_INT(5, parseCeregStat("+CEREG: 2,5,\"1A2B\",\"01234567\",7"));
}

void test_cereg_malformed()
{
    TEST_ASSERT_EQUAL_INT(-1, parseCeregStat("+CEREG: 2"));
    TEST_ASSERT_EQUAL_INT(-1, parseCeregStat("+CEREG:"));
    TEST_ASSERT_EQUAL_INT(-1, parseCeregStat("OK"));
    TEST_ASSERT_EQUAL_INT(-1, parseCeregStat(""));
}

// --- AT*BANDIND? ---

void test_bandind_typical()
{
    uint8_t band = 0, act = 0;
    TEST_ASSERT_TRUE(parseBandInd("*BANDIND: 1,3,7", &band, &act));
    TEST_ASSERT_EQUAL_UINT8(3, band);
    TEST_ASSERT_EQUAL_UINT8(7, act); // 7 = E-UTRAN
}

void test_bandind_spaces_after_commas()
{
    // The V1179 answers "*BANDIND: 0, 1, 7" - padded, unlike every other response.
    uint8_t band = 0, act = 0;
    TEST_ASSERT_TRUE(parseBandInd("*BANDIND: 0, 1, 7", &band, &act));
    TEST_ASSERT_EQUAL_UINT8(1, band);
    TEST_ASSERT_EQUAL_UINT8(7, act);
}

void test_bandind_malformed()
{
    uint8_t band = 0, act = 0;
    TEST_ASSERT_FALSE(parseBandInd("*BANDIND: 1,3", &band, &act));
    TEST_ASSERT_FALSE(parseBandInd("+BANDIND: 1,3,7", &band, &act)); // wrong prefix
    TEST_ASSERT_FALSE(parseBandInd("*BANDIND: 1,3,999", &band, &act));
    TEST_ASSERT_FALSE(parseBandInd("", &band, &act));
}

// --- AT+CBC ---

void test_cbc_three_field_simcom_form()
{
    uint16_t mv = 0;
    TEST_ASSERT_TRUE(parseCbcMillivolts("+CBC: 0,75,3912", &mv));
    TEST_ASSERT_EQUAL_UINT16(3912, mv);
}

void test_cbc_single_field_air780e_form()
{
    // The Air780E answers with the millivolts alone, not <bcs>,<bcl>,<mV>.
    // Observed on V1179; reading field 2 alone dropped VBAT entirely.
    uint16_t mv = 0;
    TEST_ASSERT_TRUE(parseCbcMillivolts("+CBC: 4607", &mv));
    TEST_ASSERT_EQUAL_UINT16(4607, mv);
}

void test_cbc_implausible_rejected()
{
    // A reading outside a generous window around the module's 3.4-4.2 V range
    // is a different encoding, not a voltage to display.
    uint16_t mv = 0;
    TEST_ASSERT_FALSE(parseCbcMillivolts("+CBC: 0,75,39", &mv));
    TEST_ASSERT_FALSE(parseCbcMillivolts("+CBC: 0,75,39120", &mv));
    TEST_ASSERT_FALSE(parseCbcMillivolts("+CBC: 0,75", &mv));
    TEST_ASSERT_FALSE(parseCbcMillivolts("", &mv));
}

// --- AT+COPS? ---

void test_cops_numeric()
{
    char oper[12] = {0};
    TEST_ASSERT_TRUE(parseCopsNumeric("+COPS: 0,2,\"52501\",7", oper, sizeof(oper)));
    TEST_ASSERT_EQUAL_STRING("52501", oper);
}

void test_cops_name_long_and_short()
{
    char oper[24] = {0};
    TEST_ASSERT_TRUE(parseCopsName("+COPS: 0,0,\"CHN-UNICOM\",7", oper, sizeof(oper)));
    TEST_ASSERT_EQUAL_STRING("CHN-UNICOM", oper);
    TEST_ASSERT_TRUE(parseCopsName("+COPS: 0,1,\"unicom\",7", oper, sizeof(oper)));
    TEST_ASSERT_EQUAL_STRING("unicom", oper);
}

void test_cops_name_numeric_format_refused()
{
    // Format 2 is the numeric MCC/MNC, not a name.
    char oper[24] = {0};
    TEST_ASSERT_FALSE(parseCopsName("+COPS: 0,2,\"52501\",7", oper, sizeof(oper)));
}

void test_cops_not_registered()
{
    char oper[24] = {0};
    TEST_ASSERT_FALSE(parseCopsName("+COPS: 0", oper, sizeof(oper)));
    TEST_ASSERT_FALSE(parseCopsName("+COPS: 0,0", oper, sizeof(oper)));
    TEST_ASSERT_FALSE(parseCopsName("", oper, sizeof(oper)));
    TEST_ASSERT_FALSE(parseCopsNumeric("+COPS: 0", oper, sizeof(oper)));
    TEST_ASSERT_FALSE(parseCopsNumeric("+COPS: 0,2", oper, sizeof(oper)));
    TEST_ASSERT_FALSE(parseCopsNumeric("", oper, sizeof(oper)));
}

void test_cops_output_buffer_too_small()
{
    char oper[4] = {0};
    TEST_ASSERT_FALSE(parseCopsName("+COPS: 0,0,\"CHN-UNICOM\",7", oper, sizeof(oper)));
    TEST_ASSERT_EQUAL_STRING("", oper); // left untouched on failure
    TEST_ASSERT_FALSE(parseCopsNumeric("+COPS: 0,2,\"52501\",7", oper, sizeof(oper)));
    TEST_ASSERT_EQUAL_STRING("", oper);
}

// --- AT^SYSCONFIG ---

void test_sysconfig_typical()
{
    uint8_t mode = 0, acqorder = 0, srvdomain = 0;
    TEST_ASSERT_TRUE(parseSysConfig("^SYSCONFIG: 2,0,1,3", &mode, &acqorder, &srvdomain));
    TEST_ASSERT_EQUAL_UINT8(2, mode);
    TEST_ASSERT_EQUAL_UINT8(0, acqorder);
    TEST_ASSERT_EQUAL_UINT8(3, srvdomain);
}

void test_sysconfig_malformed()
{
    uint8_t mode = 0, acqorder = 0, srvdomain = 0;
    TEST_ASSERT_FALSE(parseSysConfig("^SYSCONFIG: 2,0,1", &mode, &acqorder, &srvdomain));   // missing srvdomain
    TEST_ASSERT_FALSE(parseSysConfig("+SYSCONFIG: 2,0,1,3", &mode, &acqorder, &srvdomain)); // wrong prefix
    TEST_ASSERT_FALSE(parseSysConfig("", &mode, &acqorder, &srvdomain));
}

// --- Unity lifecycle ---

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_cesq_typical);
    RUN_TEST(test_cesq_not_available);
    RUN_TEST(test_cesq_boundaries);
    RUN_TEST(test_cesq_out_of_range);
    RUN_TEST(test_cesq_multiline_response);
    RUN_TEST(test_cesq_malformed);
    RUN_TEST(test_cereg_states);
    RUN_TEST(test_cereg_malformed);
    RUN_TEST(test_bandind_typical);
    RUN_TEST(test_bandind_spaces_after_commas);
    RUN_TEST(test_bandind_malformed);
    RUN_TEST(test_cbc_three_field_simcom_form);
    RUN_TEST(test_cbc_single_field_air780e_form);
    RUN_TEST(test_cbc_implausible_rejected);
    RUN_TEST(test_cops_numeric);
    RUN_TEST(test_cops_name_long_and_short);
    RUN_TEST(test_cops_name_numeric_format_refused);
    RUN_TEST(test_cops_not_registered);
    RUN_TEST(test_cops_output_buffer_too_small);
    RUN_TEST(test_sysconfig_typical);
    RUN_TEST(test_sysconfig_malformed);
    exit(UNITY_END());
}

void loop() {}
