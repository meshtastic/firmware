#include "i2c_slave.h"
#include <Arduino.h>
#include <Wire.h>

volatile OtaStatus g_status      = STATUS_IDLE;
volatile OtaCmd    g_pending_cmd = CMD_STATUS;
volatile bool      g_cmd_ready   = false;

// Called from the I2C ISR when the master writes data to us.
static void onReceive(int numBytes)
{
    if (numBytes < 1) return;

    uint8_t cmd = Wire.read();
    // Drain any extra bytes the master may have sent
    while (Wire.available()) Wire.read();

    if (cmd == CMD_STATUS) {
        // No action needed — onRequest() will send g_status
        return;
    }

    // Queue the command for the main loop
    g_pending_cmd = static_cast<OtaCmd>(cmd);
    g_cmd_ready   = true;
}

// Called from the I2C ISR when the master reads from us (after CMD_STATUS).
static void onRequest()
{
    Wire.write(static_cast<uint8_t>(g_status));
}

void i2cSlaveInit(uint8_t addr, int sda, int scl)
{
    Wire.begin(static_cast<int>(addr), sda, scl);
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    Serial.printf("[I2C] Slave initialized at 0x%02X (SDA=%d SCL=%d)\n", addr, sda, scl);
}
