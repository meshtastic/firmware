/**
   @file RAK12027_Earthquake_Interrupt_D7S.ino
   @author rakwireless.com
   @brief   Example of interrupt usage.
            INT1 active (ON) when the shutoff judgment condition and collapse detection condition are met.
            INT2 active (ON) during earthquake calculations, offset acquisition,and self-diagnostic processing.
   @version 0.1
   @date 2022-03-02
   @copyright Copyright (c) 2022
**/
#include "RAK12027_D7S.h" // Click here to get the library: http://librarymanager/RAK12027_D7S

RAK_D7S D7S;

/*
 * @note Sensor can only be plugged into SlotA or D, SlotA and B have conflict on IO.
 */
#define INT1  WB_IO3    //  WB_IO5 in SlotD.
#define INT2  WB_IO4    //  WB_IO6 in SlotD.

bool g_interrupt1Flag = false;  // Global variabl to keep track of new interrupts.
bool g_interrupt2Flag = false;

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
  Serial.println("RAK12027 Interrupt example.");

  Wire.begin();

  while (!D7S.begin()) 
  {
    Serial.print(".");
  }
  Serial.println("STARTED");

  Serial.println("Setting D7S sensor to switch axis at inizialization time.");
  D7S.setAxis(SWITCH_AT_INSTALLATION);  // Setting the D7S to switch the axis at inizialization time.

  pinMode(INT1, INPUT_PULLUP);
  pinMode(INT2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT1), int1_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(INT2), int2_ISR, CHANGE);

  Serial.println("Initializing the D7S sensor in 2 seconds. Please keep it steady during the initializing process.");
  delay(2000);
  Serial.print("Initializing");
  D7S.initialize(); // Start the initial installation procedure.
  delay(500);
  
  D7S.initialize(); // Start the initial installation procedure.
  
  while (!D7S.isReady())  // Wait until the D7S is ready (the initializing process is ended).
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("INITIALIZED!");

  if (D7S.isInCollapse()) 
  {
    // Check if there there was a collapse (if this is the first time the D7S is put in place the installation data may be wrong)
    Serial.println("COLLAPSE!");
  }

  D7S.resetEvents();  // Reset the events shutoff/collapse memorized into the D7S.
  Serial.println("\nListening for earthquakes!");
}

void loop() 
{
  if (g_interrupt1Flag == true)// INT1 Event.
  {
    g_interrupt1Flag = false;
    if (D7S.isInShutoff()) 
    {
      Serial.println("Shutting down all device!");
    } 
    else 
    {
      Serial.println("COLLAPSE!");
    }
  }
  if (g_interrupt2Flag == true)// INT2 Event.
  {
    g_interrupt2Flag = false;
    if (D7S.isEarthquakeOccuring()) //START_EARTHQUAKE EVENT.
    { 
      Serial.println("EARTHQUAKE STARTED!\n");
    }
    else // Earthquake ended.
    {
      Serial.println("EARTHQUAKE ENDED!");
      Serial.print("SI: ");
      Serial.print(D7S.getLastestSI(0));
      Serial.println(" [m/s]");
      
      Serial.print("PGA (Peak Ground Acceleration): ");
      Serial.print(D7S.getLastestPGA(0));
      Serial.println(" [m/s^2]\n");
      
      Serial.print("Temperature: ");
      Serial.print(D7S.getLastestTemperature(0));
      Serial.println(" [℃]\n");
    }
  }
}

/*
 * @brief INT1 interrupt handler.
 */
void int1_ISR()
{
  g_interrupt1Flag = true;
}

/*
 * @brief INT2 interrupt handler.
 */
void int2_ISR()
{
  g_interrupt2Flag = true;
}
