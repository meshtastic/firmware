#include "configuration.h"
#ifdef ARCH_PORTDUINO
#include "ArcadeBonnet.h"
#include "InputBroker.h"
#include "Throttle.h"
#include "platform/portduino/PortduinoGlue.h"
#include <Wire.h>

ArcadeBonnet *arcadeBonnet;

// MCP23017 registers, BANK=0 layout (A/B pairs interleaved).
#define MCP_IODIRA 0x00
#define MCP_IPOLA 0x02
#define MCP_GPINTENA 0x04
#define MCP_IOCON 0x0A
#define MCP_IOCON_BANK1 0x05 // IOCON's address if the chip was left in BANK=1 mode
#define MCP_GPPUA 0x0C
#define MCP_GPIOA 0x12

// IOCON: BANK=0, open-drain INT. The bonnet ties INT to GPIO17 (the HUB75 clock on adafruit-hat),
// so it must float rather than drive; GPINTEN stays 0 so it never asserts either.
#define MCP_IOCON_ODR 0x04

// Expander bits. Port A 0-5 are buttons 1A-1F; port B 0-3 are the 4-way joystick header. Port B
// 4-7 (the analog stick's digital outputs) are left alone: they float with no stick attached.
#define BONNET_JOY_DOWN (1 << 8)
#define BONNET_JOY_UP (1 << 9)
#define BONNET_JOY_RIGHT (1 << 10)
#define BONNET_JOY_LEFT (1 << 11)
#define BONNET_BUTTON_COUNT 6
#define BONNET_BUTTON_MASK 0x003F

// The evdev gamepad code each button reports in kbchar, so games can tell buttons apart exactly
// as they do on a USB pad. None is BTN_START: games pause on Start, and every bonnet button should
// do its action in play.
static const int buttonCodes[BONNET_BUTTON_COUNT] = {
    0x130, // 1A  BTN_SOUTH
    0x131, // 1B  BTN_EAST
    0x133, // 1C  BTN_NORTH
    0x134, // 1D  BTN_WEST
    0x132, // 1E  BTN_C
    0x135, // 1F  BTN_Z
};

