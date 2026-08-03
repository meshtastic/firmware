#include "SGM41562.h"

#ifdef HAS_SGM41562

#include <Arduino.h>

#include "Throttle.h"

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
    chipType_ = ChipType::Unknown;

    uint8_t id;
    if (!readReg(REG_DEVICE_ID, id)) {
        LOG_WARN("SGM41562: I2C read failed at 0x%02X", address_);
        return false;
    }

    if (!resetRegisters()) {
        LOG_WARN("SGM41562: register reset failed");
        return false;
    }

    chipType_ = detectChipType(id);
    if (chipType_ == ChipType::Unknown) {
        LOG_WARN("SGM41562: unsupported device ID 0x%02X", id);
        return false;
    }

    if (!applyInitSequence()) {
        LOG_WARN("SGM41562: %s initialization failed", chipTypeName(chipType_));
        chipType_ = ChipType::Unknown;
        return false;
    }

    LOG_INFO("SGM41562: detected %s at 0x%02X (id 0x%02X)", chipTypeName(chipType_), address_, id);
    lastRefreshMs_ = 0;
    return refresh();
}

const char *SGM41562::chipTypeName(ChipType type)
{
    switch (type) {
    case ChipType::SGM41562:
        return "SGM41562";
    case ChipType::SGM41562A:
        return "SGM41562A";
    case ChipType::SGM41562B:
        return "SGM41562B";
    case ChipType::SGM41562S:
        return "SGM41562S";
    case ChipType::SGM41562SA:
        return "SGM41562SA";
    case ChipType::Unknown:
    default:
        return "unknown";
    }
}

bool SGM41562::resetRegisters()
{
    uint8_t value;
    if (!readReg(REG_CHARGE_CURRENT, value))
        return false;
    if (!writeReg(REG_CHARGE_CURRENT, value | 0x80))
        return false;
    delay(10);
    return true;
}

SGM41562::ChipType SGM41562::detectChipType(uint8_t deviceId)
{
    switch (deviceId) {
    case DEVICE_ID_SGM41562:
        return ChipType::SGM41562;
    case DEVICE_ID_SGM41562_A:
        return ChipType::SGM41562A;
    case DEVICE_ID_SGM41562_S:
        return ChipType::SGM41562S;
    case DEVICE_ID_SGM41562_B_SA:
        return detectIdZeroChipType();
    default:
        return ChipType::Unknown;
    }
}

SGM41562::ChipType SGM41562::detectIdZeroChipType()
{
    uint8_t chargeVoltage;
    uint8_t systemVoltage;
    if (!readReg(REG_CHARGE_VOLTAGE, chargeVoltage) || !readReg(REG_SYS_VOLTAGE_REG, systemVoltage))
        return ChipType::Unknown;

    if (chargeVoltage == 0xA3 && systemVoltage == 0x37)
        return ChipType::SGM41562B;
    if (chargeVoltage == 0x8D && systemVoltage == 0x73)
        return ChipType::SGM41562SA;

    LOG_WARN("SGM41562: unknown ID 0x00 reset values (REG04=0x%02X, REG07=0x%02X)", chargeVoltage, systemVoltage);
    return ChipType::Unknown;
}

bool SGM41562::applyInitSequence()
{
    if (hasExtendedRegisterMap()) {
        return writeReg(REG_SYS_VOLTAGE_REG, 0x73) && writeReg(REG_MISC_OP_CONTROL, 0x40) &&
               writeReg(REG_CHARGE_TERM_TIMER, 0x1A) && writeReg(REG_SYSTEM_STATUS, 0x40) &&
               writeReg(REG_EXT_INPUT_CURRENT, 0xCA) && writeReg(REG_POWER_ON_CFG, 0xA4);
    }

    return writeReg(REG_SYS_VOLTAGE_REG, 0xB7) && writeReg(REG_MISC_OP_CONTROL, 0x40) && writeReg(REG_CHARGE_TERM_TIMER, 0x1A) &&
           writeReg(REG_SYSTEM_STATUS, 0x40) && writeReg(REG_POWER_ON_CFG, 0xA4);
}

bool SGM41562::hasExtendedRegisterMap() const
{
    return chipType_ == ChipType::SGM41562S || chipType_ == ChipType::SGM41562SA;
}

bool SGM41562::refresh()
{
    uint32_t now = millis();
    if (lastRefreshMs_ != 0 && Throttle::isWithinTimespanMs(lastRefreshMs_, 250))
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
