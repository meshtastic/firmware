#if RADIOLIB_EXCLUDE_SX126X != 1
#include "SX126xInterface.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"
#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif
#if defined(ARCH_ESP32)
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#endif

#include "Throttle.h"
#include "UptimeClock.h"
#ifdef SX126X_RX_REARM_AT_TX_DONE
#include "SPILock.h"
#endif
#ifdef SX126X_STATE_SAMPLER_MS
#include "concurrency/OSThread.h"
#include <functional>
#endif

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not
// specified (may be dangerous if using external PA and SX126x power config forgotten)
#if ARCH_PORTDUINO
#define SX126X_MAX_POWER portduino_config.sx126x_max_power
#endif
#ifndef SX126X_MAX_POWER
#define SX126X_MAX_POWER 22
#endif

template <typename T>
SX126xInterface<T>::SX126xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_DEBUG("SX126xInterface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
#if defined(SX126X_STATE_SAMPLER_MS) || defined(SX126X_RX_REARM_AT_TX_DONE)
    rawCs = cs;
#endif
#ifdef SX126X_RX_REARM_AT_TX_DONE
    isrHal = hal;
#endif
#ifdef SX126X_STATE_SAMPLER_TASK
    samplerHal = hal;
#endif
}

#ifdef SX126X_STATE_SAMPLER_TASK
#if !defined(SX126X_STATE_SAMPLER_MS) || !defined(ARCH_NRF52)
#error "SX126X_STATE_SAMPLER_TASK is a bench flag for nRF52, and needs SX126X_STATE_SAMPLER_MS for its period"
#endif
// The task looks every SX126X_STATE_SAMPLER_MS; the main loop only logs what it queued, so it need not run as often.
#define SX126X_STATE_SAMPLER_LOOP_MS 10
#elif defined(SX126X_STATE_SAMPLER_MS)
#define SX126X_STATE_SAMPLER_LOOP_MS SX126X_STATE_SAMPLER_MS
#endif

#ifdef SX126X_STATE_SAMPLER_MS
namespace
{
/** Bench: runs the chip state sample (or, with the task, its logging) on the main loop, between the other threads */
class ChipStateSampler : public concurrency::OSThread
{
  public:
    explicit ChipStateSampler(std::function<void()> sample)
        : concurrency::OSThread("ChipState", SX126X_STATE_SAMPLER_LOOP_MS), sample(std::move(sample))
    {
    }

  protected:
    int32_t runOnce() override
    {
        sample();
        return SX126X_STATE_SAMPLER_LOOP_MS;
    }

  private:
    std::function<void()> sample;
};

const char *chipModeName(uint8_t mode)
{
    switch (mode) {
    case 0x2:
        return "STBY_RC";
    case 0x3:
        return "STBY_XOSC";
    case 0x4:
        return "FS";
    case 0x5:
        return "RX";
    case 0x6:
        return "TX";
    case 0xB:
        return "BUSY";
    default:
        return "?";
    }
}
} // namespace

template <typename T> bool SX126xInterface<T>::readChipState(bool fromTask, uint8_t &mode, uint16_t &irq, uint8_t &status)
{
    // GetIrqStatus returns the status byte (the chip mode) and then the IRQ word; unlike ClearIrqStatus it changes nothing.
    // BUSY high means mid-command, waking or starting the TCXO: the chip would not answer, so report the mode as BUSY and
    // leave irq as the caller passed it.
    uint8_t out[4] = {RADIOLIB_SX126X_CMD_GET_IRQ_STATUS, RADIOLIB_SX126X_CMD_NOP, RADIOLIB_SX126X_CMD_NOP,
                      RADIOLIB_SX126X_CMD_NOP};
    uint8_t in[4] = {0, 0, 0, 0};
    bool busy;
#ifdef SX126X_STATE_SAMPLER_TASK
    if (fromTask) {
        // Never wait for the lock: a look that holds up the radio would change what it measures.
        if (!spiLock->lock(0)) {
            chipStateLockBusy = chipStateLockBusy + 1;
            return false;
        }
        busy = samplerHal->digitalRead(module.getGpio());
        if (!busy) {
            samplerHal->ArduinoHal::spiBeginTransaction();
            samplerHal->digitalWrite(rawCs, samplerHal->GpioLevelLow);
            samplerHal->spiTransfer(out, sizeof(out), in);
            samplerHal->digitalWrite(rawCs, samplerHal->GpioLevelHigh);
            samplerHal->ArduinoHal::spiEndTransaction();
        }
        spiLock->unlock();
    } else
#endif
    {
        (void)fromTask;
        busy = module.hal->digitalRead(module.getGpio());
        if (!busy) {
            module.hal->spiBeginTransaction();
            module.hal->digitalWrite(rawCs, module.hal->GpioLevelLow);
            module.hal->spiTransfer(out, sizeof(out), in);
            module.hal->digitalWrite(rawCs, module.hal->GpioLevelHigh);
            module.hal->spiEndTransaction();
        }
    }
    if (busy) {
        mode = 0xB;
        status = 0;
        return true;
    }
    status = in[1];
    mode = (status >> 4) & 0x7;
    irq = ((uint16_t)in[2] << 8) | in[3];
    return true;
}

#ifdef SX126X_STATE_SAMPLER_TASK
template <typename T> void SX126xInterface<T>::chipStateTaskMain(void *arg)
{
    auto *self = static_cast<SX126xInterface<T> *>(arg);
    const TickType_t period = pdMS_TO_TICKS(SX126X_STATE_SAMPLER_MS) ? pdMS_TO_TICKS(SX126X_STATE_SAMPLER_MS) : 1;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, period);
        if (xTaskGetTickCount() - wake >= period) // woke a whole period late: something above us ran
            self->chipStateTaskLate = self->chipStateTaskLate + 1;
        self->sampleChipStateFromTask();
    }
}

template <typename T> void SX126xInterface<T>::sampleChipStateFromTask()
{
    static uint8_t lastMode = 0xFF;
    static uint16_t lastIrq = 0xFFFF;
    uint8_t mode = 0xB, status = 0;
    uint16_t irq = lastIrq;
    if (rawCs == RADIOLIB_NC || !readChipState(true, mode, irq, status))
        return; // the SPI lock was held: no look this tick
    if (mode == lastMode && irq == lastIrq)
        return;
    lastMode = mode;
    lastIrq = irq;
    const uint8_t head = chipStateHead;
    const uint8_t next = (uint8_t)((head + 1) % chipStateRingSize);
    if (next == chipStateTail) {
        chipStateDropped = chipStateDropped + 1;
        return;
    }
    chipStateRing[head] = {millis(), irq, mode, status};
    __asm__ __volatile__("" ::: "memory"); // the entry is written before the head that publishes it
    chipStateHead = next;
}
#endif

