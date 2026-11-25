#include "variant.h"
#include "configuration.h"
#include "power/PowerHAL.h"
#include <Arduino.h>
#include <Wire.h>

#define M5IOE1_REG_GPIO_MODE 0x03 // 1 = output
#define M5IOE1_REG_GPIO_OUT 0x05
#define M5IOE1_REG_GPIO_DRV 0x13 // 1 = open drain (reset default)

static uint16_t ioeOut; // GPIO_OUT shadow, so single rails can be switched

static void ioeWrite16(uint8_t reg, uint16_t value)
{
    Wire1.beginTransmission(M5IOE1_ADDR);
    Wire1.write(reg);
    Wire1.write(value & 0xFF);
    Wire1.write(value >> 8);
    Wire1.endTransmission();
}

static void ioeSetOutputs(uint16_t value)
{
    ioeOut = value;
    ioeWrite16(M5IOE1_REG_GPIO_OUT, value);
}

static bool ip2315Awake(TwoWire &bus)
{
    bus.beginTransmission(IP2315_ADDR);
    return bus.endTransmission() == 0;
}

bool powerHAL_isVBUSConnected()
{
    return ip2315Awake(Wire) || ip2315Awake(Wire1);
}

void earlyInitVariant()
{
    pinMode(LED_PAIRING, OUTPUT);
    digitalWrite(LED_PAIRING, !LED_STATE_ON);

    // Radio and GNSS rails sit behind the M5IOE1; raise them before the radio and GPS probe
    const uint16_t outputs = (1 << IOE_LORA_PWR_EN) | (1 << IOE_LORA_ANT_SW) | (1 << IOE_GNSS_PWR_EN);
    Wire1.begin(I2C_SDA1, I2C_SCL1);
    ioeSetOutputs(outputs);
    ioeWrite16(M5IOE1_REG_GPIO_DRV, 0);
    ioeWrite16(M5IOE1_REG_GPIO_MODE, outputs);
    delay(10); // SY8089 soft start before the SX1262 is reset
}

#if !MESHTASTIC_EXCLUDE_GPS
#include "GpioLogic.h"
#include "NodeDB.h"
#include "gps/GPS.h"

// The GNSS LDO enable is an M5IOE1 output, so GPS hardsleep and GPS_OFF reach it through gps->enablePin
class GnssRail : public GpioPin
{
  public:
    void set(bool on) override
    {
        const uint16_t bit = 1 << IOE_GNSS_PWR_EN;
        ioeSetOutputs(on ? (ioeOut | bit) : (ioeOut & ~bit));
    }
};
#endif

void lateInitVariant()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (gps) {
        // A user-set gps_en_gpio already owns enablePin's only transformer; the rail then stays up
        if (config.position.gps_en_gpio)
            return;
        auto *rail = new GnssRail();
        new GpioUnaryTransformer(gps->enablePin, rail);
        // The GPS may already have set its power state (e.g. disabled while the region is unset)
        if (gps->enablePin->get() != GpioVirtPin::PinState::Unset)
            rail->set(gps->enablePin->get() == GpioVirtPin::PinState::On);
        return;
    }
#endif
    // No GPS driver (gps_mode NOT_PRESENT or GPS excluded), so nothing else would switch the rail off
    ioeSetOutputs(ioeOut & ~(1 << IOE_GNSS_PWR_EN));
}

// Runs before every deep sleep; the M5IOE1 stays powered, so drop the GNSS and RF switch rails. Keep the LoRa
// rail: LORA_CS is held high through sleep and would back-power an unpowered SX1262.
void variant_shutdown()
{
    ioeSetOutputs(1 << IOE_LORA_PWR_EN);
}
