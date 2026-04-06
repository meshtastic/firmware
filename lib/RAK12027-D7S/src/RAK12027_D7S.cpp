/**
   @file RAK12027_D7S.cpp
   @author rakwireless.com
   @brief  IIM42652 configuration function realization.
   @version 0.1
   @date 2022-01-01
   @copyright Copyright (c) 2022
**/

#include "RAK12027_D7S.h"

/*!
 *  @brief  Initialize the class.
 *  @param  addr: The device address of D7S IIC is 0x55. 
 */

RAK_D7S::RAK_D7S(byte addr)
{
  _deviceAddress = addr;
}

/*!
 *  @brief  Confirm that the D7S sensor is ready.
 *  @param  wirePort      : IIC interface used.
 *  @param  deviceAddress : Device address should be 0x55. 
 *  @return If the device init successful return true else return false.
 */
bool RAK_D7S::begin(TwoWire &wirePort, uint8_t deviceAddress)
{
  _deviceAddress = deviceAddress;
  _i2cPort = &wirePort;

  time_t timeout = millis();
  while (isReady())
  {
    if ((millis() - timeout) < 2000)
    {
      delay(500);
    }
    else
    {
      return false; // TimeOut.
    }
  }
  return true;
}

/*!
 *  @brief  Query D7S status.
 *  @param  NULL.
 *  @return D7S status.
 */
D7S_status_t RAK_D7S::getState() 
{
  D7S_status_t date;
  
  date = (D7S_status_t)(read8bit(0x10, 0x00) & 0x07);
  return date;
}

/*!
 *  @brief  Get current axes used for SI value calculation.
 *  @param  NULL.
 *  @return Current axes used for SI value calculation.
 */
D7S_axis_state_t RAK_D7S::getAxisInUse() 
{
  D7S_axis_state_t date;
  
  date = (D7S_axis_state_t)(read8bit(0x10, 0x01) & 0x03) ;
  return date;
}

/*!
 *  @brief  Change the threshold in use.
 *  @param  threshold:
 *            0 : Threshold level H.
 *            1 : Threshold level L.
 *  @return NULL.
 */
void RAK_D7S::setThreshold(D7S_threshold_t threshold) 
{
  if (threshold < 0 || threshold > 1) 
  {
    return;
  }
  uint8_t reg = read8bit(0x10, 0x04); // Read the CTRL register at 0x1004.
  reg = (((reg >> 4) << 1) | (threshold & 0x01)) << 3; // New register value with the threshold.
  write8bit(0x10, 0x04, reg); //Update register.
}

/*!
 *  @brief  SI value calculation axes setting pattern.
 *  @param  axisMode:
 *            0 : YZ axes.
 *            1 : XZ axes. 
 *            2 : XY axes.
 *            3 : Auto switch axes (auto axes calculation by automatically.
 *  @return NULL.
 */
void RAK_D7S::setAxis(D7S_axis_settings_t axisMode) 
{
  if (axisMode < 0 or axisMode > 4)   // Check if axisMode is valid.
  {
    return;
  }

  uint8_t reg = read8bit(0x10, 0x04);   // Read the CTRL register at 0x1004.
  
  reg = (axisMode << 4) | (reg & 0x0F); // New register value with the threshold.
  
  write8bit(0x10, 0x04, reg); // Update register.
}

/*!
 *  @brief  Get the lastest SI at specified index (up to 5) [m/s].
 *  @param  index: 0~4.
 *  @return SI value.
 */
float RAK_D7S::getLastestSI(uint8_t index) 
{
  if (index < 0 || index > 4) // Check if the index is in bound.
  {
    return 0;
  }

  return ((float) read16bit(0x30 + index, 0x08)) / 1000; // Return the value.
}

/*!
 *  @brief  Get the lastest PGA at specified index (up to 5) [m/s^2].
 *  @param  index: 0~4.
 *  @return PGA value.
 */
float RAK_D7S::getLastestPGA(uint8_t index) 
{
  if (index < 0 || index > 4) // Check if the index is in bound.
  {
    return 0;
  }
 
  return ((float) read16bit(0x30 + index, 0x0A)) / 1000; // Return the value.
}

/*!
 *  @brief  Get the lastest Temperature at specified index (up to 5) [Celsius].
 *  @param  index: 0~4.
 *  @return Temperature value.
 */
float RAK_D7S::getLastestTemperature(uint8_t index)
{
  if (index < 0 || index > 4) // Check if the index is in bound.
  {
    return 0;
  }
  
  return (float) ((int16_t) read16bit(0x30 + index, 0x06)) / 10; // Return the value.
}

/*!
 *  @brief  Get the ranked SI at specified position (up to 5) [m/s].
 *  @param  position: 0~4.
 *  @return Ranked SI value.
 */