template <typename T> void SX126xInterface<T>::sampleChipState()
{
    const uint32_t now = millis();
    // This runs on the main loop, so a late run is a span in which nothing ran there, the RX handler included.
    if (lastSampleMs && now - lastSampleMs > 2 * SX126X_STATE_SAMPLER_LOOP_MS)
        LOG_DEBUG("chip state: sampler late, %u ms since the last look", (unsigned)(now - lastSampleMs));
    lastSampleMs = now;
    if (rawCs == RADIOLIB_NC)
        return;

#ifdef SX126X_STATE_SAMPLER_TASK
    // The task did the looking; this only logs what it queued, each change with the time the task saw it.
    while (chipStateTail != chipStateHead) {
        __asm__ __volatile__("" ::: "memory"); // read the entry only after seeing the head that published it
        const ChipStateEvent e = chipStateRing[chipStateTail];
        __asm__ __volatile__("" ::: "memory"); // and free its slot only after reading it
        chipStateTail = (uint8_t)((chipStateTail + 1) % chipStateRingSize);
        LOG_DEBUG("chip state @%u: %s irq 0x%03x, was %s irq 0x%03x, status 0x%02x", (unsigned)e.ms, chipModeName(e.mode), e.irq,
                  chipModeName(sampledMode), sampledIrq, e.status);
        sampledMode = e.mode;
        sampledIrq = e.irq;
    }
    // Drops and late ticks log at once; lock skips are routine, so they only refresh the line every 10 s.
    static uint32_t loggedDropped = 0, loggedLate = 0, loggedLockBusy = 0, loggedCountersMs = 0;
    if (chipStateDropped != loggedDropped || chipStateTaskLate != loggedLate ||
        (chipStateLockBusy != loggedLockBusy && now - loggedCountersMs >= 10000)) {
        loggedDropped = chipStateDropped;
        loggedLate = chipStateTaskLate;
        loggedLockBusy = chipStateLockBusy;
        loggedCountersMs = now;
        LOG_DEBUG("chip state task: %u changes dropped, %u late ticks, %u looks skipped for the SPI lock",
                  (unsigned)loggedDropped, (unsigned)loggedLate, (unsigned)loggedLockBusy);
    }
#else
    uint8_t mode = 0xB;
    uint8_t status = 0;
    uint16_t irq = sampledIrq;
    readChipState(false, mode, irq, status);
    if (mode == sampledMode && irq == sampledIrq)
        return;
    LOG_DEBUG("chip state: %s irq 0x%03x, was %s irq 0x%03x, status 0x%02x", chipModeName(mode), irq, chipModeName(sampledMode),
              sampledIrq, status);
    sampledMode = mode;
    sampledIrq = irq;
#endif
}
#endif

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template <typename T> bool SX126xInterface<T>::init()
{

// Typically, the RF switch on SX126x boards is controlled by two signals, which are negations of each other (switched RFIO
// paths). The negation is usually performed in hardware, or (suboptimal design) TXEN and RXEN are the two inputs to this style of
// RF switch. On some boards, there is no hardware negation between CTRL and ¬CTRL, but CTRL is internally connected to DIO2, and
// DIO2's switching is done by the SX126X itself, so the MCU can't control ¬CTRL at exactly the same time. One solution would be
// to set ¬CTRL as SX126X_TXEN or SX126X_RXEN, but they may already be used for another purpose, such as controlling another
// PA/LNA. Keeping ¬CTRL high seems to work, as long CTRL=1, ¬CTRL=1 has the opposite and stable RF path effect as CTRL=0 and
// ¬CTRL=1, this depends on the RF switch, but it seems this usually works. Better hardware design, which is done most the time,
// means this workaround is not necessary.
#ifdef SX126X_ANT_SW // Perhaps add RADIOLIB_NC check, and beforehand define as such if it is undefined, but it is not commonly
                     // used and not part of the 'default' set of pin definitions.
    digitalWrite(SX126X_ANT_SW, HIGH);
    pinMode(SX126X_ANT_SW, OUTPUT);
#endif

#ifdef SX126X_POWER_EN // Perhaps add RADIOLIB_NC check, and beforehand define as such if it is undefined, but it is not commonly
                       // used and not part of the 'default' set of pin definitions.
    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
#endif

#if HAS_LORA_FEM
    loraFEMInterface.init();
    // Apply saved FEM LNA mode from config
    if (loraFEMInterface.isLnaCanControl()) {
        loraFEMInterface.setLNAEnable(config.lora.fem_lna_mode != meshtastic_Config_LoRaConfig_FEM_LNA_Mode_DISABLED);
    }
#endif

#ifdef RF95_FAN_EN
    pinMode(RF95_FAN_EN, OUTPUT);
    digitalWrite(RF95_FAN_EN, HIGH);
#endif

#if ARCH_PORTDUINO
    // An explicit Vref wins; probing with none given tries the radio default first.
    bool tcxoVoltageExplicit = portduino_config.dio3_tcxo_voltage > 0;
    if (tcxoVoltageExplicit)
        tcxoVoltage = (float)portduino_config.dio3_tcxo_voltage / 1000;
    else if (TCXO_OPTIONAL_ENABLED)
        tcxoVoltage = TCXO_OPTIONAL_DEFAULT_VOLTAGE;
    else
        tcxoVoltage = 0;
    if (portduino_config.lora_sx126x_ant_sw_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_sx126x_ant_sw_pin.pin, HIGH);
        pinMode(portduino_config.lora_sx126x_ant_sw_pin.pin, OUTPUT);
    }
    // The knob here is the YAML key, not the variant define the other branch reports.
    if (tcxoVoltage == 0.0)
        LOG_DEBUG("Lora.DIO3_TCXO_VOLTAGE not set, DIO3 not used as TCXO Vref");
    else if (!tcxoVoltageExplicit)
        LOG_DEBUG("TCXO_OPTIONAL: no Vref configured, probing default TCXO Vref %f V on DIO3", tcxoVoltage);
    else
        LOG_DEBUG("Lora.DIO3_TCXO_VOLTAGE set, DIO3 as TCXO Vref %f V", tcxoVoltage);
#else
    if (tcxoVoltage == 0.0)
        LOG_DEBUG("SX126X_DIO3_TCXO_VOLTAGE not defined, DIO3 not used as TCXO Vref");
    else
        LOG_DEBUG("SX126X_DIO3_TCXO_VOLTAGE defined, DIO3 as TCXO Vref %f V", tcxoVoltage);
#endif
    setTransmitEnable(false);

    RadioLibInterface::init();

    if (!reinitChip())
        return false;

    startReceive(); // start receiving

#ifdef SX126X_STATE_SAMPLER_MS
    static ChipStateSampler *chipStateSampler = nullptr; // init() runs once per radio; never start a second sampler
    if (!chipStateSampler)
        chipStateSampler = new ChipStateSampler([this]() { sampleChipState(); });
#endif
#ifdef SX126X_STATE_SAMPLER_TASK
    // Above the Arduino loop task, so a main-loop hold cannot delay a look.
    static bool chipStateTaskStarted = false;
    if (!chipStateTaskStarted) {
        chipStateTaskStarted = xTaskCreate(chipStateTaskMain, "ChipState", 256, this, tskIDLE_PRIORITY + 2, nullptr) == pdPASS;
        LOG_INFO("Chip state sampler task %s, every %u ms", chipStateTaskStarted ? "started" : "not started",
                 (unsigned)SX126X_STATE_SAMPLER_MS);
    }
