// RHGenericSPI.h
// Author: Mike McCauley (mikem@airspayce.com)
// Copyright (C) 2011 Mike McCauley
// Contributed by Joanna Rutkowska
// $Id: RHGenericSPI.h,v 1.9 2020/01/05 07:02:23 mikem Exp mikem $

#ifndef RHGenericSPI_h
#define RHGenericSPI_h

#include <RadioHead.h>

/////////////////////////////////////////////////////////////////////
/// \class RHGenericSPI RHGenericSPI.h <RHGenericSPI.h>
/// \brief Base class for SPI interfaces
///
/// This generic abstract class is used to encapsulate hardware or software SPI interfaces for 
/// a variety of platforms.
/// The intention is so that driver classes can be configured to use hardware or software SPI
/// without changing the main code.
///
/// You must provide a subclass of this class to driver constructors that require SPI.
/// A concrete subclass that encapsualates the standard Arduino hardware SPI and a bit-banged
/// software implementation is included.
///
/// Do not directly use this class: it must be subclassed and the following abstract functions at least 
/// must be implmented:
/// - begin()
/// - end() 
/// - transfer()
class RHGenericSPI 
{
public:

    /// \brief Defines constants for different SPI modes
    ///
    /// Defines constants for different SPI modes
    /// that can be passed to the constructor or setMode()
    /// We need to define these in a device and platform independent way, because the
    /// SPI implementation is different on each platform.
    typedef enum
    {
	DataMode0 = 0, ///< SPI Mode 0: CPOL = 0, CPHA = 0
	DataMode1,     ///< SPI Mode 1: CPOL = 0, CPHA = 1
	DataMode2,     ///< SPI Mode 2: CPOL = 1, CPHA = 0
	DataMode3,     ///< SPI Mode 3: CPOL = 1, CPHA = 1
    } DataMode;

    /// \brief Defines constants for different SPI bus frequencies
    ///
    /// Defines constants for different SPI bus frequencies
    /// that can be passed to setFrequency().
    /// The frequency you get may not be exactly the one according to the name.
    /// We need to define these in a device and platform independent way, because the
    /// SPI implementation is different on each platform.
    typedef enum
    {
	Frequency1MHz = 0,  ///< SPI bus frequency close to 1MHz
	Frequency2MHz,      ///< SPI bus frequency close to 2MHz
	Frequency4MHz,      ///< SPI bus frequency close to 4MHz
	Frequency8MHz,      ///< SPI bus frequency close to 8MHz
	Frequency16MHz      ///< SPI bus frequency close to 16MHz
    } Frequency;

    /// \brief Defines constants for different SPI endianness
    ///
    /// Defines constants for different SPI endianness
    /// that can be passed to setBitOrder()
    /// We need to define these in a device and platform independent way, because the
    /// SPI implementation is different on each platform.
    typedef enum
    {
	BitOrderMSBFirst = 0,  ///< SPI MSB first
	BitOrderLSBFirst,      ///< SPI LSB first
    } BitOrder;

    /// Constructor
    /// Creates an instance of an abstract SPI interface.
    /// Do not use this contructor directly: you must instead use on of the concrete subclasses provided 
    /// such as RHHardwareSPI or RHSoftwareSPI
    /// \param[in] frequency One of RHGenericSPI::Frequency to select the SPI bus frequency. The frequency
    /// is mapped to the closest available bus frequency on the platform.
    /// \param[in] bitOrder Select the SPI bus bit order, one of RHGenericSPI::BitOrderMSBFirst or 
    /// RHGenericSPI::BitOrderLSBFirst.
    /// \param[in] dataMode Selects the SPI bus data mode. One of RHGenericSPI::DataMode
    RHGenericSPI(Frequency frequency = Frequency1MHz, BitOrder bitOrder = BitOrderMSBFirst, DataMode dataMode = DataMode0);

