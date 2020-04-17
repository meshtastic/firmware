// RHNRFSPIDriver.h
// Author: Mike McCauley (mikem@airspayce.com)
// Copyright (C) 2014 Mike McCauley
// $Id: RHNRFSPIDriver.h,v 1.5 2017/11/06 00:04:08 mikem Exp $

#ifndef RHNRFSPIDriver_h
#define RHNRFSPIDriver_h

#include <RHGenericDriver.h>
#include <RHHardwareSPI.h>

class RHGenericSPI;

/////////////////////////////////////////////////////////////////////
/// \class RHNRFSPIDriver RHNRFSPIDriver.h <RHNRFSPIDriver.h>
/// \brief Base class for RadioHead drivers that use the SPI bus
/// to communicate with its NRF family transport hardware.
///
/// This class can be subclassed by Drivers that require to use the SPI bus.
/// It can be configured to use either the RHHardwareSPI class (if there is one available on the platform)
/// of the bitbanged RHSoftwareSPI class. The dfault behaviour is to use a pre-instantiated built-in RHHardwareSPI
/// interface.
/// 
/// SPI bus access is protected by ATOMIC_BLOCK_START and ATOMIC_BLOCK_END, which will ensure interrupts 
/// are disabled during access.
/// 
/// The read and write routines use SPI conventions as used by Nordic NRF radios and otehr devices, 
/// but these can be overriden 
/// in subclasses if necessary.
///
/// Application developers are not expected to instantiate this class directly: 
/// it is for the use of Driver developers.
class RHNRFSPIDriver : public RHGenericDriver
{
public:
    /// Constructor
    /// \param[in] slaveSelectPin The controller pin to use to select the desired SPI device. This pin will be driven LOW
    /// during SPI communications with the SPI device that uis iused by this Driver.
    /// \param[in] spi Reference to the SPI interface to use. The default is to use a default built-in Hardware interface.
    RHNRFSPIDriver(uint8_t slaveSelectPin = SS, RHGenericSPI& spi = hardware_spi);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    bool init();

    /// Sends a single command to the device
    /// \param[in] command The command code to send to the device.
    /// \return Some devices return a status byte during the first data transfer. This byte is returned.
    ///  it may or may not be meaningfule depending on the the type of device being accessed.
    uint8_t spiCommand(uint8_t command);

    /// Reads a single register from the SPI device
    /// \param[in] reg Register number
    /// \return The value of the register
    uint8_t        spiRead(uint8_t reg);

    /// Writes a single byte to the SPI device
    /// \param[in] reg Register number
    /// \param[in] val The value to write
    /// \return Some devices return a status byte during the first data transfer. This byte is returned.
    ///  it may or may not be meaningfule depending on the the type of device being accessed.
    uint8_t           spiWrite(uint8_t reg, uint8_t val);

    /// Reads a number of consecutive registers from the SPI device using burst read mode
    /// \param[in] reg Register number of the first register
    /// \param[in] dest Array to write the register values to. Must be at least len bytes
    /// \param[in] len Number of bytes to read
    /// \return Some devices return a status byte during the first data transfer. This byte is returned.
    ///  it may or may not be meaningfule depending on the the type of device being accessed.
    uint8_t           spiBurstRead(uint8_t reg, uint8_t* dest, uint8_t len);

    /// Write a number of consecutive registers using burst write mode
    /// \param[in] reg Register number of the first register
    /// \param[in] src Array of new register values to write. Must be at least len bytes
    /// \param[in] len Number of bytes to write
    /// \return Some devices return a status byte during the first data transfer. This byte is returned.
    ///  it may or may not be meaningfule depending on the the type of device being accessed.
    uint8_t           spiBurstWrite(uint8_t reg, const uint8_t* src, uint8_t len);

    /// Set or change the pin to be used for SPI slave select.
    /// This can be called at any time to change the
    /// pin that will be used for slave select in subsquent SPI operations.
    /// \param[in] slaveSelectPin The pin to use
    void setSlaveSelectPin(uint8_t slaveSelectPin);

    /// Set the SPI interrupt number
    /// If SPI transactions can occur within an interrupt, tell the low level SPI
    /// interface which interrupt is used
    /// \param[in] interruptNumber the interrupt number
    void spiUsingInterrupt(uint8_t interruptNumber);

protected:
    /// Reference to the RHGenericSPI instance to use to trasnfer data with teh SPI device
    RHGenericSPI&       _spi;

    /// The pin number of the Slave Select pin that is used to select the desired device.
    uint8_t             _slaveSelectPin;
};

#endif
