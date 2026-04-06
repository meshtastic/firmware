| <center><img src="./assets/rakstar.jpg" alt="RAKstar" width=25%></center>  | ![RAKWireless](./assets/RAK-Whirls.png) | [![Build Status](https://github.com/RAKWireless/RAK12027-D7S/workflows/RAK%20Library%20Build%20CI/badge.svg)](https://github.com/RAKWireless/RAK12027-D7S/actions) |
| -- | -- | -- |

# <RAK12027>

RAK12027-D7S is modified from D7S_Arduino_Library Version 1.1.0 written by Alessandro Pasqualini. Contains very convenient interfaces to implement all the operations you want with D7S. 

[*RAKWireless <RAK#> <function>*](https://store.RAKWireless.com/products/earthquake-sensor-omron-d7s-rak12027)

# Documentation

* **[Product Repository](https://github.com/RAKWireless/RAK12027-D7S)** - Product repository for the RAKWireless RAK12027 Earthquake module.
* **[Documentation](https://docs.RAKWireless.com/Product-Categories/WisBlock/RAK12027/Overview/)** - Documentation and Quick Start Guide for the RAK12027 Earthquake module.

# Installation

In Arduino IDE open Sketch->Include Library->Manage Libraries then search for RAK12027.

In PlatformIO open PlatformIO Home, switch to libraries and search for RAK12027.
Or install the library project dependencies by adding

```log
lib_deps =
  RAKWireless/RAKWireless D7S Earthquake library
```

into **`platformio.ini`**

For manual installation download the archive, unzip it and place the RAK12027-D7S folder into the library directory.
In Arduino IDE this is usually <arduinosketchfolder>/libraries/
In PlatformIO this is usually <user/.platformio/lib>

# Usage

The library provides RAK_D7S class, which allows communication with D7S via IIC. These examples show how to use RAK12027.

- [RAK12027_Earthquake_Seismograph_D7S](./examples/RAK12027_Earthquake_Seismograph_D7S) When the trigger earthquake occurs, the serial port outputs the SI and PGA values in the current calculation.  About 2 minutes of seismic processing ends.
- [RAK12027_Earthquake_Interrupt_D7S](./examples/RAK12027_Earthquake_Interrupt_D7S) Example of interrupt usage.  INT1 active (ON) when the shutoff judgment condition and collapse detection condition are met. INT2 active (ON) during earthquake calculations, offset acquisition,and self-diagnostic processing.
- [RAK12027_Earthquake_RankedDate_D7S](./examples/RAK12027_Earthquake_RankedDate_D7S) Read the data for five earthquakes with the largest SI values, out of all earthquakes that occurred in the past. SI Ranked Data 1 always holds the largest SI value.
- [RAK12027_Earthquake_ClearDate_D7S](./examples/RAK12027_Earthquake_ClearDate_D7S) Clear all data inside D7S.

## This class provides the following methods:

bool RAK_D7S::begin(TwoWire &wirePort, uint8_t deviceAddress)

Confirm that the D7S sensor is ready.

#### Parameters:

| Direction | Name          | Function                                                     |
| --------- | ------------- | ------------------------------------------------------------ |
| in        | wirePort      | IIC interface used.                                          |
| in        | deviceAddress | Device address should be 0x55.                               |
| return    |               | If the device init successful return true else return false. |

**D7S_status_t RAK_D7S::getState()  **

Read the STATE register at 0x1000.

#### Parameters:

| Direction | Name | Function    |
| --------- | ---- | ----------- |
| return    |      | D7S status. |

**D7S_axis_state_t RAK_D7S::getAxisInUse()  **

Get current axes used for SI value calculation.

#### Parameters:

| Direction | Name | Function                                    |
| --------- | ---- | ------------------------------------------- |
| return    |      | Current axes used for SI value calculation. |

**void RAK_D7S::setThreshold(D7S_threshold_t threshold)  **

Change the threshold in use.

#### Parameters:

| Direction | Name      | Function                                            |
| --------- | --------- | --------------------------------------------------- |
| in        | threshold | 0 : Threshold level H.<br />1 :  Threshold level L. |
| return    |           | none                                                |

**void RAK_D7S::setAxis(D7S_axis_settings_t axisMode)  **

SI value calculation axes setting pattern.

#### Parameters:

| Direction | Name     | Function                                                     |
| --------- | -------- | ------------------------------------------------------------ |
| in        | axisMode | 0 : YZ axes.<br />1 : XZ axes. <br />2 : XY axes. <br />3 : Auto switch axes (auto axes calculation by automatically. |
| return    |          | none                                                         |

**float RAK_D7S::getLastestSI(uint8_t index) **

Get the lastest SI at specified index (up to 5) [m/s].

#### Parameters:

| Direction | Name  | Function           |
| --------- | ----- | ------------------ |
| in        | index | Index 0~4 SI data. |
| return    |       | SI value.          |

**float RAK_D7S::getLastestPGA(uint8_t index) **

Get the lastest PGA at specified index (up to 5) [m/s^2].

#### Parameters:

| Direction | Name  | Function            |
| --------- | ----- | ------------------- |
| in        | index | Index 0~4 PGA data. |
| return    |       | PGA value.          |

**float RAK_D7S::getLastestTemperature(uint8_t index)**

Get the lastest Temperature at specified index (up to 5) [Celsius].

#### Parameters:

| Direction | Name  | Function                    |
| --------- | ----- | --------------------------- |
| in        | index | Index 0~4 Temperature data. |
| return    |       | Temperature value.          |

**float RAK_D7S::getRankedSI(uint8_t position) **

Get the ranked SI at specified position (up to 5) [m/s].

#### Parameters:

| Direction | Name     | Function                  |
| --------- | -------- | ------------------------- |
| in        | position | Index 0~4 ranked SI data. |
| return    |          | Ranked SI value.          |

**float RAK_D7S::getRankedPGA(uint8_t position) **

Get the ranked PGA at specified position (up to 5) [m/s^2].

#### Parameters:

| Direction | Name     | Function                   |
| --------- | -------- | -------------------------- |
| in        | position | Index 0~4 ranked PGA data. |
| return    |          | Ranked PGA value.          |

**float RAK_D7S::getRankedTemperature(uint8_t position) **

Get the ranked Temperature at specified position (up to 5) [Celsius].

#### Parameters:

| Direction | Name     | Function                           |
| --------- | -------- | ---------------------------------- |
| in        | position | Index 0~4 ranked Temperature data. |
| return    |          | Ranked Temperature value.          |

**float RAK_D7S::getInstantaneusSI() **

Get instantaneus SI (during an earthquake) [m/s].

#### Parameters:

| Direction | Name | Function               |
| --------- | ---- | ---------------------- |
| return    |      | Instantaneus SI value. |

**float RAK_D7S::getInstantaneusPGA()  **

Get instantaneus PGA (during an earthquake) [m/s^2].

#### Parameters:

| Direction | Name | Function                |
| --------- | ---- | ----------------------- |
| return    |      | Instantaneus PGA value. |

**void RAK_D7S::clearEarthquakeData()  **

Delete both the lastest data and the ranked data.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::clearInstallationData()  **

Delete initializzazion data.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::clearLastestOffsetData()  **

Delete offset data.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::clearSelftestData()   **

Delete selftest data.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::clearAllData()  **

Delete all data.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::initialize()   **

Initialize the D7S (start the initial installation mode).

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**void RAK_D7S::selftest()    **

Start autodiagnostic and resturn the result (OK/ERROR).

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none.    |

**D7S_mode_status_t RAK_D7S::getSelftestResult()  **

Return the result of self-diagnostic test (OK/ERROR).

#### Parameters:

| Direction | Name | Function                |
| --------- | ---- | ----------------------- |
| return    |      | 0 : OK.<br />1 : ERROR. |

**void RAK_D7S::acquireOffset()   **

Start offset acquisition and return the rersult (OK/ERROR).

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none     |

**D7S_mode_status_t RAK_D7S::getAcquireOffsetResult()**

Return the result of offset acquisition test (OK/ERROR).

#### Parameters:

| Direction | Name | Function                |
| --------- | ---- | ----------------------- |
| return    |      | 0 : OK.<br />1 : ERROR. |

**uint8_t RAK_D7S::isInCollapse() **

After each earthquakes it's important to reset the events calling resetEvents() to prevent polluting the new data with the old one Return true if the collapse condition is met (it's the sencond bit of _events).

#### Parameters:

| Direction | Name | Function                          |
| --------- | ---- | --------------------------------- |
| return    |      | Return the second bit of _events. |

**uint8_t RAK_D7S::isInShutoff()**

Return true if the shutoff condition is met (it's the first bit of _events).

#### Parameters:

| Direction | Name | Function                          |
| --------- | ---- | --------------------------------- |
| return    |      | Return the second bit of _events. |

**void RAK_D7S::resetEvents()**

Reset shutoff/collapse events.

#### Parameters:

| Direction | Name | Function |
| --------- | ---- | -------- |
| return    |      | none     |

**uint8_t RAK_D7S::isEarthquakeOccuring() **

Return true if an earthquake is occuring.

#### Parameters:

| Direction | Name | Function                                                   |
| --------- | ---- | ---------------------------------------------------------- |
| return    |      | true : Earthquake is occuring.<br />false : no earthquake. |

**bool RAK_D7S::isReady() **

Ready state.

#### Parameters:

| Direction | Name | Function                                      |
| --------- | ---- | --------------------------------------------- |
| return    |      | true : D7S ready.<br />false : D7S not ready. |

