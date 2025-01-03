#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(SENSECAP_INDICATOR)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "IndicatorSensor.h"
#include "TelemetrySensor.h"
#include "serialization/cobs.h"
#include <Adafruit_Sensor.h>
#include <driver/uart.h>

IndicatorSensor::IndicatorSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "Indicator") {}

#define SENSOR_BUF_SIZE (512)

uint8_t buf[SENSOR_BUF_SIZE];  // recv
uint8_t data[SENSOR_BUF_SIZE]; // decode

#define ACK_PKT_PARA "ACK"

enum sensor_pkt_type {
    PKT_TYPE_ACK = 0x00,                  // uin32_t
    PKT_TYPE_CMD_COLLECT_INTERVAL = 0xA0, // uin32_t
    PKT_TYPE_CMD_BEEP_ON = 0xA1,          // uin32_t  ms: on time
    PKT_TYPE_CMD_BEEP_OFF = 0xA2,
    PKT_TYPE_CMD_SHUTDOWN = 0xA3, // uin32_t
    PKT_TYPE_CMD_POWER_ON = 0xA4,
    PKT_TYPE_SENSOR_SCD41_TEMP = 0xB0,     // float
    PKT_TYPE_SENSOR_SCD41_HUMIDITY = 0xB1, // float
    PKT_TYPE_SENSOR_SCD41_CO2 = 0xB2,      // float
    PKT_TYPE_SENSOR_AHT20_TEMP = 0xB3,     // float
    PKT_TYPE_SENSOR_AHT20_HUMIDITY = 0xB4, // float
    PKT_TYPE_SENSOR_TVOC_INDEX = 0xB5,     // float
};

static int cmd_send(uint8_t cmd, const char *p_data, uint8_t len)
{
    uint8_t send_buf[32] = {0};
    uint8_t send_data[32] = {0};

    if (len > 31) {
        return -1;
    }

    uint8_t index = 1;

    send_data[0] = cmd;

    if (len > 0 && p_data != NULL) {
        memcpy(&send_data[1], p_data, len);
        index += len;
    }
    cobs_encode_result ret = cobs_encode(send_buf, sizeof(send_buf), send_data, index);

    // LOG_DEBUG("cobs TX status:%d, len:%d, type 0x%x", ret.status, ret.out_len, cmd);

    if (ret.status == COBS_ENCODE_OK) {
        return uart_write_bytes(SENSOR_PORT_NUM, send_buf, ret.out_len + 1);
    }

    return -1;
}

int32_t IndicatorSensor::runOnce()
{
    LOG_INFO("%s: init", sensorName);
    setup();
    return 2 * DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS; // give it some time to start up
}

void IndicatorSensor::setup()
{
    uart_config_t uart_config = {
        .baud_rate = SENSOR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    int intr_alloc_flags = 0;
    char buffer[11];

    uart_driver_install(SENSOR_PORT_NUM, SENSOR_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags);
    uart_param_config(SENSOR_PORT_NUM, &uart_config);
    uart_set_pin(SENSOR_PORT_NUM, SENSOR_RP2040_TXD, SENSOR_RP2040_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    cmd_send(PKT_TYPE_CMD_POWER_ON, NULL, 0);
    // measure and send only once every minute, for the phone API
    const char *interval = ultoa(60000, buffer, 10);
    cmd_send(PKT_TYPE_CMD_COLLECT_INTERVAL, interval, strlen(interval) + 1);
}

bool IndicatorSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    cobs_decode_result ret;
    int len = uart_read_bytes(SENSOR_PORT_NUM, buf, (SENSOR_BUF_SIZE - 1), 100 / portTICK_PERIOD_MS);

    float value = 0.0;
    uint8_t *p_buf_start = buf;
    uint8_t *p_buf_end = buf;
    if (len > 0) {
        while (p_buf_start < (buf + len)) {
            p_buf_end = p_buf_start;
            while (p_buf_end < (buf + len)) {
                if (*p_buf_end == 0x00) {
                    break;
                }
                p_buf_end++;
            }
            // decode buf
            memset(data, 0, sizeof(data));
            ret = cobs_decode(data, sizeof(data), p_buf_start, p_buf_end - p_buf_start);

            // LOG_DEBUG("cobs RX status:%d, len:%d, type:0x%x  ", ret.status, ret.out_len, data[0]);

            if (ret.out_len > 1 && ret.status == COBS_DECODE_OK) {

                value = 0.0;
                uint8_t pkt_type = data[0];
                switch (pkt_type) {
                case PKT_TYPE_SENSOR_SCD41_CO2: {
                    memcpy(&value, &data[1], sizeof(value));
                    // LOG_DEBUG("CO2: %.1f", value);
                    cmd_send(PKT_TYPE_ACK, ACK_PKT_PARA, 4);
                    break;
                }

                case PKT_TYPE_SENSOR_AHT20_TEMP: {
                    memcpy(&value, &data[1], sizeof(value));
                    // LOG_DEBUG("Temp: %.1f", value);
                    cmd_send(PKT_TYPE_ACK, ACK_PKT_PARA, 4);
                    measurement->variant.environment_metrics.has_temperature = true;
                    measurement->variant.environment_metrics.temperature = value;
                    break;
                }

                case PKT_TYPE_SENSOR_AHT20_HUMIDITY: {
                    memcpy(&value, &data[1], sizeof(value));
                    // LOG_DEBUG("Humidity: %.1f", value);
                    cmd_send(PKT_TYPE_ACK, ACK_PKT_PARA, 4);
                    measurement->variant.environment_metrics.has_relative_humidity = true;
                    measurement->variant.environment_metrics.relative_humidity = value;
                    break;
                }

                case PKT_TYPE_SENSOR_TVOC_INDEX: {
                    memcpy(&value, &data[1], sizeof(value));
                    // LOG_DEBUG("Tvoc: %.1f", value);
                    cmd_send(PKT_TYPE_ACK, ACK_PKT_PARA, 4);
                    measurement->variant.environment_metrics.has_iaq = true;
                    measurement->variant.environment_metrics.iaq = value;
                    break;
                }
                default:
                    break;
                }
            }

            p_buf_start = p_buf_end + 1; // next message
        }
        return true;
    }
    return false;
}

#endif