ArcadeBonnet::ArcadeBonnet(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

bool ArcadeBonnet::writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// Read GPIOA and GPIOB in one sequential transfer. Returns the pins inverted to active-high, since
// the buttons short a pulled-up input to ground.
bool ArcadeBonnet::readPins(uint16_t &pressed)
{
    Wire.beginTransmission(address);
    Wire.write(MCP_GPIOA);
    if (Wire.endTransmission() != 0)
        return false;
    if (Wire.requestFrom(address, (uint8_t)2) != 2)
        return false;
    const uint8_t portA = Wire.read();
    const uint8_t portB = Wire.read();
    pressed = (uint16_t)~(portA | (portB << 8));
    return true;
}

bool ArcadeBonnet::init()
{
    const int configured = portduino_config.arcadeBonnetAddress;
    if (configured < ARCADE_BONNET_ADDR_MIN || configured > ARCADE_BONNET_ADDR_MAX) {
        LOG_WARN("Arcade Bonnet: 0x%x is not an MCP23017 address (0x20-0x27)", configured);
        return false;
    }
    address = (uint8_t)configured;

    // All 16 pins inputs with pull-ups and no inversion or interrupts. Unlike Adafruit's script we
    // don't mirror INTA/INTB or configure interrupt-on-change: nothing listens to that line.
    if (!writeRegister(MCP_IOCON_BANK1, 0x00) || !writeRegister(MCP_IOCON, MCP_IOCON_ODR) || !writeRegister(MCP_IODIRA, 0xFF) ||
        !writeRegister(MCP_IODIRA + 1, 0xFF) || !writeRegister(MCP_IPOLA, 0x00) || !writeRegister(MCP_IPOLA + 1, 0x00) ||
        !writeRegister(MCP_GPINTENA, 0x00) || !writeRegister(MCP_GPINTENA + 1, 0x00) || !writeRegister(MCP_GPPUA, 0xFF) ||
        !writeRegister(MCP_GPPUA + 1, 0xFF)) {
        LOG_WARN("Arcade Bonnet: no MCP23017 answering at 0x%02x", address);
        return false;
    }

    if (portduino_config.arcadeBonnetButtons.empty()) {
        // 1A, 1C and 1F select; 1B, 1D and 1E cancel.
        buttonMap[0] = INPUT_BROKER_SELECT; // 1A
        buttonMap[1] = INPUT_BROKER_CANCEL; // 1B
        buttonMap[2] = INPUT_BROKER_SELECT; // 1C
        buttonMap[3] = INPUT_BROKER_CANCEL; // 1D
        buttonMap[4] = INPUT_BROKER_CANCEL; // 1E
        buttonMap[5] = INPUT_BROKER_SELECT; // 1F
    } else {
        for (const auto &button : portduino_config.arcadeBonnetButtons) {
            input_broker_event event = inputBrokerEventFromAction(button.second);
            if (event != INPUT_BROKER_NONE)
                buttonMap[button.first] = event;
        }
    }

    // Seed from the current state so a button held through boot doesn't fire.
    readPins(lastPressed);

    LOG_INFO("Arcade Bonnet: MCP23017 at 0x%02x", address);
    inputBroker->registerSource(this);
    return true;
}

void ArcadeBonnet::emitEvent(input_broker_event event, unsigned char kbchar)
{
    InputEvent e = {};
    e.inputEvent = event;
    e.source = this->_originName;
    e.kbchar = kbchar;
    this->notifyObservers(&e);
}

// Zone for one joystick axis from its two switch bits: -1, 0 or +1. Both closed reads as centered.
static int switchZone(uint16_t pressed, uint16_t lowBit, uint16_t highBit)
{
    const bool low = pressed & lowBit;
    const bool high = pressed & highBit;
    if (low == high)
        return 0;
    return low ? -1 : 1;
}

int32_t ArcadeBonnet::runOnce()
{
    uint16_t pressed;
    if (!readPins(pressed)) {
        // Bus hiccup or the bonnet went away. Release the stick so nothing polling heldXZone() keeps
        // moving, and back off; the next good read re-establishes any direction still held.
        heldX = 0;
        heldY = 0;
        return 500;
    }

    // Buttons fire once on press, carrying their gamepad code. Polling at 20 ms is slower than the
    // switches bounce, so a single changed sample is a real edge.
    const uint16_t newlyPressed = pressed & ~lastPressed & BONNET_BUTTON_MASK;
    for (int bit = 0; bit < BONNET_BUTTON_COUNT; bit++) {
        if (!(newlyPressed & (1 << bit)))
            continue;
        auto mapped = buttonMap.find(bit);
        if (mapped != buttonMap.end())
            emitEvent(mapped->second, joyButtonToKbchar(buttonCodes[bit]));
    }
    lastPressed = pressed;

    // Joystick: emit on entering a direction and auto-repeat while held, with kbchar 0 so games
    // treat it as the D-pad rather than a button.
    const uint32_t now = millis();
    const int zoneX = switchZone(pressed, BONNET_JOY_LEFT, BONNET_JOY_RIGHT);
    if (zoneX != heldX) {
        heldX = zoneX;
        if (zoneX != 0) {
            emitEvent((zoneX < 0) ? INPUT_BROKER_LEFT : INPUT_BROKER_RIGHT);
            // unset-sentinel-ok: heldX carries the armed state, so 0 is a legal deadline
            nextRepeatX = now + ARCADE_BONNET_REPEAT_DELAY_MS;
        }
    } else if (heldX != 0 && Throttle::deadlinePassedAt(now, nextRepeatX)) {
        emitEvent((heldX < 0) ? INPUT_BROKER_LEFT : INPUT_BROKER_RIGHT);
        // unset-sentinel-ok: heldX carries the armed state, so 0 is a legal deadline
        nextRepeatX = now + ARCADE_BONNET_REPEAT_INTERVAL_MS;
    }

    const int zoneY = switchZone(pressed, BONNET_JOY_UP, BONNET_JOY_DOWN);
    if (zoneY != heldY) {
        heldY = zoneY;
        if (zoneY != 0) {
            emitEvent((zoneY < 0) ? INPUT_BROKER_UP : INPUT_BROKER_DOWN);
            // unset-sentinel-ok: heldY carries the armed state, so 0 is a legal deadline
            nextRepeatY = now + ARCADE_BONNET_REPEAT_DELAY_MS;
        }
    } else if (heldY != 0 && Throttle::deadlinePassedAt(now, nextRepeatY)) {
        emitEvent((heldY < 0) ? INPUT_BROKER_UP : INPUT_BROKER_DOWN);
        // unset-sentinel-ok: heldY carries the armed state, so 0 is a legal deadline
        nextRepeatY = now + ARCADE_BONNET_REPEAT_INTERVAL_MS;
    }

    return 20;
}

#endif
