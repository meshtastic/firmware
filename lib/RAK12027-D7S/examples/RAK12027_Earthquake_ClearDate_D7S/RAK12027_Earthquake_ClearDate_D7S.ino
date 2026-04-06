/**
   @file RAK12027_Earthquake_ClearDate_D7S.ino
   @author Clear all data inside D7S.
   @version 0.1
   @date 2022-03-02
   @copyright Copyright (c) 2022
**/
#include "RAK12027_D7S.h" // Click here to get the library: http://librarymanager/RAK12027_D7S

RAK_D7S D7S;

void setup() 
{
  pinMode(WB_IO2, OUTPUT);
  digitalWrite(WB_IO2, LOW); 
  delay(1000);
  digitalWrite(WB_IO2, HIGH); // Power up the D7S.

  time_t timeout = millis();
  Serial.begin(115200); // Initialize Serial for debug output.
  while (!Serial)
  {
    if ((millis() - timeout) < 5000)
    {
      delay(100);
    }
    else
    {
      break;
    }
  }
  Serial.println("RAK12027 Clear Date example.");

  Wire.begin();

  while (!D7S.begin()) 
  {
    Serial.print(".");
  }

  //clearing earthquake measured data
  Serial.print("Clearing earthquake data...");
  D7S.clearEarthquakeData();
  Serial.println("CLEARED!\n");

  //clearing installation data
  Serial.print("Clearing installation data...");
  D7S.clearInstallationData();
  Serial.println("CLEARED!\n");

  //clearing lastest offset data
  Serial.print("Clearing lastest offset data...");
  D7S.clearLastestOffsetData();
  Serial.println("CLEARED!\n");

  //clearing selftest data
  Serial.print("Clearing Earthquake data...");
  D7S.clearSelftestData();
  Serial.println("CLEARED!\n");

  //clearing all data
  Serial.print("Clearing all data...");
  D7S.clearAllData();
  Serial.println("CLEARED!\n");
}

void loop() 
{
}