float RAK_D7S::getRankedSI(uint8_t position) 
{
  if (position < 0 || position > 4) // Check if the position is in bound.
  {
    return 0;
  }
  
  return ((float) read16bit(0x30 + position +5, 0x08)) / 1000; // Return the value.
}

/*!
 *  @brief  Get the ranked PGA at specified position (up to 5) [m/s^2].
 *  @param  Index 0~4 ranked PGA data.
 *  @return Ranked PGA value.
 */
float RAK_D7S::getRankedPGA(uint8_t position) 
{
  if (position < 0 || position > 4) // Check if the position is in bound.
  {
    return 0;
  }
  
  return ((float) read16bit(0x30 + position +5, 0x0A)) / 1000; // Return the value.
}

/*!
 *  @brief  Get the ranked Temperature at specified position (up to 5) [Celsius].
 *  @param  Index 0~4 ranked Temperature data.
 *  @return Ranked Temperature value.
 */
float RAK_D7S::getRankedTemperature(uint8_t position) 
{
  if (position < 0 || position > 4) // Check if the position is in bound.
  {
    return 0;
  }
  
  return (float) ((int16_t) read16bit(0x30 + position +5, 0x06)) / 10; // Return the value.
}

/*!
 *  @brief  Get instantaneus SI (during an earthquake) [m/s].
 *  @param  NULL.
 *  @return Instantaneus SI value.
 */
float RAK_D7S::getInstantaneusSI() 
{
  return ((float) read16bit(0x20, 0x00)) / 1000; // Return the value.
}

/*!
 *  @brief  Get instantaneus PGA (during an earthquake) [m/s^2].
 *  @param  NULL.
 *  @return NULL.
 */
float RAK_D7S::getInstantaneusPGA() 
{
  return ((float) read16bit(0x20, 0x02)) / 1000; // Return the value.
}

/*!
 *  @brief  Delete both the lastest data and the ranked data.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::clearEarthquakeData() 
{
  write8bit(0x10, 0x05, 0x01); // Write clear command.
}

/*!
 *  @brief  Delete initializzazion data.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::clearInstallationData() 
{
  write8bit(0x10, 0x05, 0x08); // Write clear command.
}

/*!
 *  @brief  Delete offset data.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::clearLastestOffsetData() 
{
  write8bit(0x10, 0x05, 0x04);  // Write clear command.
}

/*!
 *  @brief  Delete selftest data.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::clearSelftestData() 
{
  write8bit(0x10, 0x05, 0x02); // Write clear command.
}

/*!
 *  @brief  Delete all data.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::clearAllData() 
{
  write8bit(0x10, 0x05, 0x0F); // Write clear command.
}

/*!
 *  @brief  Initialize the D7S (start the initial installation mode).
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::initialize() 
{
  write8bit(0x10, 0x03, 0x02); // Write INITIAL INSTALLATION MODE command.
}

/*!
 *  @brief  Start autodiagnostic and resturn the result (OK/ERROR).
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::selftest() 
{
  write8bit(0x10, 0x03, 0x04);  // Write SELFTEST command.
}

/*!
 *  @brief  Return the result of self-diagnostic test (OK/ERROR).
 *  @param  NULL.
 *  @return 0 : OK. 1 : ERROR.
 */
D7S_mode_status_t RAK_D7S::getSelftestResult() 
{
  return (D7S_mode_status_t) ((read8bit(0x10, 0x02) & 0x07) >> 2); // Return result of the selftest.
}

/*!
 *  @brief  Start offset acquisition and return the rersult (OK/ERROR).
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::acquireOffset() 
{
  write8bit(0x10, 0x03, 0x03);  // Write OFFSET ACQUISITION MODE command.
}

/*!
 *  @brief  Return the result of offset acquisition test (OK/ERROR).
 *  @param  NULL.
 *  @return NULL.
 */
D7S_mode_status_t RAK_D7S::getAcquireOffsetResult()
{
  return (D7S_mode_status_t) ((read8bit(0x10, 0x02) & 0x0F) >> 3);  // Return result of the offset acquisition.
}

/*!
 *  @brief  After each earthquakes it's important to reset the events calling resetEvents() to prevent polluting the new data with the old one
 *          Return true if the collapse condition is met (it's the sencond bit of _events).
 *  @param  NULL.
 *  @return NULL.
 */
uint8_t RAK_D7S::isInCollapse() 
{
  readEvents();  // Updating the _events variable.
  return (0x02) >> 1; // Return the second bit of _events.
}

/*!
 *  @brief  Return true if the shutoff condition is met (it's the first bit of _events).
 *  @param  NULL.
 *  @return NULL.
 */