#endif

    return true;
}

// begin() and the chip-side setup that a reset chip loses: begin() hardware-resets the chip, then
// PA ramp, OCP limit, DIO2-as-RF-switch, RF switch pins, RX gain, the 0x8B5 RX patch, and CRC are
// reprogrammed. Shared by init() and by reconfigure()'s recovery path.
template <typename T> bool SX126xInterface<T>::reinitChip()
{
    // Clamp here, not just in programModemParams(): applyModemConfig() resets `power` to the raw
    // config value, and the recovery path reaches begin() without passing through the params clamp
    limitPower(SX126X_MAX_POWER);
    // Make sure we reach the minimum power supported to turn the chip on (-9dBm)
    if (power < -9)
        power = -9;

    // FIXME: May want to set depending on a definition, currently all SX126x variant files use the DC-DC regulator option
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);

    // Chip answered but would not start on the TCXO: retry on the XTAL. CHIP_NOT_FOUND is a
    // wiring or SPI fault, where a second attempt only hides it. Portduino only - an embedded
    // board gets this from the second interface instance in initLoRa()'s ladder.
    // TODO: consider deferring to RadioLib, which has autocorrected this itself since 7.5.0:
    // SX126x::modSetup() retries config() on the XTAL when begin() fails with SPI_CMD_FAILED and
    // XOSC_START_ERR, so the ordinary "TCXO configured, XTAL fitted" case never reaches here and
    // what does is mostly invalid settings. Narrowing this to SPI_CMD_TIMEOUT - the oscillator
    // symptom RadioLib's condition misses - would keep the cover and drop the misdiagnosis.
#if ARCH_PORTDUINO
    if (TCXO_OPTIONAL_ENABLED && res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
        LOG_WARN("SX126x init failed with TCXO Vref %f V (err %d), retrying without TCXO", tcxoVoltage, res);
        tcxoVoltage = 0;
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);
        if (res == RADIOLIB_ERR_NONE)
            LOG_INFO("SX126x init success without TCXO (XTAL mode)");
    }
#endif

#ifdef SX126X_PA_RAMP_US
    // Set custom PA ramp time for boards requiring longer stabilization (e.g., T-Beam 1W needs >800us)
    if (res == RADIOLIB_ERR_NONE) {
        lora.setPaRampTime(SX126X_PA_RAMP_US);
    }
#endif
#ifdef ARCH_PORTDUINO
    // Bench: MESHTASTIC_TCXO_DELAY_US reprograms the DIO3 TCXO start-up, which begin() leaves at RadioLib's 5000 us.
    // A clear CAD drops the chip to STDBY_RC, so every TX after one waits this long before the PA ramps.
    if (res == RADIOLIB_ERR_NONE && irqPolledOverUsb() && tcxoVoltage > 0) {
        const char *delayEnv = getenv("MESHTASTIC_TCXO_DELAY_US");
        if (delayEnv && *delayEnv) {
            char *end = nullptr;
            const long delayUs = strtol(delayEnv, &end, 10);
            if (*end != '\0' || delayUs < 0 || delayUs > 10000) {
                LOG_WARN("Ignoring MESHTASTIC_TCXO_DELAY_US=%s, keeping 5000 us", delayEnv);
            } else {
                const int16_t tcxoErr = lora.setTCXO(tcxoVoltage, (uint32_t)delayUs);
                LOG_INFO("TCXO start-up delay %ld us %s%d", delayUs, radioLibErr, tcxoErr);
            }
        } else {
            LOG_INFO("TCXO start-up delay 5000 us (default)");
        }
    }
#elif defined(SX126X_TCXO_DELAY_US)
    // Bench: the build-time counterpart of MESHTASTIC_TCXO_DELAY_US, for an embedded board.
    if (res == RADIOLIB_ERR_NONE && tcxoVoltage > 0) {
        const int16_t tcxoErr = lora.setTCXO(tcxoVoltage, (uint32_t)(SX126X_TCXO_DELAY_US));
        LOG_INFO("TCXO start-up delay %u us %s%d", (unsigned)(SX126X_TCXO_DELAY_US), radioLibErr, tcxoErr);
    }
#endif
    // Keep the oscillator running in standby: leaving STDBY_RC restarts a DIO3 TCXO, and BUSY stays high for its
    // 5 ms start-up on every SET_RX, SET_CAD and SET_TX. Also sets the RX/TX fallback mode to STDBY_XOSC.
    // Always on a CH341 host; -DSX126X_STANDBY_XOSC turns it on for an embedded board (bench).
#ifdef SX126X_STANDBY_XOSC
    constexpr bool standbyXoscBuild = true;
#else
    constexpr bool standbyXoscBuild = false;
#endif
    if (res == RADIOLIB_ERR_NONE && (standbyXoscBuild || irqPolledOverUsb())) {
        const int16_t xoscErr = lora.setStandbyXOSC(true);
        LOG_INFO("SX126x standby set to XOSC %s%d", radioLibErr, xoscErr);
    }
#ifdef ARCH_PORTDUINO
    if (irqPolledOverUsb()) {
        const char *pollUs = getenv("PINEDIO_POLL_INTERVAL_US");
        LOG_INFO("CH341 pin poll interval %s us", pollUs && *pollUs ? pollUs : "33000 (default)");
        const char *prestage = getenv("MESHTASTIC_TX_PRESTAGE");
        txPrestageEnabled = prestage && prestage[0] == '1' && prestage[1] == '\0';
        LOG_INFO("CH341 TX prestage %s", txPrestageEnabled ? "on" : "off");
    }
#endif
#ifdef SX126X_TX_PRESTAGE
    txPrestageEnabled = true;
    LOG_INFO("SX126x TX prestage on (build flag)");
#elif defined(SX126X_TX_LAUNCH_TRACE)
    LOG_INFO("SX126x TX launch trace on, prestage off (build flag)");
#endif
#ifdef SX126X_TX_LAUNCH_OVERRIDE
    txStagedByRadioLib = false; // begin() reset the chip, and the sensitivity fix with it
#endif
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_INFO("SX126x init result %d", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || res == RADIOLIB_ERR_SPI_CMD_FAILED)
        return false;

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

    // Overriding current limit
    // (https://github.com/jgromes/RadioLib/blob/690a050ebb46e6097c5d00c371e961c1caa3b52e/src/modules/SX126x/SX126x.cpp#L85) using
    // value in SX126xInterface.h (currently 140 mA) It may or may not be necessary, depending on how RadioLib functions, from
    // SX1261/2 datasheet: OCP after setting DeviceSel with SetPaConfig(): SX1261 - 60 mA, SX1262 - 140 mA For the SX1268 the IC
    // defaults to 140mA no matter the set power level, but RadioLib set it lower, this would need further checking Default values
    // are: SX1262, SX1268: 0x38 (140 mA), SX1261: 0x18 (60 mA)
    // FIXME: Not ideal to increase SX1261 current limit above 60mA as it can only transmit max 15dBm, should probably only do it
    // if using SX1262 or SX1268
    res = lora.setCurrentLimit(currentLimit);
    LOG_DEBUG("Current limit set to %f", currentLimit);
    LOG_DEBUG("Current limit set result %d", res);

    if (res == RADIOLIB_ERR_NONE) {
#ifdef SX126X_DIO2_AS_RF_SWITCH
        bool dio2AsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
        bool dio2AsRfSwitch = false;
        if (portduino_config.dio2_as_rf_switch) {
            dio2AsRfSwitch = true;
        }
#else
        bool dio2AsRfSwitch = false;
#endif
        res = lora.setDio2AsRfSwitch(dio2AsRfSwitch);
        LOG_DEBUG("Set DIO2 as %sRF switch, result: %d", dio2AsRfSwitch ? "" : "not ", res);
    }

