#include "Channels.h"
#include "NodeDB.h"
#include "CryptoEngine.h"

#include <assert.h>

/// A usable psk - which has been constructed based on the (possibly short psk) in channelSettings
static uint8_t activePSK[32];
static uint8_t activePSKSize;

/// 16 bytes of random PSK for our _public_ default channel that all devices power up on (AES128)
static const uint8_t defaultpsk[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0xbf};

Channels channels;

/**
 * Validate a channel, fixing any errors as needed
 */
Channel &fixupChannel(size_t chIndex)
{
    assert(chIndex < devicestate.channels_count);

    Channel *ch = devicestate.channels + chIndex;

    ch->index = chIndex; // Preinit the index so it be ready to share with the phone (we'll never change it later)

    if (!ch->has_settings) {
        // No settings! Must disable and skip
        ch->role = Channel_Role_DISABLED;
    } else {
        ChannelSettings &channelSettings = ch->settings;

        // Convert the old string "Default" to our new short representation
        if (strcmp(channelSettings.name, "Default") == 0)
            *channelSettings.name = '\0';

        // Convert any old usage of the defaultpsk into our new short representation.
        if (channelSettings.psk.size == sizeof(defaultpsk) &&
            memcmp(channelSettings.psk.bytes, defaultpsk, sizeof(defaultpsk)) == 0) {
            *channelSettings.psk.bytes = 1;
            channelSettings.psk.size = 1;
        }
    }

    return *ch;
}



/**
 * Write a default channel to the specified channel index
 */
void initDefaultChannel(size_t chIndex)
{
    assert(chIndex < devicestate.channels_count);
    Channel *ch = devicestate.channels + chIndex;
    ChannelSettings &channelSettings = ch->settings;

    // radioConfig.modem_config = RadioConfig_ModemConfig_Bw125Cr45Sf128;  // medium range and fast
    // channelSettings.modem_config = ChannelSettings_ModemConfig_Bw500Cr45Sf128;  // short range and fast, but wide
    // bandwidth so incompatible radios can talk together
    channelSettings.modem_config = ChannelSettings_ModemConfig_Bw125Cr48Sf4096; // slow and long range

    channelSettings.tx_power = 0; // default
    uint8_t defaultpskIndex = 1;
    channelSettings.psk.bytes[0] = defaultpskIndex;
    channelSettings.psk.size = 1;
    strcpy(channelSettings.name, "");

    ch->has_settings = true;
    ch->role = Channel_Role_PRIMARY;
}

/** Given a channel index, change to use the crypto key specified by that index
 */
void setCrypto(size_t chIndex)
{

    assert(chIndex < devicestate.channels_count);
    Channel *ch = devicestate.channels + chIndex;
    ChannelSettings &channelSettings = ch->settings;

    memset(activePSK, 0, sizeof(activePSK)); // In case the user provided a short key, we want to pad the rest with zeros
    memcpy(activePSK, channelSettings.psk.bytes, channelSettings.psk.size);
    activePSKSize = channelSettings.psk.size;
    if (activePSKSize == 0)
        DEBUG_MSG("Warning: User disabled encryption\n");
    else if (activePSKSize == 1) {
        // Convert the short single byte variants of psk into variant that can be used more generally

        uint8_t pskIndex = activePSK[0];
        DEBUG_MSG("Expanding short PSK #%d\n", pskIndex);
        if (pskIndex == 0)
            activePSKSize = 0; // Turn off encryption
        else {
            memcpy(activePSK, defaultpsk, sizeof(defaultpsk));
            activePSKSize = sizeof(defaultpsk);
            // Bump up the last byte of PSK as needed
            uint8_t *last = activePSK + sizeof(defaultpsk) - 1;
            *last = *last + pskIndex - 1; // index of 1 means no change vs defaultPSK
        }
    } else if (activePSKSize < 16) {
        // Error! The user specified only the first few bits of an AES128 key.  So by convention we just pad the rest of the key
        // with zeros
        DEBUG_MSG("Warning: User provided a too short AES128 key - padding\n");
        activePSKSize = 16;
    } else if (activePSKSize < 32 && activePSKSize != 16) {
        // Error! The user specified only the first few bits of an AES256 key.  So by convention we just pad the rest of the key
        // with zeros
        DEBUG_MSG("Warning: User provided a too short AES256 key - padding\n");
        activePSKSize = 32;
    }

    // Tell our crypto engine about the psk
    crypto->setKey(activePSKSize, activePSK);
}