uint8_t RAK_D7S::isInShutoff()
{
  readEvents(); // Updating the _events variable.
  
  return 0x01; // Return the second bit of _events.
}

/*!
 *  @brief  Reset shutoff/collapse events.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::resetEvents()
{
  read8bit(0x10, 0x02); // Reset the EVENT register (read to zero-ing it).
}

/*!
 *  @brief  Return true if an earthquake is occuring.
 *  @param  NULL.
 *  @return NULL.
 */
uint8_t RAK_D7S::isEarthquakeOccuring() 
{
  // If D7S is in NORMAL MODE NOT IN STANBY (after the first 4 sec to initial delay) there is an earthquake.
  return getState() == NORMAL_MODE_NOT_IN_STANBY;
}

/*!
 *  @brief  Ready state.
 *  @param  NULL.
 *  @return NULL.
 */
bool RAK_D7S::isReady() 
{
  if (getState() == NORMAL_MODE)
    return true;
  else
    return false;
}

/*!
 *  @brief  Read 8 bit from the specified register.
 *  @param  NULL.
 *  @return NULL.
 */
uint8_t RAK_D7S::read8bit(uint8_t regH, uint8_t regL) 
{
  LIB_LOG("[read8bit]: ","REG: 0x%x%x",regH,regL);  

  _i2cPort->beginTransmission(_deviceAddress); // Setting up i2c connection.

  // Write register address
  _i2cPort->write(regH); // Register address high
  delay(10); // Delay to prevent freezing
  _i2cPort->write(regL); // Register address low
  delay(10); // Delay to prevent freezing

  // Send RE-START message
  uint8_t status = _i2cPort->endTransmission(false);

  LIB_LOG("[read8bit]: ","%d",status);

  delay(10); // Delay to prevent freezing

  // If the status != 0 there is an error
  if (status != 0)
  {
    return read8bit(regH, regL); // Retry.
  }

  //  Request 1 byte.
  _i2cPort->requestFrom(_deviceAddress, 1);
  uint8_t data = 0x00;
  //while (_i2cPort->available() < 1); // Wait until the data is received.
  if (_i2cPort->available() == 1)
  {
    data = _i2cPort->read(); // Read the data.
  }
  
  return data;  // Return the data.
}

/*!
 *  @brief  Read 16 bit from the specified register.
 *  @param  NULL.
 *  @return NULL.
 */
uint16_t RAK_D7S::read16bit(uint8_t regH, uint8_t regL) 
{
  LIB_LOG("[read16bit]: ","REG: 0x%x%x",regH,regL);

  _i2cPort->beginTransmission(_deviceAddress); // Setting up i2c connection.

  // Write register address.
  _i2cPort->write(regH); // Register address high.
  delay(10); // Delay to prevent freezing.
  _i2cPort->write(regL); // Register address low.
  delay(10); // Delay to prevent freezing.

  // Send RE-START message.
  uint8_t status = _i2cPort->endTransmission(false);

  LIB_LOG("[RE-START]: ","%d",status);

  // If the status != 0 there is an error.
  if (status != 0) 
  {
    return read16bit(regH, regL);
  }  
  _i2cPort->requestFrom(_deviceAddress, 2); // Request 2 byte.
  while (_i2cPort->available() < 2); // Wait until the data is received.
  uint8_t msb = _i2cPort->read();  // Read the data.
  uint8_t lsb = _i2cPort->read();
  return (msb << 8) | lsb; // Return the data.
}

/*!
 *  @brief  Write 8 bit to the register specified.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::write8bit(uint8_t regH, uint8_t regL, uint8_t val) 
{
  LIB_LOG("write8bit","--- write8bit ---");

  // Setting up i2c connection.
  _i2cPort->beginTransmission(_deviceAddress);

  // Write register address.
  _i2cPort->write(regH);  // Register address high.
  delay(10);             // Delay to prevent freezing.
  _i2cPort->write(regL);  // Register address low.
  delay(10);             // Delay to prevent freezing.

  // Write data.
  _i2cPort->write(val);
  delay(10); // Delay to prevent freezing.

  // Closing the connection (STOP message).
  uint8_t status = _i2cPort->endTransmission(true);

  // Debug.
  LIB_LOG("[STOP]: ","%d",status);
}

/*!
 *  @brief  Read the event (SHUTOFF/COLLAPSE) from the EVENT register.
 *  @param  NULL.
 *  @return NULL.
 */
void RAK_D7S::readEvents() 
{
  // Read the EVENT register at 0x1002 and obtaining only the first two bits
  uint8_t events = read8bit(0x10, 0x02) & 0x03;
  // Updating the _events variable
}