// If a pin isn't defined, we set it to RADIOLIB_NC, it is safe to always do external RF switching with RADIOLIB_NC as it has
// no effect
#if ARCH_PORTDUINO
    if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("Use MCU pin %i as RXEN, pin %i as TXEN for RF switching", portduino_config.lora_rxen_pin.pin,
                  portduino_config.lora_txen_pin.pin);
        lora.setRfSwitchPins(portduino_config.lora_rxen_pin.pin, portduino_config.lora_txen_pin.pin);
    }
#else
#ifndef SX126X_RXEN
#define SX126X_RXEN RADIOLIB_NC
    LOG_DEBUG("SX126X_RXEN not defined, default RADIOLIB_NC");
#endif
#ifndef SX126X_TXEN
#define SX126X_TXEN RADIOLIB_NC
    LOG_DEBUG("SX126X_TXEN not defined, default RADIOLIB_NC");
#endif
    if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("Use MCU pin %i as RXEN, pin %i as TXEN for RF switching", SX126X_RXEN, SX126X_TXEN);
        lora.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
    }
#endif
    if (config.lora.sx126x_rx_boosted_gain) {
        uint16_t result = lora.setRxBoostedGainMode(true);
        LOG_INFO("Set RX gain to boosted mode; result: %d", result);
    } else {
        uint16_t result = lora.setRxBoostedGainMode(false);
        LOG_INFO("Set RX gain to power saving mode; result: %d", result);
    }

    // Undocumented SX1262 register patch recommended by Heltec/Semtech for improved RX sensitivity.
    // Sets bit 0 of register 0x8B5.
    if (module.SPIsetRegValue(0x8B5, 0x01, 0, 0) == RADIOLIB_ERR_NONE) {
        LOG_INFO("Applied SX1262 reg 0x8B5 RX patch");
    } else {
        LOG_WARN("Can't apply SX1262 reg 0x8B5 RX patch");
    }

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(RADIOLIB_SX126X_LORA_CRC_ON);

#ifdef SX126X_NO_POWER_OPTIMIZATION_TABLE
    // begin() applied the optimization table; re-apply the fixed PA config.
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setOutputPower(power, false);
#endif

    if (res != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX126x re-init failed %s%d", radioLibErr, res);
    return res == RADIOLIB_ERR_NONE;
}

template <typename T> int16_t SX126xInterface<T>::programModemParams()
{
    // configure publicly accessible settings
    int16_t err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setSpreadingFactor(%u) %s%d", sf, radioLibErr, err);
        return err;
    }

    err = lora.setBandwidth(bw);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setBandwidth(%.1f) %s%d", bw, radioLibErr, err);
        return err;
    }

    err = lora.setCodingRate(cr);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setCodingRate(%u) %s%d", cr, radioLibErr, err);
        return err;
    }

    err = lora.setSyncWord(syncWord);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setSyncWord %s%d", radioLibErr, err);
        return err;
    }

    err = lora.setCurrentLimit(currentLimit);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setCurrentLimit %s%d", radioLibErr, err);
        return err;
    }

    err = lora.setPreambleLength(preambleLength);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setPreambleLength(%u) %s%d", preambleLength, radioLibErr, err);
        return err;
    }

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X setFrequency(%.3f) %s%d", getFreq(), radioLibErr, err);
        return err;
    }

    limitPower(SX126X_MAX_POWER);
    // Make sure we reach the minimum power supported to turn the chip on (-9dBm)
    if (power < -9)
        power = -9;

#ifdef SX126X_NO_POWER_OPTIMIZATION_TABLE
    err = lora.setOutputPower(power, false); // external PA: fixed PA config
#else
    err = lora.setOutputPower(power);
#endif
    if (err != RADIOLIB_ERR_NONE) {
        // Don't abort: this power is operator config (tx_power/SX126X_MAX_POWER); a value above the
        // driver's max would crash the daemon before reloadConfig() persists. Flag it and keep prior power.
        LOG_ERROR("SX126X setOutputPower %d dBm rejected (%s%d); keep previous Tx power", power, radioLibErr, err);
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
    }

    // Apply RX gain mode - valid in STDBY (datasheet §9.6), matches resetAGC() pattern
    err = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);
    if (err != RADIOLIB_ERR_NONE)
        LOG_WARN("SX126X setRxBoostedGainMode %s%d", radioLibErr, err);

    return RADIOLIB_ERR_NONE;
}

template <typename T> bool SX126xInterface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();

    // set mode to standby - a chip that lost its state to a reset/brownout can time out here (-707),
    // so don't let setStandby()'s assert fire before the recovery below gets a chance
    int16_t err = trySetStandby();
    if (err == RADIOLIB_ERR_NONE)
        err = programModemParams();

    if (err != RADIOLIB_ERR_NONE) {
        // Chip likely lost its state (reset/brownout); recover in place rather than crash - see
        // RadioLibInterface::recoverChipStateLoss().
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
        LOG_ERROR("SX126x rejected modem params, chip state lost? Full re-init");
        if (!reinitChip() || (err = programModemParams()) != RADIOLIB_ERR_NONE) {
            LOG_ERROR("SX126x unrecoverable %s%d, radio down until reboot", radioLibErr, err);
            return false;
        }
        LOG_INFO("SX126x recovered after re-init");
    }

    startReceive(); // restart receiving

    return true;
}

template <typename T> int16_t SX126xInterface<T>::getCurrentRSSI()
{
    float rssi = lora.getRSSI(false);
    return (int16_t)round(rssi);
}

template <typename T> void SX126xInterface<T>::setRadioIsr(void (*callback)())
{
#ifdef LORA_DIO1_SOFTWARE_POLL
    irqPollingActive = true;
    pollTxMode = isIsrTxCallback(callback);
    scheduleIrqPollTick();
#else
    lora.setDio1Action(callback);
#endif
}

template <typename T> void SX126xInterface<T>::clearRadioIsr()
{
#ifdef LORA_DIO1_SOFTWARE_POLL
    irqPollingActive = false;
#else
    lora.clearDio1Action();
#endif
}

