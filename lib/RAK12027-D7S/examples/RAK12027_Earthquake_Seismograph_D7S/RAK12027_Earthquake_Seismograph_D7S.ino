/**
   @file RAK12027_Earthquake_Seismograph_D7S.ino
   @author rakwireless.com
   @brief  When the trigger earthquake occurs, the serial port outputs the SI and PGA values in the current calculation.
           About 2 minutes of seismic processing ends.
           After the earthquake, the value is 0.
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
  Serial.println("RAK12027 Seismograph example.");

  Wire.begin();

  while (!D7S.begin()) 
  {
    Serial.print(".");
  }
  Serial.println("STARTED");

  Serial.println("Setting D7S sensor to switch axis at inizialization time.");
  D7S.setAxis(SWITCH_AT_INSTALLATION);  // Setting the D7S to switch the axis at inizialization time.

  Serial.println("Initializing the D7S sensor in 2 seconds. Please keep it steady during the initializing process.");
  delay(2000);
  Serial.print("Initializing");
  D7S.initialize(); // Start the initial installation procedure.
  delay(500);
  
  while (!D7S.isReady()) // Wait until the D7S is ready (the initializing process is ended).
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("INITIALIZED!");

  Serial.println("\nListening for earthquakes!"); // READY TO GO.
}

void loop() 
{
  if (D7S.isEarthquakeOccuring()) // Checking if there is an earthquake occuring right now.
  {
    Serial.print("Instantaneus SI: ");
    Serial.print(D7S.getInstantaneusSI());  // Getting instantaneus SI.
    Serial.println(" [m/s]");

    Serial.print("Instantaneus PGA (Peak Ground Acceleration): ");
    Serial.print(D7S.getInstantaneusPGA()); // Getting instantaneus PGA.
    Serial.println(" [m/s^2]\n");
  }
  delay(500); // Wait 500ms before checking again.
}
