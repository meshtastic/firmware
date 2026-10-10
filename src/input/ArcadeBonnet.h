#pragma once
// Adafruit Arcade Bonnet (product 3422): six buttons and a 4-way joystick on an MCP23017 I2C
// port expander. Polled over I2C rather than driven by the expander's interrupt line, because the
// bonnet wires that line to GPIO17, which a HUB75 panel on the adafruit-hat mapping uses as its
// clock.
#ifdef ARCH_PORTDUINO
#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include <map>
#include <stdint.h>

#define ARCADE_BONNET_DEFAULT_ADDR 0x26

// D-pad auto-repeat while a direction is held, matching LinuxJoystick.
#define ARCADE_BONNET_REPEAT_DELAY_MS 400
#define ARCADE_BONNET_REPEAT_INTERVAL_MS 150

class ArcadeBonnet : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit ArcadeBonnet(const char *name);
    bool init(); // Configures the expander and registers with the InputBroker; false if it is absent

    // Held joystick zone: -1 = left/up, 0 = centered, +1 = right/down. Same contract as LinuxJoystick.
    int heldXZone() const { return heldX; }
    int heldYZone() const { return heldY; }
    const char *originName() const { return _originName; }

  protected:
    virtual int32_t runOnce() override;

  private:
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readPins(uint16_t &pins);
    void emitEvent(input_broker_event event, unsigned char kbchar = 0);

    const char *_originName;
    uint8_t address = ARCADE_BONNET_DEFAULT_ADDR;

    // Expander bit (0-5 = buttons 1A-1F) -> broker event, from config or defaults.
    std::map<int, input_broker_event> buttonMap;

    // Active-high pressed bits from the previous poll (the pins themselves read low when pressed).
    uint16_t lastPressed = 0;

    int heldX = 0;
    int heldY = 0;
    uint32_t nextRepeatX = 0;
    uint32_t nextRepeatY = 0;
};
extern ArcadeBonnet *arcadeBonnet;
#endif
