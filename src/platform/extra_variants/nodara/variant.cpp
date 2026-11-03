#ifdef NODARA

// Nodara has no power switch: an always-on power MCU owns the power button and reports it to us
// as Modbus RTU frames on a receive-only 1200 baud line (Serial2).

#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "mesh/Throttle.h"

namespace
{
constexpr uint32_t MCU_BAUD = 1200;
constexpr uint32_t POLL_MS = 20;       // a quiet poll ends a frame; one 1200 baud byte takes ~8 ms
constexpr uint32_t IDLE_POLL_MS = 250; // the 64-byte UART buffer holds ~0.5 s of the line
// The power MCU powers us as soon as the button goes down and confirms after a ~5 s hold. An
// uptime deadline, not a duration: the confirm is timed from power-on, not from this wait.
constexpr uint32_t POWER_ON_DEADLINE_MS = 5500;
constexpr uint8_t MODBUS_ADDR = 0x01;
constexpr uint16_t POWER_REG = 0x1000;
constexpr uint16_t CMD_SHUTDOWN_DELAYED = 0x0003;
constexpr uint16_t CMD_POWER = 0x0005; // value 1 confirms power-on, 0 (or none) asks for shutdown now

uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8) | p[1];
}

bool crcOk(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n - 2; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc == (uint16_t)(p[n - 2] | (p[n - 1] << 8));
}

/// Find the next power-register frame at or after pos. Write single register (0x06) carries a
/// command, write multiple (0x10) a command and a value. Leaves pos just past the frame.
bool nextFrame(const uint8_t *buf, size_t len, size_t &pos, uint16_t &cmd, uint16_t &value)
{
    for (; pos + 1 < len; pos++) {
        const uint8_t *f = buf + pos;
        if (f[0] != MODBUS_ADDR)
            continue;
        if (f[1] == 0x06 && pos + 8 <= len && crcOk(f, 8) && be16(f + 2) == POWER_REG) {
            cmd = be16(f + 4);
            value = 0;
            pos += 8;
            return true;
        }
        if (f[1] == 0x10 && pos + 13 <= len && crcOk(f, 13) && be16(f + 2) == POWER_REG) {
            cmd = be16(f + 7);
            value = be16(f + 9);
            pos += 13;
            return true;
        }
    }
    return false;
}

class PowerMcuLink : public concurrency::OSThread
{
  public:
    PowerMcuLink() : OSThread("PowerMcu") {}

  protected:
    int32_t runOnce() override
    {
        bool got = false;
        while (Serial2.available() > 0 && len < sizeof(buf)) {
            buf[len++] = Serial2.read();
            got = true;
        }
        if (!got && len > 0) {
            handleFrames();
            len = 0;
        }
        return len > 0 ? POLL_MS : IDLE_POLL_MS;
    }

  private:
    uint8_t buf[32] = {};
    size_t len = 0;

    void handleFrames()
    {
        char hex[sizeof(buf) * 3 + 1] = {};
        for (size_t i = 0; i < len; i++)
            snprintf(hex + i * 3, 4, "%02X ", buf[i]);
        LOG_DEBUG("Power MCU RX: %s", hex);

        size_t pos = 0;
        uint16_t cmd, value;
        while (nextFrame(buf, len, pos, cmd, value)) {
            if (cmd == CMD_POWER && value) {
                LOG_INFO("Power MCU confirmed power-on");
            } else if (cmd == CMD_POWER || cmd == CMD_SHUTDOWN_DELAYED) {
                LOG_INFO("Power MCU requested shutdown%s", cmd == CMD_SHUTDOWN_DELAYED ? " (delayed)" : "");
                // The same request the shutdown menu makes
                shutdownAtMsec = cmd == CMD_SHUTDOWN_DELAYED ? Time::timerEndsAtMillis(DEFAULT_SHUTDOWN_SECONDS * 1000)
                                                             : Time::skipZero(Time::getMillis());
            } else {
                LOG_WARN("Power MCU sent unknown command 0x%04x value 0x%04x", cmd, value);
            }
        }
    }
};

PowerMcuLink *powerMcuLink;
uint32_t powerOnConfirmMs; // earlyInitVariant() can't log, so lateInitVariant() reports this
bool waitedForPowerOn;
} // namespace

void earlyInitVariant()
{
    pinMode(MCU_DATA_PIN, INPUT_PULLUP);
    Serial2.begin(MCU_BAUD);
    NRF_UARTE1->PSEL.TXD = UARTE_PSEL_TXD_CONNECT_Disconnected << UARTE_PSEL_TXD_CONNECT_Pos;

    // On a power-on reset, hold boot until the press is long enough to keep us on. A short tap
    // cuts our power again, and this keeps it from interrupting flash writes or e-ink refreshes.
    // Reboots and reflashes get no confirm, so they skip the wait.
    if (readResetReason() != 0)
        return;
    waitedForPowerOn = true;
    uint8_t buf[32] = {};
    size_t len = 0;
    while (!Throttle::deadlinePassed(POWER_ON_DEADLINE_MS)) {
        if (len == sizeof(buf)) // noise filled it: start over rather than stop matching
            len = 0;
        if (Serial2.available() > 0)
            buf[len++] = Serial2.read();
        size_t pos = 0;
        uint16_t cmd, value;
        while (nextFrame(buf, len, pos, cmd, value)) {
            if (cmd == CMD_POWER && value) {
                powerOnConfirmMs = millis();
                return;
            }
        }
        delay(1);
    }
}

void lateInitVariant()
{
    if (powerOnConfirmMs)
        LOG_INFO("Power MCU confirmed power-on at %u ms", (unsigned)powerOnConfirmMs);
    else if (waitedForPowerOn)
        LOG_WARN("No power-on confirm from the power MCU by %u ms", (unsigned)POWER_ON_DEADLINE_MS);
    powerMcuLink = new PowerMcuLink();
}

#endif
