#include "mesh/NodeDB.h"

void variantDefaultConfig()
{
    config.network.eth_enabled = true;
}

void initVariant()
{
    pinMode(LED_PAIRING, OUTPUT);
    digitalWrite(LED_PAIRING, !LED_STATE_ON); // Turn off the LED to start
    pinMode(LED_LORA, OUTPUT);
    digitalWrite(LED_LORA, !LED_STATE_ON); // Turn off the LED to start
}