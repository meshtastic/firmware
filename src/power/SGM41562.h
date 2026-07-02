#pragma once

#include "configuration.h"

#ifdef HAS_SGM41562

#include <Wire.h>
#include <stdint.h>

// SG Micro SGM41562 - single-cell Li-ion buck charger, I²C-controlled, no
// fuel gauge. This driver exposes status (charging / input good / fault),
// charge enable, and shipping-mode control. Battery voltage/percent still
// come from the platform ADC path; the charger is plumbed in as a
// side-channel for isCharging()/isVbusIn() in AnalogBatteryLevel.
//
// Reference: SGM41562 datasheet (Cmd map + bit fields cross-verified against
// LilyGo's `Cpp_Bus_Driver::Sgm41562xx` driver, which is what their vendor
// example for this board uses).

#ifndef SGM41562_ADDR
#define SGM41562_ADDR 0x03 // Per datasheet - unusual but correct
#endif

#ifndef SGM41562_WIRE
#define SGM41562_WIRE Wire1 // Most boards put the PMU on the secondary bus
#endif

class SGM41562
{
  public:
    enum class ChargeStatus : uint8_t {
        NotCharging = 0b00,
        Precharge = 0b01,
        FastCharge = 0b10,
        ChargeDone = 0b11,
    };

    bool begin(TwoWire &wire, uint8_t address = SGM41562_ADDR);

    // Re-read the system status + fault registers. Throttled internally to
    // at most one I²C transaction per 250 ms - call as often as you like.
    bool refresh();

    // Status - cached from the most recent refresh().
    ChargeStatus chargeStatus() const { return chargeStatus_; }
    bool isCharging() const { return chargeStatus_ == ChargeStatus::Precharge || chargeStatus_ == ChargeStatus::FastCharge; }
    bool isChargeDone() const { return chargeStatus_ == ChargeStatus::ChargeDone; }
    bool isInputPowerGood() const { return inputPowerGood_; }
    bool isThermalRegulation() const { return thermalReg_; }
    uint8_t faultMask() const { return faultMask_; }

    // Control.
    bool setChargeEnable(bool enable);
    bool setShippingModeEnable(bool enable);

  private:
    TwoWire *wire_ = nullptr;
    uint8_t address_ = SGM41562_ADDR;
    uint32_t lastRefreshMs_ = 0;

    ChargeStatus chargeStatus_ = ChargeStatus::NotCharging;
    bool inputPowerGood_ = false;
    bool thermalReg_ = false;
    uint8_t faultMask_ = 0;

    bool readReg(uint8_t reg, uint8_t &value);
    bool writeReg(uint8_t reg, uint8_t value);
    bool updateReg(uint8_t reg, uint8_t mask, uint8_t value);

    // SGM41562 register addresses
    static constexpr uint8_t REG_INPUT_SOURCE = 0x00;
    static constexpr uint8_t REG_POWER_ON_CFG = 0x01;
    static constexpr uint8_t REG_CHARGE_CURRENT = 0x02;
    static constexpr uint8_t REG_DISCHARGE_TERM_CURRENT = 0x03;
    static constexpr uint8_t REG_CHARGE_VOLTAGE = 0x04;
    static constexpr uint8_t REG_CHARGE_TERM_TIMER = 0x05;
    static constexpr uint8_t REG_MISC_OP_CONTROL = 0x06;
    static constexpr uint8_t REG_SYS_VOLTAGE_REG = 0x07;
    static constexpr uint8_t REG_SYSTEM_STATUS = 0x08;
    static constexpr uint8_t REG_FAULT = 0x09;
    static constexpr uint8_t REG_I2C_ADDR_MISC = 0x0A;
    static constexpr uint8_t REG_DEVICE_ID = 0x0B;

    // Bit positions in REG_POWER_ON_CFG.
    static constexpr uint8_t POWER_ON_CFG_CHG_DISABLE = 0x08; // bit 3: 1 = charging disabled
    // Bit positions in REG_MISC_OP_CONTROL.
    static constexpr uint8_t MISC_OP_SHIPPING_MODE = 0x20; // bit 5: 1 = enter shipping mode
    // Bit positions in REG_SYSTEM_STATUS.
    static constexpr uint8_t SYS_STATUS_CHRG_SHIFT = 3;
    static constexpr uint8_t SYS_STATUS_CHRG_MASK = 0x03;
    static constexpr uint8_t SYS_STATUS_PG = 0x02;        // bit 1: input power good
    static constexpr uint8_t SYS_STATUS_THERM_REG = 0x01; // bit 0: thermal regulation

    static constexpr uint8_t DEVICE_ID_EXPECTED = 0x04;
};

extern SGM41562 *sgm41562;

// Lazy-instantiate the global on the supplied wire. Returns true on success.
bool initSGM41562(TwoWire &wire);

#endif // HAS_SGM41562
