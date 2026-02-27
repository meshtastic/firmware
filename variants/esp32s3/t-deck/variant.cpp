#include "variant.h"
#include "Arduino.h"

void earlyInitVariant()
{
    // GPIO10 manages all peripheral power supplies
    // Turn on peripheral power immediately after MUC starts.
    // If some boards are turned on late, ESP32 will reset due to low voltage.
    // ESP32-C3(Keyboard) , MAX98357A(Audio Power Amplifier) ,
    // TF Card , Display backlight(AW9364DNR) , AN48841B(Trackball) , ES7210(Decoder)
    pinMode(KB_POWERON, OUTPUT);
    digitalWrite(KB_POWERON, HIGH);
    // T-Deck has all three SPI peripherals (TFT, SD, LoRa) attached to the same SPI bus
    // We need to initialize all CS pins in advance otherwise there will be SPI communication issues
    // e.g. when detecting the SD card
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    delay(100);
}