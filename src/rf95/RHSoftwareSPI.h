// SoftwareSPI.h
// Author: Chris Lapa (chris@lapa.com.au)
// Copyright (C) 2014 Chris Lapa
// Contributed by Chris Lapa

#ifndef RHSoftwareSPI_h
#define RHSoftwareSPI_h

#include <RHGenericSPI.h>

/////////////////////////////////////////////////////////////////////
/// \class RHSoftwareSPI RHSoftwareSPI.h <RHSoftwareSPI.h>
/// \brief Encapsulate a software SPI interface
///
/// This concrete subclass of RHGenericSPI enapsulates a bit-banged software SPI interface.
/// Caution: this software SPI interface will be much slower than hardware SPI on most
/// platforms.
///
/// SPI transactions are not supported, and associated functions do nothing.
///
/// \par Usage
///
/// Usage varies slightly depending on what driver you are using.
///
/// For RF22, for example:
/// \code
/// #include <RHSoftwareSPI.h>
/// RHSoftwareSPI spi;
/// RH_RF22 driver(SS, 2, spi);
/// RHReliableDatagram(driver, CLIENT_ADDRESS);
/// void setup()
/// {
///    spi.setPins(6, 5, 7); // Or whatever SPI pins you need
///    ....
/// }
/// \endcode
class RHSoftwareSPI : public RHGenericSPI 
{
public:

    /// Constructor
    /// Creates an instance of a bit-banged software SPI interface.
    /// Sets the SPI pins to the defaults of 
    /// MISO = 12, MOSI = 11, SCK = 13. If you need other assigments, call setPins() before
    /// calling manager.init() or driver.init().
    /// \param[in] frequency One of RHGenericSPI::Frequency to select the SPI bus frequency. The frequency
    /// is mapped to the closest available bus frequency on the platform. CAUTION: the achieved
    /// frequency will almost certainly be very much slower on most platforms. eg on Arduino Uno, the
    /// the clock rate is likely to be at best around 46kHz.
    /// \param[in] bitOrder Select the SPI bus bit order, one of RHGenericSPI::BitOrderMSBFirst or 
    /// RHGenericSPI::BitOrderLSBFirst.
    /// \param[in] dataMode Selects the SPI bus data mode. One of RHGenericSPI::DataMode
    RHSoftwareSPI(Frequency frequency = Frequency1MHz, BitOrder bitOrder = BitOrderMSBFirst, DataMode dataMode = DataMode0);

    /// Transfer a single octet to and from the SPI interface
    /// \param[in] data The octet to send
    /// \return The octet read from SPI while the data octet was sent.
    uint8_t transfer(uint8_t data);

    /// Initialise the software SPI library
    /// Call this after configuring the SPI interface and before using it to transfer data.
    /// Initializes the SPI bus by setting SCK, MOSI, and SS to outputs, pulling SCK and MOSI low, and SS high. 
    void begin();

    /// Disables the SPI bus usually, in this case
    /// there is no hardware controller to disable.
    void end();

    /// Sets the pins used by this SoftwareSPIClass instance.
    /// The defaults are: MISO = 12, MOSI = 11, SCK = 13.
    /// \param[in] miso master in slave out pin used
    /// \param[in] mosi master out slave in pin used
    /// \param[in] sck clock pin used
    void setPins(uint8_t miso = 12, uint8_t mosi = 11, uint8_t sck = 13);

private:

    /// Delay routine for bus timing.
    void delayPeriod();

private:
    uint8_t _miso;
    uint8_t _mosi;
    uint8_t _sck;
    uint8_t _delayCounts;
    uint8_t _clockPolarity;
    uint8_t _clockPhase;
};

#endif
