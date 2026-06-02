/*
  XIAO ESP32C6 variant init
  Fixes:
  1. WDT timeout a 120s (il default 5s scatta durante radio init)
  2. SPI esplicito con i nostri pin
  3. PRE-RESET del SX1262 con timing corretto
  4. Stack loopTask 24KB (default 8KB insufficiente per RadioLib + bit-bang + WiFi)
*/

#include <Arduino.h>
#include <SPI.h>
#include "esp_task_wdt.h"

// Nostri pin SX1262
#define SX_NSS  23
#define SX_SCK  19
#define SX_MOSI 18
#define SX_MISO 20
#define SX_RST  2
#define SX_BUSY 21

void waitBusy() {
  int t = 0;
  while (digitalRead(SX_BUSY) && t < 50000) {
    delayMicroseconds(10);
    t++;
  }
}

void sxPreReset() {
  // Configura i pin come input prima
  const int pins[] = {SX_NSS, SX_SCK, SX_MOSI, SX_MISO, SX_RST, SX_BUSY};
  for (int p : pins) pinMode(p, INPUT);

  // RST come output HIGH (non in reset)
  pinMode(SX_RST, OUTPUT);
  digitalWrite(SX_RST, HIGH);

  // BUSY come input
  pinMode(SX_BUSY, INPUT);

  // === RESET CON TIMING DEL NOSTRO DRIVER COLLAUDATO ===
  digitalWrite(SX_RST, LOW);
  delay(10);                    // 10ms reset pulse (RadioLib: 1ms)
  digitalWrite(SX_RST, HIGH);
  delay(20);                    // 20ms stabilization (RadioLib: 0ms)
  waitBusy();                   // Aspetta BUSY=0 (RadioLib: non lo fa!)

  Serial.print("Pre-reset: BUSY=");
  Serial.println(digitalRead(SX_BUSY));
}

void earlyInitVariant() {
  // Fix 1: Riconfigura WDT per evitare crash durante lungo radio init
  esp_task_wdt_config_t cfg = {
    .timeout_ms = 120000,
    .idle_core_mask = 0x01,
    .trigger_panic = false
  };
  esp_task_wdt_reconfigure(&cfg);

  // Fix 2: Pre-reset SX1262 con timing corretto
  sxPreReset();

  // Fix 3: Inizializza SPI con SS=-1 (disabilita hardware SS)
  SPI.end();
  delay(10);
  SPI.begin(SX_SCK, SX_MISO, SX_MOSI, -1);  // SS=-1 = no hardware SS!
  SPI.setFrequency(1000000);  // 1 MHz (testato funzionante)

  Serial.println("earlyInit: WDT+preReset+SPI@1MHz(noHWSS) done");
}

void lateInitVariant() {
  Serial.println("lateInit: done");
}
