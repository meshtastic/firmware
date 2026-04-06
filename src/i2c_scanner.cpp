#include <Arduino.h>
// Simple I2C scanner for RAK4631 (nRF52)
// Connect SDA to P0.26, SCL to P0.27 (default for RAK4631)
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\nI2C Scanner for RAK4631");
  Wire.begin();
}

void loop() {
  Serial.println("Scanning...");
  byte count = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      count++;
    }
  }
  if (count == 0) Serial.println("No I2C devices found\n");
  else Serial.println("Done\n");
  delay(5000);
}
