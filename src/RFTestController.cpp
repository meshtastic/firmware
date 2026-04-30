#include "configuration.h"

#ifdef MESHTASTIC_RF_TEST_FIRMWARE

#include "RFTestController.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

RFTestController *rfTestController = nullptr;

RFTestController::RFTestController(std::unique_ptr<RadioInterface> radioInterface) : ownedRadio(std::move(radioInterface))
{
    radio = ownedRadio.get();
    chipPowerDbm = modulePowerToChipPower(requestedModulePowerDbm);

    Serial.println();
    Serial.println("IKOKA RF test firmware ready.");
    if (!radio) {
        Serial.println("ERROR: RF test firmware requires a radio interface.");
        return;
    }

    applySettings();
    printHelp();
    printStatus();
}

void RFTestController::loop()
{
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (lineLength > 0) {
                lineBuffer[lineLength] = '\0';
                processLine(lineBuffer);
                lineLength = 0;
            }
            continue;
        }

        if (lineLength < sizeof(lineBuffer) - 1) {
            lineBuffer[lineLength++] = c;
        } else {
            lineLength = 0;
            println("ERROR: command too long");
        }
    }
}

void RFTestController::processLine(char *line)
{
    char *command = strtok(line, " \t");
    char *argument = strtok(nullptr, " \t");
    if (!command)
        return;

    for (char *p = command; *p; ++p)
        *p = static_cast<char>(toupper(*p));
    if (argument) {
        for (char *p = argument; *p; ++p)
            *p = static_cast<char>(toupper(*p));
    }

    if (strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0) {
        printHelp();
    } else if (strcmp(command, "STATUS") == 0) {
        printStatus();
    } else if (strcmp(command, "FREQ") == 0) {
        if (!argument) {
            println("ERROR: usage FREQ <MHz>");
            return;
        }
        float mhz = 0.0f;
        if (!parseFloat(argument, mhz)) {
            println("ERROR: invalid frequency");
            return;
        }
        setFrequency(mhz);
    } else if (strcmp(command, "POWER") == 0) {
        if (!argument) {
            println("ERROR: usage POWER <module-dBm>");
            return;
        }
        int powerDbm = 0;
        if (!parseInt(argument, powerDbm)) {
            println("ERROR: invalid power");
            return;
        }
        setPower(powerDbm);
    } else if (strcmp(command, "MODE") == 0) {
        if (!argument) {
            println("ERROR: usage MODE LORA|CW|OFF");
            return;
        }
        setMode(argument);
    } else if (strcmp(command, "START") == 0) {
        start();
    } else if (strcmp(command, "STOP") == 0) {
        stop();
    } else {
        println("ERROR: unknown command. Type HELP.");
    }
}

void RFTestController::printHelp()
{
    Serial.println("Commands:");
    Serial.println("  HELP");
    Serial.println("  STATUS");
    Serial.println("  FREQ <MHz>        example: FREQ 433.125");
    Serial.println("  POWER <dBm>       requested E22 module output, max 33");
    Serial.println("  MODE LORA|CW|OFF  LORA is SX126x infinite preamble");
    Serial.println("  START");
    Serial.println("  STOP");
}

void RFTestController::printStatus()
{
    Serial.printf("STATUS mode=%s tx=%s freq=%.3fMHz requested_module_power=%ddBm chip_power=%ddBm\r\n", modeName(),
                  active ? "ON" : "OFF", frequencyMhz, requestedModulePowerDbm, chipPowerDbm);
}

void RFTestController::setMode(const char *modeName)
{
    if (strcmp(modeName, "LORA") == 0 || strcmp(modeName, "PREAMBLE") == 0) {
        mode = Mode::LoRaPreamble;
    } else if (strcmp(modeName, "CW") == 0 || strcmp(modeName, "CARRIER") == 0) {
        mode = Mode::ContinuousWave;
    } else if (strcmp(modeName, "OFF") == 0 || strcmp(modeName, "STOP") == 0) {
        mode = Mode::Off;
        stop();
        printStatus();
        return;
    } else {
        println("ERROR: mode must be LORA, CW, or OFF");
        return;
    }

    restartIfActive();
    printStatus();
}

void RFTestController::setFrequency(float mhz)
{
    if (!isfinite(mhz) || mhz <= 0.0f) {
        println("ERROR: frequency must be a positive MHz value");
        return;
    }

    frequencyMhz = mhz;
    if (!restartIfActive())
        applySettings();
    printStatus();
}

void RFTestController::setPower(int powerDbm)
{
    if (powerDbm < 0 || powerDbm > DEFAULT_MODULE_POWER_DBM) {
        Serial.printf("ERROR: module power must be 0..%d dBm\r\n", DEFAULT_MODULE_POWER_DBM);
        return;
    }

    requestedModulePowerDbm = static_cast<int8_t>(powerDbm);
    chipPowerDbm = modulePowerToChipPower(requestedModulePowerDbm);
    if (!restartIfActive())
        applySettings();
    printStatus();
}

bool RFTestController::parseFloat(const char *text, float &value) const
{
    char *end = nullptr;
    value = strtof(text, &end);
    return end != text && end && *end == '\0';
}

bool RFTestController::parseInt(const char *text, int &value) const
{
    char *end = nullptr;
    long parsed = strtol(text, &end, 10);
    if (end == text || !end || *end != '\0')
        return false;

    value = static_cast<int>(parsed);
    return true;
}

void RFTestController::start()
{
    if (!radio) {
        println("ERROR: no SX1268 radio");
        return;
    }
    if (mode == Mode::Off) {
        println("ERROR: select MODE LORA or MODE CW before START");
        return;
    }
    if (!applySettings())
        return;

    int16_t err = RADIOLIB_ERR_NONE;
    if (mode == Mode::LoRaPreamble) {
        err = radio->startRfTestInfinitePreamble();
    } else {
        err = radio->startRfTestContinuousWave();
    }

    if (err != RADIOLIB_ERR_NONE) {
        Serial.printf("ERROR: RF start failed: %d\r\n", err);
        active = false;
        return;
    }

    active = true;
    printStatus();
}

void RFTestController::stop()
{
    if (radio)
        radio->stopRfTest();
    active = false;
    println("TX stopped");
}

bool RFTestController::applySettings()
{
    if (!radio)
        return false;

    int16_t err = radio->setRfTestParameters(frequencyMhz, chipPowerDbm);
    if (err != RADIOLIB_ERR_NONE) {
        Serial.printf("ERROR: RF settings failed: %d\r\n", err);
        return false;
    }
    return true;
}

bool RFTestController::restartIfActive()
{
    if (!active)
        return false;

    stop();
    start();
    return true;
}

const char *RFTestController::modeName() const
{
    switch (mode) {
    case Mode::LoRaPreamble:
        return "LORA";
    case Mode::ContinuousWave:
        return "CW";
    case Mode::Off:
    default:
        return "OFF";
    }
}

int8_t RFTestController::modulePowerToChipPower(int modulePowerDbm) const
{
    int chipPower = modulePowerDbm - E22_PA_GAIN_DB;
    if (chipPower < MIN_CHIP_POWER_DBM)
        chipPower = MIN_CHIP_POWER_DBM;
    if (chipPower > MAX_CHIP_POWER_DBM)
        chipPower = MAX_CHIP_POWER_DBM;
    return static_cast<int8_t>(chipPower);
}

void RFTestController::println(const char *message)
{
    Serial.println(message);
}

#endif
