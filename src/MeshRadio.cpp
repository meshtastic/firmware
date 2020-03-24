#include "RH_RF95.h"
#include <RHMesh.h>
#include <SPI.h>
#include <assert.h>

#include "MeshRadio.h"
#include "NodeDB.h"
#include "configuration.h"
#include <pb_decode.h>
#include <pb_encode.h>

/// 16 bytes of random PSK for our _public_ default channel that all devices power up on
static const uint8_t defaultpsk[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0xbf};

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

/// Sometimes while debugging it is useful to set this false, to disable rf95 accesses
bool useHardware = true;

MeshRadio::MeshRadio(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest) : rf95(_pool, _rxDest), manager(rf95)
{
    myNodeInfo.num_channels = NUM_CHANNELS;

    // radioConfig.modem_config = RadioConfig_ModemConfig_Bw125Cr45Sf128;  // medium range and fast
    // channelSettings.modem_config = ChannelSettings_ModemConfig_Bw500Cr45Sf128;  // short range and fast, but wide bandwidth so
    // incompatible radios can talk together
    channelSettings.modem_config = ChannelSettings_ModemConfig_Bw125Cr48Sf4096; // slow and long range

    channelSettings.tx_power = 23;
    memcpy(&channelSettings.psk, &defaultpsk, sizeof(channelSettings.psk));
    strcpy(channelSettings.name, "Default");
    // Can't print strings this early - serial not setup yet
    // DEBUG_MSG("Set meshradio defaults name=%s\n", channelSettings.name);
}

bool MeshRadio::init()
{
    if (!useHardware)
        return true;

    DEBUG_MSG("Starting meshradio init...\n");

#ifdef RESET_GPIO
    pinMode(RESET_GPIO, OUTPUT); // Deassert reset
    digitalWrite(RESET_GPIO, HIGH);

    // pulse reset
    digitalWrite(RESET_GPIO, LOW);
    delay(10);
    digitalWrite(RESET_GPIO, HIGH);
    delay(10);
#endif

    manager.setThisAddress(
        nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor time.

    if (!manager.init()) {
        DEBUG_MSG("LoRa radio init failed\n");
        DEBUG_MSG("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info\n");
        return false;
    }

    // not needed - defaults on
    // rf95.setPayloadCRC(true);

    reloadConfig();

    return true;
}

/** hash a string into an integer
 *
 * djb2 by Dan Bernstein.
 * http://www.cse.yorku.ca/~oz/hash.html
 */
unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = *str++) != 0)
        hash = ((hash << 5) + hash) + (unsigned char)c; /* hash * 33 + c */

    return hash;
}

void MeshRadio::reloadConfig()
{
    rf95.setModeIdle(); // Need to be idle before doing init

    // Set up default configuration
    // No Sync Words in LORA mode.
    rf95.setModemConfig(
        (RH_RF95::ModemConfigChoice)channelSettings.modem_config); // Radio default
                                                                   //    setModemConfig(Bw125Cr48Sf4096); // slow and reliable?
    // rf95.setPreambleLength(8);           // Default is 8

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
    int channel_num = hash(channelSettings.name) % NUM_CHANNELS;
    float center_freq = CH0 + CH_SPACING * channel_num;
    if (!rf95.setFrequency(center_freq)) {
        DEBUG_MSG("setFrequency failed\n");
        assert(0); // fixme panic
    }

    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

    // The default transmitter power is 13dBm, using PA_BOOST.
    // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
    // you can set transmitter powers from 5 to 23 dBm:
    // FIXME - can we do this?  It seems to be in the Heltec board.
    rf95.setTxPower(channelSettings.tx_power, false);

    DEBUG_MSG("Set radio: name=%s. config=%u, ch=%d, txpower=%d\n", channelSettings.name, channelSettings.modem_config,
              channel_num, channelSettings.tx_power);

    // Done with init tell radio to start receiving
    rf95.setModeRx();
}

ErrorCode MeshRadio::send(MeshPacket *p)
{
    lastTxStart = millis();

    if (useHardware)
        return rf95.send(p);
    else {
        rf95.pool.release(p);
        return ERRNO_OK;
    }
}

#define TX_WATCHDOG_TIMEOUT 30 * 1000

void MeshRadio::loop()
{
    // It should never take us more than 30 secs to send a packet, if it does, we have a bug
    uint32_t now = millis();
    if (lastTxStart != 0 && (now - lastTxStart) > TX_WATCHDOG_TIMEOUT && rf95.mode() == RHGenericDriver::RHModeTx) {
        DEBUG_MSG("ERROR! Bug! Tx packet took too long to send, forcing radio into rx mode");
        rf95.setModeRx();
        lastTxStart = 0; // Stop checking for now, because we just warned the developer
    }
}
