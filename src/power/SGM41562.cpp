#include "SGM41562.h"

#ifdef HAS_SGM41562

#include <Arduino.h>

SGM41562 *sgm41562 = nullptr;

bool initSGM41562(TwoWire &wire)
{
    if (sgm41562)
        return true;
    sgm41562 = new SGM41562();
    if (!sgm41562->begin(wire)) {
        delete sgm41562;
        sgm41562 = nullptr;
        return false;
    }
    return true;
}

bool SGM41562::readReg(uint8_t reg, uint8_t &value)
{
    wire_->beginTransmission(address_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0)
        return false;
    if (wire_->requestFrom((int)address_, 1) != 1)
        return false;
    value = wire_->read();
    return true;
}

bool SGM41562::writeReg(uint8_t reg, uint8_t value)
{
    wire_->beginTransmission(address_);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
}

bool SGM41562::updateReg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur;
    if (!readReg(reg, cur))
        return false;
    cur = (cur & ~mask) | (value & mask);
    return writeReg(reg, cur);
}

bool SGM41562::begin(TwoWire &wire, uint8_t address)
{
    wire_ = &wire;
    address_ = address;

    uint8_t id;
    if (!readReg(REG_DEVICE_ID, id)) {
        LOG_WARN("SGM41562: I2C read failed at 0x%02X", address_);
        return false;
    }
    if (id != DEVICE_ID_EXPECTED) {
        LOG_WARN("SGM41562: unexpected device ID 0x%02X (expected 0x%02X)", id, DEVICE_ID_EXPECTED);
        return false;
    }
    LOG_INFO("SGM41562: detected at 0x%02X (id 0x%02X)", address_, id);

    // Mirror the vendor reference init sequence: PCB OTP off, NTC off,
    // watchdog off, charger enabled. These match LilyGo's stock firmware
    // for the T-Impulse Plus.
    delay(120);
    writeReg(REG_SYS_VOLTAGE_REG, 0xB7);
    writeReg(REG_MISC_OP_CONTROL, 0x40);
    writeReg(REG_CHARGE_TERM_TIMER, 0x1A);
    writeReg(REG_POWER_ON_CFG, 0xA4);

    return refresh();
}

bool SGM41562::refresh()
{
    uint32_t now = millis();
    if (lastRefreshMs_ != 0 && (now - lastRefreshMs_) < 250)
        return true; // cached
    lastRefreshMs_ = now == 0 ? 1 : now;

    uint8_t status, fault;
    if (!readReg(REG_SYSTEM_STATUS, status))
        return false;
    if (!readReg(REG_FAULT, fault))
        return false;

    chargeStatus_ = static_cast<ChargeStatus>((status >> SYS_STATUS_CHRG_SHIFT) & SYS_STATUS_CHRG_MASK);
    inputPowerGood_ = (status & SYS_STATUS_PG) != 0;
    thermalReg_ = (status & SYS_STATUS_THERM_REG) != 0;
    faultMask_ = fault & 0x3F; // bits [7:6] are enter_ship_time config, not faults
    return true;
}

bool SGM41562::setChargeEnable(bool enable)
{
    return updateReg(REG_POWER_ON_CFG, POWER_ON_CFG_CHG_DISABLE, enable ? 0x00 : POWER_ON_CFG_CHG_DISABLE);
}

bool SGM41562::setShippingModeEnable(bool enable)
{
    return updateReg(REG_MISC_OP_CONTROL, MISC_OP_SHIPPING_MODE, enable ? MISC_OP_SHIPPING_MODE : 0x00);
}

#endif // HAS_SGM41562