void Channels::initDefaults()
{
    devicestate.channels_count = MAX_CHANNELS;
    for (int i = 0; i < devicestate.channels_count; i++)
        fixupChannel(i);
    initDefaultChannel(0);
}

void Channels::onConfigChanged()
{
    // Make sure the phone hasn't mucked anything up
    for (int i = 0; i < devicestate.channels_count; i++) {
        auto ch = fixupChannel(i);

        if(ch.role == Channel_Role_PRIMARY)
            primaryIndex = i;
    }

    setCrypto(0); // FIXME: for the time being (still single channel - just use our only channel as the crypto key)
}

Channel &Channels::getChannel(size_t chIndex)
{
    assert(chIndex < devicestate.channels_count);
    Channel *ch = devicestate.channels + chIndex;
    return *ch;
}

void Channels::setChannel(const Channel &c) {
    Channel &old = getChannel(c.index);

    // if this is the new primary, demote any existing roles
    if(c.role == Channel_Role_PRIMARY)
        for (int i = 0; i < devicestate.channels_count; i++) 
            if(devicestate.channels[i].role == Channel_Role_PRIMARY)
                devicestate.channels[i].role = Channel_Role_SECONDARY;

    old = c; // slam in the new settings/role
}

const char *Channels::getName(size_t chIndex)
{
    // Convert the short "" representation for Default into a usable string
    ChannelSettings &channelSettings = getChannel(chIndex).settings;
    const char *channelName = channelSettings.name;
    if (!*channelName) { // emptystring
        // Per mesh.proto spec, if bandwidth is specified we must ignore modemConfig enum, we assume that in that case
        // the app fucked up and forgot to set channelSettings.name

        if (channelSettings.bandwidth != 0)
            channelName = "Unset";
        else
            switch (channelSettings.modem_config) {
            case ChannelSettings_ModemConfig_Bw125Cr45Sf128:
                channelName = "Medium";
                break;
            case ChannelSettings_ModemConfig_Bw500Cr45Sf128:
                channelName = "ShortFast";
                break;
            case ChannelSettings_ModemConfig_Bw31_25Cr48Sf512:
                channelName = "LongAlt";
                break;
            case ChannelSettings_ModemConfig_Bw125Cr48Sf4096:
                channelName = "LongSlow";
                break;
            default:
                channelName = "Invalid";
                break;
            }
    }

    return channelName;
}

/**
* Generate a short suffix used to disambiguate channels that might have the same "name" entered by the human but different PSKs.
* The ideas is that the PSK changing should be visible to the user so that they see they probably messed up and that's why they
their nodes
* aren't talking to each other.
*
* This string is of the form "#name-X".
*
* Where X is either:
* (for custom PSKS) a letter from A to Z (base26), and formed by xoring all the bytes of the PSK together,
* OR (for the standard minimially secure PSKs) a number from 0 to 9.
*
* This function will also need to be implemented in GUI apps that talk to the radio.
*
* https://github.com/meshtastic/Meshtastic-device/issues/269
*/
const char *Channels::getPrimaryName()
{
    static char buf[32];

    char suffix;
    auto channelSettings = getPrimary();
    if (channelSettings.psk.size != 1) {
        // We have a standard PSK, so generate a letter based hash.
        uint8_t code = 0;
        for (int i = 0; i < activePSKSize; i++)
            code ^= activePSK[i];

        suffix = 'A' + (code % 26);
    } else {
        suffix = '0' + channelSettings.psk.bytes[0];
    }

    snprintf(buf, sizeof(buf), "#%s-%c", channelSettings.name, suffix);
    return buf;
}