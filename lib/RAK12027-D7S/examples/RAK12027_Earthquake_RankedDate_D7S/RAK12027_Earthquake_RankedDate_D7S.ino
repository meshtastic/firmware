/**
   @file RAK12027_Earthquake_RankedDate_D7S.ino
   @author rakwireless.com
   @brief  Read the data for five earthquakes with the largest SI values, out of all earthquakes that occurred in the past. 
           SI Ranked Data 1 always holds the largest SI value.
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
  Serial.println("RAK12027 Read Ranked Date example.");

  Wire.begin();

  while (!D7S.begin()) 
  {
    Serial.print(".");
  }
  Serial.println("STARTED");
}

void loop() 
{
  Serial.println("--- RANKED EARTHQUAKES BY SI ---\n");
  // Print the 5 greatest earthquakes registered with all data.
  for (int i = 0; i < 5; i++) // The index must be from 0 to 4 (5 earthquakes in total).
  { 
    Serial.print("Earthquake in postion ");
    Serial.println(i+1);
    
    Serial.print("SI: ");
    Serial.print(D7S.getRankedSI(i)); // Getting the ranked SI at position i.
    Serial.println(" [m/s]");
    
    Serial.print("PGA (Peak Ground Acceleration): ");
    Serial.print(D7S.getRankedPGA(i));  // Getting the ranked PGA at position i.
    Serial.println(" [m/s^2]");

    Serial.print("Temperature: ");
    Serial.print(D7S.getRankedTemperature(i));  // Getting the temperature at which the earthquake at position i has occured.
    Serial.println(" [℃]\n");
  }
  delay(5000);  // Wait 500ms before checking again.
}
