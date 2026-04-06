/**
   @file RAK12027_D7S.h
   @author rakwireless.com
   @brief  TDK 6-axis digital output sense IC library.
   @version 0.1
   @date 2022-01-01
   @copyright Copyright (c) 2022
**/

#ifndef __RAK12027_D7S_H__
#define __RAK12027_D7S_H__

#include <Arduino.h>
#include <Wire.h>
#include "stdint.h"

#define LIB_DEBUG 0
#if LIB_DEBUG > 0
  #define LIB_LOG(tag, ...)              \
    do                                   \
    {                                    \
      if (tag)                           \
      Serial.printf("#Debug ");          \
      Serial.printf("<%s> ", tag);       \
      Serial.printf(__VA_ARGS__);        \
      Serial.printf("\n");               \
    } while (0)
#else
  #define LIB_LOG(...)
#endif

/**
 * @brief D7S address on the I2C bus.
 */
#define D7S_DEV_ADDRESS 0x55

/**
 * @brief D7S STATE.
 */
typedef enum 
{
  NORMAL_MODE = 0x00,
  NORMAL_MODE_NOT_IN_STANBY = 0x01, // Earthquake in progress.
  INITIAL_INSTALLATION_MODE = 0x02,
  OFFSET_ACQUISITION_MODE = 0x03,
  SELFTEST_MODE = 0x04
}D7S_status_t;

/**
 * @brief D7S axis settings.
 */
typedef enum 
{
  FORCE_YZ = 0x00,
  FORCE_XZ = 0x01,
  FORXE_XY = 0x02,
  AUTO_SWITCH = 0x03,
  SWITCH_AT_INSTALLATION = 0x04 
}D7S_axis_settings_t;

/**
 * @brief D7S axis state.
 */
typedef enum D7S_axis_state 
{
  AXIS_YZ = 0x00,
  AXIS_XZ = 0x01,
  AXIS_XY = 0x02
}D7S_axis_state_t;


/**
 * @brief D7S threshold settings.
 */
typedef enum
{
  THRESHOLD_HIGH = 0x00,
  THRESHOLD_LOW = 0x01
}D7S_threshold_t;

/**
 * @brief D7S message status (selftes, offset acquisition).
 */
typedef enum 
{
  D7S_OK = 0,
  D7S_ERROR = 1
}D7S_mode_status_t;

/**
 * @brief D7S events handled externaly by the using using an handler (the D7S int1, int2 must be connected to interrupt pin)
 */
typedef enum 
{
  START_EARTHQUAKE = 0, //INT 2
  END_EARTHQUAKE = 1, //INT 2
  SHUTOFF_EVENT = 2, //INT 1
  COLLAPSE_EVENT = 3 //INT 1
}D7S_interrupt_event_t;

//class RAK_D7S
class RAK_D7S 
{
public: 

  RAK_D7S(byte addr = D7S_DEV_ADDRESS);

  bool begin(TwoWire &wirePort = Wire, uint8_t deviceAddress = D7S_DEV_ADDRESS); 

  D7S_status_t getState();            // Return the currect state.
  D7S_axis_state_t getAxisInUse();  // Return the current axis in use.

  void setThreshold(D7S_threshold_t threshold); // Change the threshold in use.
  void setAxis(D7S_axis_settings_t axisMode);   // Change the axis selection mode.

  float getLastestSI(uint8_t index);          // Get the lastest SI at specified index (up to 5) [m/s].
  float getLastestPGA(uint8_t index);         // Get the lastest PGA at specified index (up to 5) [m/s^2].
  float getLastestTemperature(uint8_t index); // Get the lastest Temperature at specified index (up to 5) [Celsius].

  // RANKED DATA 
  float getRankedSI(uint8_t position);  // Get the ranked SI at specified position (up to 5) [m/s].
  float getRankedPGA(uint8_t position); // Get the ranked PGA at specified position (up to 5) [m/s^2].
  float getRankedTemperature(uint8_t position); //Get the ranked Temperature at specified position (up to 5) [Celsius].

  // INSTANTANEUS DATA  
  float getInstantaneusSI();  //  Get instantaneus SI (during an earthquake) [m/s].
  float getInstantaneusPGA(); //  Get instantaneus PGA (during an earthquake) [m/s^2].

  // CLEAR MEMORY
  void clearEarthquakeData();     // Delete both the lastest data and the ranked data.
  void clearInstallationData();   //  Delete initializzazion data.
  void clearLastestOffsetData();  // Delete offset data.
  void clearSelftestData();       // Delete selftest data.
  void clearAllData();            // Delete all data.

  // INITIALIZATION
  void initialize();              // Initialize the D7S (start the initial installation mode).

  // SELFTEST
  void selftest();                // Trigger self-diagnostic test.
  D7S_mode_status_t getSelftestResult();  // Return the result of self-diagnostic test (OK/ERROR).

  // OFFSET ACQUISITION
  void acquireOffset(); // Trigger offset acquisition.
  D7S_mode_status_t getAcquireOffsetResult();  //Return the result of offset acquisition test (OK/ERROR).

  // SHUTOFF/COLLAPSE EVENT
  // After each earthquakes it's important to reset the events calling resetEvents() to prevent polluting the new data with the old one.
  uint8_t isInCollapse(); // Return true if the collapse condition is met (it's the sencond bit of _events).
  uint8_t isInShutoff();  // Return true if the shutoff condition is met (it's the first bit of _events).
  void resetEvents();     // Reset shutoff/collapse events.

  // EARTHQUAKE EVENT
  uint8_t isEarthquakeOccuring(); // Return true if an earthquake is occuring.

  // READY STATE
  bool isReady();

private:

  // READ
  uint8_t read8bit(uint8_t regH, uint8_t regL); //read 8 bit from the specified register
  uint16_t read16bit(uint8_t regH, uint8_t regL); //read 16 bit from the specified register

  // WRITE
  void write8bit(uint8_t regH, uint8_t regL, uint8_t val); //write 8 bit to the register specified

  // READ EVENTS
  void readEvents(); //read the event (SHUTOFF/COLLAPSE) from the EVENT register
  
  TwoWire *_i2cPort = &Wire; // The generic connection to user's chosen I2C hardware
  uint8_t _deviceAddress;
};

#endif
