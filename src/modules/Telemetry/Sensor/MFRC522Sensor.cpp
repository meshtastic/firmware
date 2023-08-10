#include "MFRC522Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include "MFRC522_lib/MFRC522_I2C.h"
#include <typeinfo>
#include <Adafruit_I2CDevice.h>

MFRC522Sensor::MFRC522Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MFRC522, "MFRC522") {}

int32_t MFRC522Sensor::runOnce()
{
    // LOG_INFO("!hasSensor: %s\n", !hasSensor());
    LOG_INFO("Init sensor: %s\n", sensorName);
    // LOG_INFO("!hasSensor: %s\n", !hasSensor());
    if (!hasSensor())
    {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = mfrc522.begin_I2C();

    return initI2CSensor();
}

void MFRC522Sensor::setup() {}

bool MFRC522Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MFRC522Sensor::getMetrics\n");

    uint32_t ID_RFID = 0x9AA3DD0B;

    mfrc522.PCD_Init();

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
    {
        // запишем метку в 4 байта

        for (byte i = 0; i < 4; i++)
        {
            ID_RFID <<= 8;
            ID_RFID |= mfrc522.uid.uidByte[i];
        }
    }

    Serial.print("ID_RFID = ");
    Serial.println(ID_RFID,HEX);


    measurement->variant.environment_metrics.temperature = (float)ID_RFID;
    measurement->variant.environment_metrics.relative_humidity = 0;
    measurement->variant.environment_metrics.barometric_pressure = 0;

    return true;
}