    /// Transfer a single octet to and from the SPI interface
    /// \param[in] data The octet to send
    /// \return The octet read from SPI while the data octet was sent
    virtual uint8_t transfer(uint8_t data) = 0;

#if (RH_PLATFORM == RH_PLATFORM_MONGOOSE_OS)
    /// Transfer up to 2 bytes on the SPI interface
    /// \param[in] byte0 The first byte to be sent on the SPI interface
    /// \param[in] byte1 The second byte to be sent on the SPI interface
    /// \return The second byte clocked in as the second byte is sent.
    virtual uint8_t transfer2B(uint8_t byte0, uint8_t byte1) = 0;

    /// Read a number of bytes on the SPI interface from an NRF device
    /// \param[in] reg The NRF device register to read
    /// \param[out] dest The buffer to hold the bytes read
    /// \param[in] len The number of bytes to read
    /// \return The NRF status byte
    virtual uint8_t spiBurstRead(uint8_t reg, uint8_t* dest, uint8_t len) = 0;

    /// Wrte a number of bytes on the SPI interface to an NRF device
    /// \param[in] reg The NRF device register to read
    /// \param[out] src The buffer to hold the bytes write
    /// \param[in] len The number of bytes to write
    /// \return The NRF status byte
    virtual uint8_t spiBurstWrite(uint8_t reg, const uint8_t* src, uint8_t len) = 0;

#endif

    /// SPI Configuration methods
    /// Enable SPI interrupts (if supported)
    /// This can be used in an SPI slave to indicate when an SPI message has been received
    virtual void attachInterrupt() {};

    /// Disable SPI interrupts (if supported)
    /// This can be used to diable the SPI interrupt in slaves where that is supported.
    virtual void detachInterrupt() {};

    /// Initialise the SPI library.
    /// Call this after configuring and before using the SPI library
    virtual void begin() = 0;

    /// Disables the SPI bus (leaving pin modes unchanged). 
    /// Call this after you have finished using the SPI interface
    virtual void end() = 0;

    /// Sets the bit order the SPI interface will use
    /// Sets the order of the bits shifted out of and into the SPI bus, either 
    /// LSBFIRST (least-significant bit first) or MSBFIRST (most-significant bit first). 
    /// \param[in] bitOrder Bit order to be used: one of RHGenericSPI::BitOrder
    virtual void setBitOrder(BitOrder bitOrder);

    /// Sets the SPI data mode: that is, clock polarity and phase. 
    /// See the Wikipedia article on SPI for details. 
    /// \param[in] dataMode The mode to use: one of RHGenericSPI::DataMode
    virtual void setDataMode(DataMode dataMode);

    /// Sets the SPI clock divider relative to the system clock. 
    /// On AVR based boards, the dividers available are 2, 4, 8, 16, 32, 64 or 128. 
    /// The default setting is SPI_CLOCK_DIV4, which sets the SPI clock to one-quarter 
    /// the frequency of the system clock (4 Mhz for the boards at 16 MHz). 
    /// \param[in] frequency The data rate to use: one of RHGenericSPI::Frequency
    virtual void setFrequency(Frequency frequency);

    /// Signal the start of an SPI transaction that must not be interrupted by other SPI actions
    /// In subclasses that support transactions this will ensure that other SPI transactions
    /// are blocked until this one is completed by endTransaction().
    /// Base does nothing
    /// Might be overridden in subclass
    virtual void beginTransaction(){}

    /// Signal the end of an SPI transaction
    /// Base does nothing
    /// Might be overridden in subclass
    virtual void endTransaction(){}

    /// Specify the interrupt number of the interrupt that will use SPI transactions
    /// Tells the SPI support software that SPI transactions will occur with the interrupt
    /// handler assocated with interruptNumber
    /// Base does nothing
    /// Might be overridden in subclass
    /// \param[in] interruptNumber The number of the interrupt
    virtual void usingInterrupt(uint8_t interruptNumber){
      (void)interruptNumber;
    }

protected:
    
    /// The configure SPI Bus frequency, one of RHGenericSPI::Frequency
    Frequency    _frequency; // Bus frequency, one of RHGenericSPI::Frequency

    /// Bit order, one of RHGenericSPI::BitOrder
    BitOrder     _bitOrder;  

    /// SPI bus mode, one of RHGenericSPI::DataMode
    DataMode     _dataMode;  
};
#endif
