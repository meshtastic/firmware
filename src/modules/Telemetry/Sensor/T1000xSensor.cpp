#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(T1000X_SENSOR_EN)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "T1000xSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_Sensor.h>

#define T1000X_SENSE_SAMPLES 15
#define T1000X_LIGHT_REF_VCC 2400

#define HEATER_NTC_BX 4250   // thermistor coefficient B
#define HEATER_NTC_RP 8250   // ohm, series resistance to thermistor
#define HEATER_NTC_KA 273.15 // 25 Celsius at Kelvin
#define NTC_REF_VCC 3000     // mV, output voltage of LDO

// ntc res table
uint32_t ntc_res2[136] = {
    113347, 107565, 102116, 96978, 92132, 87559, 83242, 79166, 75316, 71677, 68237, 64991, 61919, 59011, 56258, 53650, 51178,
    48835,  46613,  44506,  42506, 40600, 38791, 37073, 35442, 33892, 32420, 31020, 29689, 28423, 27219, 26076, 24988, 23951,
    22963,  22021,  21123,  20267, 19450, 18670, 17926, 17214, 16534, 15886, 15266, 14674, 14108, 13566, 13049, 12554, 12081,
    11628,  11195,  10780,  10382, 10000, 9634,  9284,  8947,  8624,  8315,  8018,  7734,  7461,  7199,  6948,  6707,  6475,
    6253,   6039,   5834,   5636,  5445,  5262,  5086,  4917,  4754,  4597,  4446,  4301,  4161,  4026,  3896,  3771,  3651,
    3535,   3423,   3315,   3211,  3111,  3014,  2922,  2834,  2748,  2666,  2586,  2509,  2435,  2364,  2294,  2228,  2163,
    2100,   2040,   1981,   1925,  1870,  1817,  1766,  1716,  1669,  1622,  1578,  1535,  1493,  1452,  1413,  1375,  1338,
    1303,   1268,   1234,   1202,  1170,  1139,  1110,  1081,  1053,  1026,  999,   974,   949,   925,   902,   880,   858,
};

int8_t ntc_temp2[136] = {
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8,
    -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14, 15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37, 38,
    39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60, 61,
    62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83, 84,
    85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
};

T1000xSensor::T1000xSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "T1000x") {}

int32_t T1000xSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

void T1000xSensor::setup()
{
    // Set up oversampling and filter initialization
}

float T1000xSensor::getLux()
{
    uint32_t lux_vot = 0;
    float lux_level = 0;

    for (uint32_t i = 0; i < T1000X_SENSE_SAMPLES; i++) {
        lux_vot += analogRead(T1000X_LUX_PIN);
    }
    lux_vot = lux_vot / T1000X_SENSE_SAMPLES;
    lux_vot = ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * lux_vot;

    if (lux_vot <= 80)
        lux_level = 0;
    else if (lux_vot >= 2480)
        lux_level = 100;
    else
        lux_level = 100 * (lux_vot - 80) / T1000X_LIGHT_REF_VCC;

    return lux_level;
}

float T1000xSensor::getTemp()
{
    uint32_t vcc_vot = 0, ntc_vot = 0;

    uint8_t u8i = 0;
    float Vout = 0, Rt = 0, temp = 0;
    float Temp = 0;

    for (uint32_t i = 0; i < T1000X_SENSE_SAMPLES; i++) {
        vcc_vot += analogRead(T1000X_VCC_PIN);
    }
    vcc_vot = vcc_vot / T1000X_SENSE_SAMPLES;
    vcc_vot = 2 * ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * vcc_vot;

    for (uint32_t i = 0; i < T1000X_SENSE_SAMPLES; i++) {
        ntc_vot += analogRead(T1000X_NTC_PIN);
    }
    ntc_vot = ntc_vot / T1000X_SENSE_SAMPLES;
    ntc_vot = ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * ntc_vot;

    Vout = ntc_vot;
    Rt = (HEATER_NTC_RP * vcc_vot) / Vout - HEATER_NTC_RP;
    for (u8i = 0; u8i < 135; u8i++) {
        if (Rt >= ntc_res2[u8i]) {
            break;
        }
    }
    temp = ntc_temp2[u8i - 1] + 1 * (ntc_res2[u8i - 1] - Rt) / (float)(ntc_res2[u8i - 1] - ntc_res2[u8i]);
    Temp = (temp * 100 + 5) / 100; // half adjust

    return Temp;
}

bool T1000xSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_lux = true;

    measurement->variant.environment_metrics.temperature = getTemp();
    measurement->variant.environment_metrics.lux = getLux();
    return true;
}

#endif