#ifdef LORA_DIO1_SOFTWARE_POLL
template <typename T> void SX126xInterface<T>::handleSoftwareLoraIrqPoll()
{
    if (!irqPollingActive)
        return;

    // getIrqFlags()/clearIrqFlags() both operate on the raw SX126x IRQ register, so use the
    // chip-specific RADIOLIB_SX126X_IRQ_* masks on both the read and the clear.
    uint16_t irq = lora.getIrqFlags();
    const uint16_t rxEventMask =
        RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT | RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR;
    const uint16_t noisyRxMask = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID;

    // Do NOT treat a preamble/header-only IRQ as a full RX event: noisy preamble detections would
    // repeatedly trigger readData() and starve TX scheduling. Clear these non-terminal bits, or the
    // poll loop spins at high rate while they stay latched.
    if (!pollTxMode && (irq & noisyRxMask) && ((irq & ~noisyRxMask) == 0U)) {
        // Record the look first: it clears PREAMBLE, and the TX path must still see a header this clear hides.
        receiveDetected(irq, RADIOLIB_SX126X_IRQ_HEADER_VALID, RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
        if (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID)
            lora.clearIrqFlags(RADIOLIB_SX126X_IRQ_HEADER_VALID);
        scheduleIrqPollTick();
        return;
    }

    if (pollTxMode) {
        if (irq & (RADIOLIB_SX126X_IRQ_TX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT)) {
            deliverPendingIrqFromPoll(ISR_TX);
            return;
        }
    } else if (irq & rxEventMask) {
        deliverPendingIrqFromPoll(ISR_RX);
        return;
    }

    scheduleIrqPollTick();
}
#endif

template <typename T> int16_t SX126xInterface<T>::trySetStandby()
{
    const uint32_t t0 = millis();
    checkNotification(); // handle any pending interrupts before we force standby
    const uint32_t tNotify = millis();

    int16_t err = lora.standby();
    if (err == RADIOLIB_ERR_SPI_CMD_TIMEOUT) {
        // After a bounded RX times out, the status byte returned with SET_STANDBY still carries that timeout, and
        // RadioLib reports it as a failed command although the chip took it. The next command sees a fresh status.
        err = lora.standby();
        LOG_DEBUG("SX126x standby reported a stale command timeout, retry %s%d", radioLibErr, err);
    }
    const uint32_t tCmd = millis();

    if (err != RADIOLIB_ERR_NONE)
        LOG_DEBUG("SX126x standby %s%d", radioLibErr, err);
#ifdef ARCH_PORTDUINO
    if (err != RADIOLIB_ERR_NONE)
        portduino_status.LoRa_in_error = true;
#endif
    isReceiving = false; // If we were receiving, not any more
    rxArmedContinuous = false;
    rxSighting.reset();
    const uint32_t tDetach = millis();
    disableInterrupt();
    lastStandbySteps = {tNotify - t0, tCmd - tNotify, millis() - tDetach};
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
    return err;
}

template <typename T> void SX126xInterface<T>::setStandby()
{
    int16_t err = trySetStandby();
#ifdef ARCH_PORTDUINO
    (void)err;
#else
    assert(err == RADIOLIB_ERR_NONE);
#endif
}

/**
 * Add SNR data to received messages
 */
template <typename T> void SX126xInterface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
    mp->has_rx_rssi = true; // rx_rssi has explicit presence - a genuine reading must be marked present to survive encoding
    LOG_TRACE("Corrected frequency offset: %f", lora.getFrequencyError());
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void SX126xInterface<T>::configHardwareForSend()
{
    setTransmitEnable(true);
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void SX126xInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setTransmitEnable(false);

    // Continuous RX on a CH341 host too: nothing to save there, and only a known-continuous RX can be resumed
    // after RX_DONE (resumeRunningReceive()) instead of restarted over the slow bus.
#ifdef SX126X_RESUME_CONTINUOUS_RX
    // Bench flag: the same resume on MCU boards. Costs nothing on presets where the duty cycle falls back to
    // continuous anyway (SHORT_FAST's 16-symbol preamble against 8 wake symbols leaves no sleep).
    const bool continuousRx = true;
#else
    const bool continuousRx = irqPolledOverUsb();
#endif
#ifdef ARCH_PORTDUINO_WASM
    const char *rxMethod = "startReceive";
#else
    const char *rxMethod = continuousRx ? "startReceive" : "startReceiveDutyCycleAuto";
#endif
    auto tryStartRx = [&]() -> int16_t {
#ifdef ARCH_PORTDUINO_WASM
        // Continuous RX in the browser: duty-cycle sleep parks BUSY high between RX
        // windows and stalls the slow WebUSB SPI link. No battery to save here.
        return lora.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS);
#else
        if (continuousRx)
            return lora.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS);
        // We use a 16 bit preamble so this should save some power by letting radio sit in standby mostly.
        return lora.startReceiveDutyCycleAuto(preambleLength, 8, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS);
#endif
    };

    const uint32_t tStandby = millis();
    int16_t err = trySetStandby();
    const uint32_t tStartRx = millis();
    if (err == RADIOLIB_ERR_NONE)
        err = tryStartRx();
    const uint32_t tStartRxEnd = millis();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("SX126X %s %s%d", rxMethod, radioLibErr, err);
        if (maybeRecoverChipStateLoss())
            err = tryStartRx();
    }

    if (err != RADIOLIB_ERR_NONE) {
#ifdef ARCH_PORTDUINO
        portduino_status.LoRa_in_error = true;
#else
        // No assert: leave RX off rather than reboot; periodicRadioMaintenance() re-arms it, throttled
        LOG_ERROR("SX126X RX offline %s%d", radioLibErr, err);
        rxOffline = true;
        return;
#endif
    }

    const uint32_t tArm = millis();
    RadioLibInterface::startReceive();
    rxArmedContinuous = continuousRx;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
    lastRxArmSteps = {tStartRx - tStandby, lastStandbySteps.cmdMs, tStartRxEnd - tStartRx, millis() - tArm};
#endif
}

template <typename T> bool SX126xInterface<T>::resumeRunningReceive()
{
    // Continuous RX survives RX_DONE and CRC or header errors: the chip is still listening. A restart is a
    // standby and ~10 bus transactions, deaf throughout on a CH341 host, so pick the RX back up instead.
    if (!rxArmedContinuous)
        return false;
    // readData() clears these, but handleReceiveInterrupt()'s early outs do not, and a latched one would hold
    // DIO1 high past the re-arm. PREAMBLE/HEADER_VALID stay: they may belong to the next frame, already arriving.
    lora.clearIrqFlags(RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR |
                       RADIOLIB_SX126X_IRQ_TIMEOUT);
    if (deafSinceMs) {
        LOG_TRACE("RX still running, re-arm skipped after %s, readout %u ms", deafFor,
                  (unsigned)(Time::getMillis() - deafSinceMs));
        deafSinceMs = 0; // the chip never stopped listening, so there is no deaf window to report
    }
    rxSighting.reset(); // RX_DONE ends the frame's hold, as the standby it replaces would
    RadioLibInterface::startReceive();
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that beat the arm
    return true;
}

