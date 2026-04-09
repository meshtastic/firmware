#pragma once

enum LoRaRadioType {
    NO_RADIO,
    STM32WLx_RADIO,
    SIM_RADIO,
    RF95_RADIO,
    SX1262_RADIO,
    SX1268_RADIO,
    LLCC68_RADIO,
    SX1280_RADIO,
    LR1110_RADIO,
    LR1120_RADIO,
    LR1121_RADIO
};

extern LoRaRadioType radioType;