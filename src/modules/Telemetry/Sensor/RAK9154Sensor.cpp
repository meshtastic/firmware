#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK9154Sensor.h"
#include "TelemetrySensor.h"
#include "concurrency/Periodic.h"
#include <RAK-OneWireSerial.h>

using namespace concurrency;

#define BOOT_DATA_REQ

RAK9154Sensor::RAK9154Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "RAK1954") {}

static Periodic *onewirePeriodic;

static SoftwareHalfSerial mySerial(HALF_UART_PIN); // Wire pin  P0.15

static uint8_t buff[0x100];
static uint16_t bufflen = 0;

static int16_t dc_cur = 0;
static uint16_t dc_vol = 0;
static uint8_t dc_prec = 0;
static uint8_t provision = 0;

extern RAK9154Sensor rak9154Sensor;

static void onewire_evt(const uint8_t pid, const uint8_t sid, const SNHUBAPI_EVT_E eid, uint8_t *msg, uint16_t len)
{
    switch (eid) {
    case SNHUBAPI_EVT_RECV_REQ:
    case SNHUBAPI_EVT_RECV_RSP:
        break;

    case SNHUBAPI_EVT_QSEND:
        mySerial.write(msg, len);
        break;

    case SNHUBAPI_EVT_ADD_SID:
        // LOG_INFO("+ADD:SID:[%02x]", msg[0]);
        break;

    case SNHUBAPI_EVT_ADD_PID:
        // LOG_INFO("+ADD:PID:[%02x]", msg[0]);
#ifdef BOOT_DATA_REQ
        provision = msg[0];
#endif
        break;

    case SNHUBAPI_EVT_GET_INTV:
        break;

    case SNHUBAPI_EVT_GET_ENABLE:
        break;

    case SNHUBAPI_EVT_SDATA_REQ:

        // LOG_INFO("+EVT:PID[%02x],IPSO[%02x]",pid,msg[0]);
        // for( uint16_t i=1; i<len; i++)
        // {
        //     LOG_INFO("%02x,", msg[i]);
        // }
        // LOG_INFO("");
        switch (msg[0]) {
        case RAK_IPSO_CAPACITY:
            dc_prec = msg[1];
            if (dc_prec > 100) {
                dc_prec = 100;
            }
            break;
        case RAK_IPSO_DC_CURRENT:
            dc_cur = (msg[2] << 8) + msg[1];
            break;
        case RAK_IPSO_DC_VOLTAGE:
            dc_vol = (msg[2] << 8) + msg[1];
            dc_vol *= 10;
            break;
        default:
            break;
        }
        rak9154Sensor.setLastRead(millis());

        break;
    case SNHUBAPI_EVT_REPORT:

        // LOG_INFO("+EVT:PID[%02x],IPSO[%02x]",pid,msg[0]);
        // for( uint16_t i=1; i<len; i++)
        // {
        //     LOG_INFO("%02x,", msg[i]);
        // }
        // LOG_INFO("");

        switch (msg[0]) {
        case RAK_IPSO_CAPACITY:
            dc_prec = msg[1];
            if (dc_prec > 100) {
                dc_prec = 100;
            }
            break;
        case RAK_IPSO_DC_CURRENT:
            dc_cur = (msg[1] << 8) + msg[2];
            break;
        case RAK_IPSO_DC_VOLTAGE:
            dc_vol = (msg[1] << 8) + msg[2];
            dc_vol *= 10;
            break;
        default:
            break;
        }
        rak9154Sensor.setLastRead(millis());

        break;

    case SNHUBAPI_EVT_CHKSUM_ERR:
        LOG_INFO("+ERR:CHKSUM");
        break;

    case SNHUBAPI_EVT_SEQ_ERR:
        LOG_INFO("+ERR:SEQUCE");
        break;

    default:
        break;
    }
}

static int32_t onewireHandle()
{
    if (provision != 0) {
        RakSNHub_Protocl_API.get.data(provision);
        provision = 0;
    }

    while (mySerial.available()) {
        char a = mySerial.read();
        buff[bufflen++] = a;
        delay(2); // continue data, timeout=2ms
    }

    if (bufflen != 0) {
        RakSNHub_Protocl_API.process((uint8_t *)buff, bufflen);
        bufflen = 0;
    }

    return 50;
}

int32_t RAK9154Sensor::runOnce()
{
    if (!rak9154Sensor.isInitialized()) {
        onewirePeriodic = new Periodic("onewireHandle", onewireHandle);

        mySerial.begin(9600);

        RakSNHub_Protocl_API.init(onewire_evt);

        status = true;
        initialized = true;
    }

    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

void RAK9154Sensor::setup()
{
    // Set up oversampling and filter initialization
}

bool RAK9154Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (getBusVoltageMv() > 0) {
        measurement->variant.environment_metrics.has_voltage = true;
        measurement->variant.environment_metrics.has_current = true;

        measurement->variant.environment_metrics.voltage = (float)getBusVoltageMv() / 1000;
        measurement->variant.environment_metrics.current = (float)getCurrentMa() / 1000;
        return true;
    } else {
        return false;
    }
}

uint16_t RAK9154Sensor::getBusVoltageMv()
{
    return dc_vol;
}

int16_t RAK9154Sensor::getCurrentMa()
{
    return dc_cur;
}

int RAK9154Sensor::getBusBatteryPercent()
{
    return (int)dc_prec;
}

bool RAK9154Sensor::isCharging()
{
    return (dc_cur > 0) ? true : false;
}
void RAK9154Sensor::setLastRead(uint32_t lastRead)
{
    this->lastRead = lastRead;
}
#endif // HAS_RAKPROT