#ifdef SX126X_RX_REARM_AT_TX_DONE
#if !defined(ARCH_NRF52)
#error "SX126X_RX_REARM_AT_TX_DONE is a bench flag for nRF52 only: it drives SPI from the DIO1 interrupt"
#endif
// Test values, not definedness: init() above defines both pins as RADIOLIB_NC when a variant leaves them out.
#if (defined(SX126X_TXEN) && (SX126X_TXEN) != RADIOLIB_NC) || (defined(SX126X_RXEN) && (SX126X_RXEN) != RADIOLIB_NC) ||          \
    HAS_LORA_FEM
#error "SX126X_RX_REARM_AT_TX_DONE needs a board with no CPU-driven RF switch: the ISR does not drive TX/RX enable pins"
#endif

template <typename T>
typename SX126xInterface<T>::RearmOutcome SX126xInterface<T>::rawCommandFromIsr(const uint8_t *cmd, size_t len)
{
    uint8_t out[rawCommandMax];
    uint8_t in[rawCommandMax];
    if (len > sizeof(out))
        return REARM_BAD_COMMAND;
    // The chip holds BUSY for microseconds after each command. Bounded: millis() does not advance in an ISR.
    for (unsigned i = 0; module.hal->digitalRead(module.getGpio()); i++) {
        if (i >= 200)
            return REARM_CHIP_BUSY;
        delayMicroseconds(1);
    }
    memcpy(out, cmd, len);
    isrHal->ArduinoHal::spiBeginTransaction(); // the base class's: the lock is already held
    isrHal->digitalWrite(rawCs, isrHal->GpioLevelLow);
    isrHal->spiTransfer(out, len, in);
    isrHal->digitalWrite(rawCs, isrHal->GpioLevelHigh);
    isrHal->ArduinoHal::spiEndTransaction();
    return REARM_ARMED;
}

/// After TX_DONE the chip sits in standby until the RadioIf thread re-arms it, and a main-loop hold can make that
/// hundreds of ms. So re-arm here, with what startReceive() would program: the RX IRQ set with RX_DONE on DIO1, the
/// flags cleared, the RX packet length, and a continuous RX. Skipped if the SPI lock or the chip is busy.
template <typename T> bool SX126xInterface<T>::rearmReceiveFromIsr()
{
    if (rawCs == RADIOLIB_NC || !isrHal)
        return false;
    if (!spiLock->tryLockFromISR()) {
        rearmOutcome = REARM_SPI_BUSY;
        return false;
    }
    const uint16_t irqMask = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT | RADIOLIB_SX126X_IRQ_CRC_ERR |
                             RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_HEADER_ERR |
                             RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
    const uint16_t dio1Mask = RADIOLIB_SX126X_IRQ_RX_DONE;
    const uint8_t setDioIrq[] = {RADIOLIB_SX126X_CMD_SET_DIO_IRQ_PARAMS,
                                 (uint8_t)(irqMask >> 8),
                                 (uint8_t)(irqMask & 0xFF),
                                 (uint8_t)(dio1Mask >> 8),
                                 (uint8_t)(dio1Mask & 0xFF),
                                 0,
                                 0,
                                 0,
                                 0};
    const uint8_t clearIrq[] = {RADIOLIB_SX126X_CMD_CLEAR_IRQ_STATUS, (uint8_t)(RADIOLIB_SX126X_IRQ_ALL >> 8),
                                (uint8_t)(RADIOLIB_SX126X_IRQ_ALL & 0xFF)};
    // As configured in programModemParams(): explicit header, CRC on, standard IQ. 255 is the RX length RadioLib uses.
    const uint8_t packetParams[] = {RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS, (uint8_t)(preambleLength >> 8),
                                    (uint8_t)(preambleLength & 0xFF),      RADIOLIB_SX126X_LORA_HEADER_EXPLICIT,
                                    RADIOLIB_SX126X_MAX_PACKET_LENGTH,     RADIOLIB_SX126X_LORA_CRC_ON,
                                    RADIOLIB_SX126X_LORA_IQ_STANDARD};
    const uint8_t setRx[] = {RADIOLIB_SX126X_CMD_SET_RX, 0xFF, 0xFF, 0xFF}; // continuous
    static_assert(sizeof(setDioIrq) <= rawCommandMax && sizeof(clearIrq) <= rawCommandMax &&
                      sizeof(packetParams) <= rawCommandMax && sizeof(setRx) <= rawCommandMax,
                  "rawCommandFromIsr() would reject a re-arm command");
    RearmOutcome outcome = rawCommandFromIsr(setDioIrq, sizeof(setDioIrq));
    if (outcome == REARM_ARMED)
        outcome = rawCommandFromIsr(clearIrq, sizeof(clearIrq));
    if (outcome == REARM_ARMED)
        outcome = rawCommandFromIsr(packetParams, sizeof(packetParams));
    if (outcome == REARM_ARMED)
        outcome = rawCommandFromIsr(setRx, sizeof(setRx));
    spiLock->unlockFromISR();
    if (outcome != REARM_ARMED) {
        rearmOutcome = outcome; // the thread's startReceive() redoes all of it
        return false;
    }
    rearmTicks = xTaskGetTickCountFromISR();
    rearmOutcome = REARM_ARMED;
    return true;
}

template <typename T> bool SX126xInterface<T>::adoptReceiveArmedFromIsr()
{
    const uint8_t outcome = rearmOutcome;
    rearmOutcome = REARM_NONE;
    if (outcome == REARM_SPI_BUSY || outcome == REARM_CHIP_BUSY) {
        LOG_TRACE("RX re-arm at TX_DONE skipped, %s busy", outcome == REARM_SPI_BUSY ? "SPI" : "chip");
        return false;
    }
    if (outcome == REARM_BAD_COMMAND) {
        LOG_ERROR("RX re-arm at TX_DONE skipped, command longer than %u bytes", (unsigned)rawCommandMax);
        return false;
    }
    if (outcome != REARM_ARMED)
        return false;
    const uint32_t heldMs = (uint32_t)(((uint64_t)(xTaskGetTickCount() - rearmTicks) * 1000) / configTICK_RATE_HZ);
    LOG_TRACE("Radio back in RX at TX_DONE, %u ms before the handler ran", (unsigned)heldMs);
    deafSinceMs = 0; // listening since the interrupt: no deaf window to report
    RadioLibInterface::startReceive();
#ifdef SX126X_RESUME_CONTINUOUS_RX
    rxArmedContinuous = true; // the interrupt armed SET_RX with no timeout
#endif
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that completed while the handler waited
    return true;
}
#endif

