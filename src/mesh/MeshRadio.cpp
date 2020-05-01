#include "error.h"
#include <SPI.h>
#include <assert.h>

#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "sleep.h"
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

MeshRadio::MeshRadio(RadioInterface *rIf) : radioIf(*rIf) // , manager(radioIf)
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

    configChangedObserver.observe(&service.configChanged);
    preflightSleepObserver.observe(&preflightSleep);
    notifyDeepSleepObserver.observe(&notifyDeepSleep);

#ifdef RESET_GPIO
    pinMode(RESET_GPIO, OUTPUT); // Deassert reset
    digitalWrite(RESET_GPIO, HIGH);

    // pulse reset
    digitalWrite(RESET_GPIO, LOW);
    delay(10);
    digitalWrite(RESET_GPIO, HIGH);
    delay(10);
#endif

    // we now expect interfaces to operate in promiscous mode
    // radioIf.setThisAddress(nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor
    // time.

    applySettings();

    if (!radioIf.init()) {
        DEBUG_MSG("LoRa radio init failed\n");
        return false;
    }

    // No need to call this now, init is supposed to do same.  reloadConfig();

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

/**
 * Pull our channel settings etc... from protobufs to the dumb interface settings
 */
void MeshRadio::applySettings()
{
    // Set up default configuration
    // No Sync Words in LORA mode.
    radioIf.modemConfig = (ModemConfigChoice)channelSettings.modem_config;

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
    int channel_num = hash(channelSettings.name) % NUM_CHANNELS;
    radioIf.freq = CH0 + CH_SPACING * channel_num;
    radioIf.power = channelSettings.tx_power;

    DEBUG_MSG("Set radio: name=%s, config=%u, ch=%d, txpower=%d\n", channelSettings.name, channelSettings.modem_config,
              channel_num, channelSettings.tx_power);
}

int MeshRadio::reloadConfig(void *unused)
{
    applySettings();
    radioIf.reconfigure();

    return 0;
}
