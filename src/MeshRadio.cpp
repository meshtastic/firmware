#include "RH_RF95.h"
#include "error.h"
#include <RHMesh.h>
#include <SPI.h>
#include <assert.h>

#include "MeshRadio.h"
#include "NodeDB.h"
#include "configuration.h"
#include <pb_decode.h>
#include <pb_encode.h>

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

/// Sometimes while debugging it is useful to set this false, to disable rf95 accesses
bool useHardware = true;

MeshRadio::MeshRadio(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest)
    : radioIf(_pool, _rxDest) // , manager(radioIf)
{
    myNodeInfo.num_channels = NUM_CHANNELS;

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

    radioIf.setThisAddress(
        nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor time.

    if (!radioIf.init()) {
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
    radioIf.setModeIdle(); // Need to be idle before doing init

    // Set up default configuration
    // No Sync Words in LORA mode.
    radioIf.setModemConfig(
        (RH_RF95::ModemConfigChoice)channelSettings.modem_config); // Radio default
                                                                   //    setModemConfig(Bw125Cr48Sf4096); // slow and reliable?
    // rf95.setPreambleLength(8);           // Default is 8

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
    int channel_num = hash(channelSettings.name) % NUM_CHANNELS;
    float center_freq = CH0 + CH_SPACING * channel_num;
    if (!radioIf.setFrequency(center_freq)) {
        DEBUG_MSG("setFrequency failed\n");
        assert(0); // fixme panic
    }

    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

    // The default transmitter power is 13dBm, using PA_BOOST.
    // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
    // you can set transmitter powers from 5 to 23 dBm:
    // FIXME - can we do this?  It seems to be in the Heltec board.
    radioIf.setTxPower(channelSettings.tx_power, false);

    DEBUG_MSG("Set radio: name=%s, config=%u, ch=%d, txpower=%d\n", channelSettings.name, channelSettings.modem_config,
              channel_num, channelSettings.tx_power);

    // Done with init tell radio to start receiving
    radioIf.setModeRx();
}

ErrorCode MeshRadio::send(MeshPacket *p)
{
    lastTxStart = millis();

    if (useHardware)
        return radioIf.send(p);
    else {
        radioIf.pool.release(p);
        return ERRNO_OK;
    }
}

#define TX_WATCHDOG_TIMEOUT 30 * 1000

void MeshRadio::loop()
{
    // It should never take us more than 30 secs to send a packet, if it does, we have a bug, FIXME, move most of this
    // into CustomRF95
    uint32_t now = millis();
    if (lastTxStart != 0 && (now - lastTxStart) > TX_WATCHDOG_TIMEOUT && radioIf.mode() == RHGenericDriver::RHModeTx) {
        DEBUG_MSG("ERROR! Bug! Tx packet took too long to send, forcing radio into rx mode");
        radioIf.setModeRx();
        if (radioIf.sendingPacket) { // There was probably a packet we were trying to send, free it
            radioIf.pool.release(radioIf.sendingPacket);
            radioIf.sendingPacket = NULL;
        }
        recordCriticalError(ErrTxWatchdog);
        lastTxStart = 0; // Stop checking for now, because we just warned the developer
    }
}