/** Is the channel currently active? */
template <typename T> bool SX126xInterface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel.
    // NOTE: symNum is the *encoded* SET_CAD_PARAMS value, not a raw count - RADIOLIB_SX126X_CAD_ON_4_SYMB
    // (== raw 2) runs the 4-symbol scan getCadSymbolCountSubGhz() reports; keep the two in step.
    // Exit CAD straight into RX on detection (GOTO_RX) with the RX IRQs already mapped, so the chip's
    // own CAD->RX transition delivers RX_DONE with no library call in between. irqFlags is
    // the status-enable set (CAD verdict + full RX set incl. PREAMBLE/HEADER_VALID for isActivelyReceiving);
    // irqMask is the DIO-trigger set - CAD verdict + terminal RX events only, NOT PREAMBLE/HEADER_VALID,
    // which would fire the ISR mid-frame and run readData() on an incomplete packet.
    const uint32_t cadIrqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS | MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS;
    const uint32_t cadIrqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK | (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT) |
                                (1UL << RADIOLIB_IRQ_CRC_ERR) | (1UL << RADIOLIB_IRQ_HEADER_ERR);
    // cadTimeout bounds the RX the chip enters on detection: one max-length airtime, in microseconds. A
    // detection that delivers nothing then expires to standby and raises TIMEOUT, which re-arms us.
    const RadioLibTime_t cadRxTimeoutUsec =
        (RadioLibTime_t)getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader), false) * 1000;
    ChannelScanConfig_t cfg = {.cad = {.symNum = RADIOLIB_SX126X_CAD_ON_4_SYMB,
                                       .detPeak = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
                                       .detMin = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
                                       .exitMode = RADIOLIB_SX126X_CAD_GOTO_RX,
                                       .timeout = cadRxTimeoutUsec,
                                       .irqFlags = cadIrqFlags,
                                       .irqMask = cadIrqMask}};
    // Each step is timed: on a USB-SPI host every command is a bus round trip, and a scan measured at a
    // median 27 ms swallows a 10.4 ms SHORT_FAST preamble. This is lora.scanChannel(cfg), unrolled.
#ifdef SX126X_TX_LAUNCH_OVERRIDE
    prestagedLen = 0; // only a clear verdict from this scan may launch what it stages
#endif
    const uint32_t t0 = millis();
    setTransmitEnable(false);
    const uint32_t tTxEn = millis();
    int16_t result = trySetStandby();
    const uint32_t tStandby = millis();
    uint32_t tPrestage = tStandby;
    if (result == RADIOLIB_ERR_NONE) {
#ifdef SX126X_TX_LAUNCH_OVERRIDE
        // Write the payload now, while nothing is listening anyway, rather than after the verdict. The CAD leaves
        // the buffer alone; a detection's RX may overwrite it, but then there is no TX and the next scan rewrites it.
        if (txPrestageEnabled && scanForTx && txStagedByRadioLib) {
            const size_t numbytes = encodeRadioBuffer(scanForTx);
            const uint8_t writeBuffer[] = {RADIOLIB_SX126X_CMD_WRITE_BUFFER, 0x00}; // offset 0, RadioLib's TX base
            if (module.SPIwriteStream(writeBuffer, sizeof(writeBuffer), (uint8_t *)&radioBuffer, numbytes) == RADIOLIB_ERR_NONE) {
                prestagedLen = numbytes;
                prestagedId = scanForTx->id;
            }
        }
        tPrestage = millis();
#endif
        result = lora.startChannelScan(cfg);
        const uint32_t tSetup = millis();
        uint32_t tWait = tSetup;
        unsigned polls = 0;
        if (result == RADIOLIB_ERR_NONE) {
            // What scanChannel() does between the two calls: wait for DIO1 to report the CAD finished.
            while (!module.hal->digitalRead(module.getIrq())) {
                polls++;
                module.hal->yield();
            }
            tWait = millis();
            result = lora.getChannelScanResult();
        }
#ifdef SX126X_TX_LAUNCH_OVERRIDE
        cadVerdictMs = millis();
#endif
        LOG_TRACE("Channel scan steps: txen %u, standby %u, setup %u, cad wait %u (%u polls), result %u ms; "
                  "standby split: notify %u, cmd %u, detach %u ms; prestage %u ms",
                  (unsigned)(tTxEn - t0), (unsigned)(tStandby - tTxEn), (unsigned)(tSetup - tPrestage),
                  (unsigned)(tWait - tSetup), polls, (unsigned)(millis() - tWait), (unsigned)lastStandbySteps.notifyMs,
                  (unsigned)lastStandbySteps.cmdMs, (unsigned)lastStandbySteps.detachMs, (unsigned)(tPrestage - tStandby));
#ifdef SX126X_TX_LAUNCH_OVERRIDE
        if (result != RADIOLIB_CHANNEL_FREE)
            prestagedLen = 0; // no TX follows, and a detection's RX may have overwritten the buffer
#endif
        if (result == RADIOLIB_LORA_DETECTED) {
            // The chip auto-entered RX (GOTO_RX). Drop the latched CAD verdict so the pin releases and the
            // coming RX_DONE is a clean edge.
            lora.clearIrqFlags(RADIOLIB_SX126X_IRQ_CAD_DONE | RADIOLIB_SX126X_IRQ_CAD_DETECTED);
            noteCadHandoffToRx(); // nothing below arms the radio; the caller's rearmReceive() adopts it
            return true;
        }
        if (result != RADIOLIB_CHANNEL_FREE)
            LOG_ERROR("SX126X scanChannel %s%d", radioLibErr, result);
        if (result != RADIOLIB_ERR_WRONG_MODEM)
            return false;
    }
#ifdef ARCH_PORTDUINO
    portduino_status.LoRa_in_error = true;
#endif
    // standby failed or the LoRa modem type is gone - the chip lost its runtime state
    maybeRecoverChipStateLoss();
    return false; // report the channel free: a recovered chip can TX, a dead one fails startSend safely
}

#ifdef SX126X_TX_LAUNCH_OVERRIDE
template <typename T> bool SX126xInterface<T>::txLaunchTimed() const
{
#if defined(SX126X_TX_LAUNCH_TRACE) || defined(SX126X_TX_PRESTAGE)
    return true;
#else
    return irqPolledOverUsb();
#endif
}

template <typename T> int16_t SX126xInterface<T>::launchTransmit(size_t numbytes)
{
    if (!txLaunchTimed())
        return RadioLibInterface::launchTransmit(numbytes);

    // Everything from the CAD verdict to SET_TX is time the channel goes unwatched, and each command is several
    // USB transfers here. Time each step; with a prestaged payload, send only what the scan overwrote.
    const uint32_t t0 = millis();
    const bool prestaged = prestagedLen != 0 && prestagedLen == numbytes && sendingPacket && sendingPacket->id == prestagedId;
    prestagedLen = 0;
    int16_t res;
    uint32_t tStage, tCmd;
    unsigned polls = 0;
    if (prestaged) {
        // What RadioLib's TX staging would send, less the buffer (already written), the buffer base (RadioLib only
        // ever uses 0/0), the IQ and sensitivity register fixes (earlier stagings set them and the chip keeps them
        // until it loses its registers), and the packet-type read. Packet params are the ones reinitChip() and
        // programModemParams() give RadioLib, with our length.
        const uint8_t packetParams[] = {(uint8_t)(preambleLength >> 8),       (uint8_t)(preambleLength & 0xFF),
                                        RADIOLIB_SX126X_LORA_HEADER_EXPLICIT, (uint8_t)numbytes,
                                        RADIOLIB_SX126X_LORA_CRC_ON,          RADIOLIB_SX126X_LORA_IQ_STANDARD};
        const uint16_t irqMask = RADIOLIB_SX126X_IRQ_TX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT;
        const uint16_t dio1Mask = RADIOLIB_SX126X_IRQ_TX_DONE;
        const uint8_t dioIrqParams[] = {
            (uint8_t)(irqMask >> 8), (uint8_t)(irqMask & 0xFF), (uint8_t)(dio1Mask >> 8), (uint8_t)(dio1Mask & 0xFF), 0, 0, 0, 0};
        const uint8_t clearAll[] = {(uint8_t)(RADIOLIB_SX126X_IRQ_ALL >> 8), (uint8_t)(RADIOLIB_SX126X_IRQ_ALL & 0xFF)};
        res = module.SPIwriteStream(RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS, packetParams, sizeof(packetParams));
        if (res == RADIOLIB_ERR_NONE)
            res = module.SPIwriteStream(RADIOLIB_SX126X_CMD_SET_DIO_IRQ_PARAMS, dioIrqParams, sizeof(dioIrqParams));
        if (res == RADIOLIB_ERR_NONE)
            res = module.SPIwriteStream(RADIOLIB_SX126X_CMD_CLEAR_IRQ_STATUS, clearAll, sizeof(clearAll));
        tStage = millis();
        if (res == RADIOLIB_ERR_NONE) {
            module.setRfSwitchState(Module::MODE_TX);
            const uint8_t txTimeout[] = {0, 0, 0}; // RADIOLIB_SX126X_TX_TIMEOUT_NONE: single TX
            // No BUSY wait inside the command: the one above left the chip idle, and the wait is timed on its own below.
            res = module.SPIwriteStream(RADIOLIB_SX126X_CMD_SET_TX, txTimeout, sizeof(txTimeout), false);
        }
        tCmd = millis();
        if (res == RADIOLIB_ERR_NONE) {
            // As RadioLib's launchMode(): BUSY drops once the PA has ramped, after any oscillator start-up.
            while (module.hal->digitalRead(module.getGpio())) {
                polls++;
                if (millis() - tCmd > 100) {
                    LOG_WARN("Prestaged TX: BUSY still high after 100 ms");
                    break;
                }
                module.hal->yield();
            }
        }
    } else {
        RadioModeConfig_t cfg = {.transmit = {.data = (uint8_t *)&radioBuffer, .len = numbytes, .addr = 0}};
        res = lora.stageMode(RADIOLIB_RADIO_MODE_TX, &cfg);
        tStage = millis();
        if (res == RADIOLIB_ERR_NONE) {
            txStagedByRadioLib = true;
            res = lora.launchMode(); // SET_TX, then the BUSY wait
        }
        tCmd = millis();
    }
    LOG_TRACE("Tx launch steps: %s, verdict to launch %u, stage %u, settx %u, busy %u (%u polls) ms",
              prestaged ? "prestaged" : "radiolib", (unsigned)(t0 - cadVerdictMs), (unsigned)(tStage - t0),
              (unsigned)(tCmd - tStage), (unsigned)(millis() - tCmd), polls);
    return res;
}
#endif

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool SX126xInterface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation. Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    return receiveDetected(lora.getIrqFlags(), RADIOLIB_SX126X_IRQ_HEADER_VALID, RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
}

template <typename T> bool SX126xInterface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_DEBUG("SX126x entering sleep mode"); // (FIXME, don't keep config)
#ifdef SX126X_TX_LAUNCH_OVERRIDE
    txStagedByRadioLib = false; // sleep does not keep every register; let RadioLib stage the next TX in full
#endif
    (void)trySetStandby(); // Stop any pending operations - the chip is being put to sleep, a failure must not crash

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

#ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, LOW);
#endif

#if HAS_LORA_FEM
    loraFEMInterface.setSleepModeEnable();
#endif

    return true;
}

template <typename T> void SX126xInterface<T>::resetAGC()
{
    // Safety: don't reset mid-packet
    if (sendingPacket != NULL || (isReceiving && isActivelyReceiving()))
        return;

    LOG_DEBUG("SX126x AGC reset: warm sleep + Calibrate(0x7F)");
#ifdef SX126X_TX_LAUNCH_OVERRIDE
    txStagedByRadioLib = false; // as in sleep(): the next TX gets RadioLib's full staging
#endif

    // 1. Warm sleep - powers down the entire analog frontend, resetting AGC state.
    //    A plain standby→startReceive cycle does NOT reset the AGC.
    lora.sleep(true);

    // 2. Wake to RC standby for stable calibration
    lora.standby(RADIOLIB_SX126X_STANDBY_RC, true);

    // 3. Calibrate all blocks (ADC, PLL, image, RC oscillators)
    uint8_t calData = RADIOLIB_SX126X_CALIBRATE_ALL;
    module.SPIwriteStream(RADIOLIB_SX126X_CMD_CALIBRATE, &calData, 1, true, false);

    // 4. Wait for calibration to complete (BUSY pin goes low)
    module.hal->delay(5);
    uint32_t start = millis();
    while (module.hal->digitalRead(module.getGpio())) {
        if (millis() - start > 50)
            break;
        module.hal->yield();
    }

    if (module.hal->digitalRead(module.getGpio())) {
        LOG_WARN("SX126x AGC reset: calibration not done in 50ms");
        startReceive();
        return;
    }

    // 5. Re-calibrate image rejection for actual operating frequency
    //    Calibrate(0x7F) defaults to 902-928 MHz which is wrong for other regions.
    lora.calibrateImage(getFreq());
    // 6. CalibrateImage keeps working internally after it returns, and BUSY does not
    //    stay asserted for it; a register write in that window fails write-verify
    //    (RADIOLIB_ERR_SPI_WRITE_FAILED) and stalls the chip.
    module.hal->delay(50);

    // Re-apply settings that calibration may have reset

    // DIO2 as RF switch
#ifdef SX126X_DIO2_AS_RF_SWITCH
    lora.setDio2AsRfSwitch(true);
#elif defined(ARCH_PORTDUINO)
    if (portduino_config.dio2_as_rf_switch)
        lora.setDio2AsRfSwitch(true);
#endif

    // RX boosted gain mode
    lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);

    // Re-apply the undocumented 0x8B5 RX sensitivity patch that was set in init().
    // The CALIBRATE_ALL (0x7F) command above clears bit 0 of register 0x8B5, which
    // silently removes the RX sensitivity improvement introduced in #9571 / #9777.
    // Without this re-apply, every SX1262 node loses its RX boost ~60s after boot
    // and never recovers until reboot. See empirical evidence in the PR description.
    if (module.SPIsetRegValue(0x8B5, 0x01, 0, 0) != RADIOLIB_ERR_NONE) {
        LOG_WARN("SX126x resetAGC: 0x8B5 RX patch re-apply failed");
    }

    // 7. Resume receiving
    startReceive();
}

/** Control PA mode for GC1109 FEM - CPS pin selects full PA (txon=true) or bypass mode (txon=false) */
template <typename T> void SX126xInterface<T>::setTransmitEnable(bool txon)
{
#if HAS_LORA_FEM
    if (txon) {
        loraFEMInterface.setTxModeEnable();
    } else {
        loraFEMInterface.setRxModeEnable();
    }
#endif
}

#endif
