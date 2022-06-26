#include "Channels.h"
#include "CryptoEngine.h"
#include "NodeDB.h"
#include "configuration.h"

#include <assert.h>

/// 16 bytes of random PSK for our _public_ default channel that all devices power up on (AES128)
static const uint8_t defaultpsk[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

Channels channels;

const char *Channels::adminChannel = "admin";
const char *Channels::gpioChannel = "gpio";
const char *Channels::serialChannel = "serial";

uint8_t xorHash(const uint8_t *p, size_t len)
{
    uint8_t code = 0;
    for (size_t i = 0; i < len; i++)
        code ^= p[i];
    return code;
}

/** Given a channel number, return the (0 to 255) hash for that channel.
 * The hash is just an xor of the channel name followed by the channel PSK being used for encryption
 * If no suitable channel could be found, return -1
 */
int16_t Channels::generateHash(ChannelIndex channelNum)
{
    auto k = getKey(channelNum);
    if (k.length < 0)
        return -1; // invalid
    else {
        const char *name = getName(channelNum);
        uint8_t h = xorHash((const uint8_t *)name, strlen(name));

        h ^= xorHash(k.bytes, k.length);

        return h;
    }
}

/**
 * Validate a channel, fixing any errors as needed
 */
Channel &Channels::fixupChannel(ChannelIndex chIndex)
{
    Channel &ch = getByIndex(chIndex);

    ch.index = chIndex; // Preinit the index so it be ready to share with the phone (we'll never change it later)

    if (!ch.has_settings) {
        // No settings! Must disable and skip
        ch.role = Channel_Role_DISABLED;
        memset(&ch.settings, 0, sizeof(ch.settings));
        ch.has_settings = true;
    } else {
        ChannelSettings &channelSettings = ch.settings;

        // Convert the old string "Default" to our new short representation
        if (strcmp(channelSettings.name, "Default") == 0)
            *channelSettings.name = '\0';

        /* Convert any old usage of the defaultpsk into our new short representation.
        if (channelSettings.psk.size == sizeof(defaultpsk) &&
            memcmp(channelSettings.psk.bytes, defaultpsk, sizeof(defaultpsk)) == 0) {
            *channelSettings.psk.bytes = 1;
            channelSettings.psk.size = 1;
        } */
    }

    hashes[chIndex] = generateHash(chIndex);

    return ch;
}

/**
 * Write a default channel to the specified channel index
 */
void Channels::initDefaultChannel(ChannelIndex chIndex)
{
    Channel &ch = getByIndex(chIndex);
    ChannelSettings &channelSettings = ch.settings;
    Config_LoRaConfig &loraConfig = config.lora;

    loraConfig.modem_preset = Config_LoRaConfig_ModemPreset_LongFast; // Default to Long Range & Fast

    loraConfig.tx_power = 0; // default
    uint8_t defaultpskIndex = 1;
    channelSettings.psk.bytes[0] = defaultpskIndex;
    channelSettings.psk.size = 1;
    strcpy(channelSettings.name, "");

    ch.has_settings = true;
    ch.role = Channel_Role_PRIMARY;
}

CryptoKey Channels::getKey(ChannelIndex chIndex)
{
    Channel &ch = getByIndex(chIndex);
    const ChannelSettings &channelSettings = ch.settings;
    assert(ch.has_settings);

    CryptoKey k;
    memset(k.bytes, 0, sizeof(k.bytes)); // In case the user provided a short key, we want to pad the rest with zeros

    if (ch.role == Channel_Role_DISABLED) {
        k.length = -1; // invalid
    } else {
        memcpy(k.bytes, channelSettings.psk.bytes, channelSettings.psk.size);
        k.length = channelSettings.psk.size;
        if (k.length == 0) {
            if (ch.role == Channel_Role_SECONDARY) {
                DEBUG_MSG("Unset PSK for secondary channel %s. using primary key\n", ch.settings.name);
                k = getKey(primaryIndex);
            } else
                DEBUG_MSG("Warning: User disabled encryption\n");
        } else if (k.length == 1) {
            // Convert the short single byte variants of psk into variant that can be used more generally

            uint8_t pskIndex = k.bytes[0];
            DEBUG_MSG("Expanding short PSK #%d\n", pskIndex);
            if (pskIndex == 0)
                k.length = 0; // Turn off encryption
            else {
                memcpy(k.bytes, defaultpsk, sizeof(defaultpsk));
                k.length = sizeof(defaultpsk);
                // Bump up the last byte of PSK as needed
                uint8_t *last = k.bytes + sizeof(defaultpsk) - 1;
                *last = *last + pskIndex - 1; // index of 1 means no change vs defaultPSK
            }
        } else if (k.length < 16) {
            // Error! The user specified only the first few bits of an AES128 key.  So by convention we just pad the rest of the
            // key with zeros
            DEBUG_MSG("Warning: User provided a too short AES128 key - padding\n");
            k.length = 16;
        } else if (k.length < 32 && k.length != 16) {
            // Error! The user specified only the first few bits of an AES256 key.  So by convention we just pad the rest of the
            // key with zeros
            DEBUG_MSG("Warning: User provided a too short AES256 key - padding\n");
            k.length = 32;
        }
    }

    return k;
}

/** Given a channel index, change to use the crypto key specified by that index
 */
int16_t Channels::setCrypto(ChannelIndex chIndex)
{
    CryptoKey k = getKey(chIndex);

    if (k.length < 0)
        return -1;
    else {
        // Tell our crypto engine about the psk
        crypto->setKey(k);
        return getHash(chIndex);
    }
}

void Channels::initDefaults()
{
    channelFile.channels_count = MAX_NUM_CHANNELS;
    for (int i = 0; i < channelFile.channels_count; i++)
        fixupChannel(i);
    initDefaultChannel(0);
}

void Channels::onConfigChanged()
{
    // Make sure the phone hasn't mucked anything up
    for (int i = 0; i < channelFile.channels_count; i++) {
        Channel &ch = fixupChannel(i);

        if (ch.role == Channel_Role_PRIMARY)
            primaryIndex = i;
    }
}

Channel &Channels::getByIndex(ChannelIndex chIndex)
{
    assert(chIndex < channelFile.channels_count); // This should be equal to MAX_NUM_CHANNELS
    Channel *ch = channelFile.channels + chIndex;
    return *ch;
}

void Channels::setChannel(const Channel &c)
{
    Channel &old = getByIndex(c.index);

    // if this is the new primary, demote any existing roles
    if (c.role == Channel_Role_PRIMARY)
        for (int i = 0; i < getNumChannels(); i++)
            if (channelFile.channels[i].role == Channel_Role_PRIMARY)
                channelFile.channels[i].role = Channel_Role_SECONDARY;

    old = c; // slam in the new settings/role
}

const char *Channels::getName(size_t chIndex)
{
    // Convert the short "" representation for Default into a usable string
    const ChannelSettings &channelSettings = getByIndex(chIndex).settings;
    const char *channelName = channelSettings.name;
    if (!*channelName) { // emptystring
        // Per mesh.proto spec, if bandwidth is specified we must ignore modemPreset enum, we assume that in that case
        // the app fucked up and forgot to set channelSettings.name

        if (config.lora.bandwidth != 0)
            channelName = "Custom";
        else
            switch (config.lora.modem_preset) {
            case Config_LoRaConfig_ModemPreset_ShortSlow:
                channelName = "ShortS";
                break;
            case Config_LoRaConfig_ModemPreset_ShortFast:
                channelName = "ShortF";
                break;
            case Config_LoRaConfig_ModemPreset_MedSlow:
                channelName = "MedS";
                break;
            case Config_LoRaConfig_ModemPreset_MedFast:
                channelName = "MedF";
                break;
            case Config_LoRaConfig_ModemPreset_LongSlow:
                channelName = "LongS";
                break;
            case Config_LoRaConfig_ModemPreset_LongFast:
                channelName = "LongF";
                break;
            case Config_LoRaConfig_ModemPreset_VLongSlow:
                channelName = "VeryL";
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
*
* This function will also need to be implemented in GUI apps that talk to the radio.
*
* https://github.com/meshtastic/Meshtastic-device/issues/269
*/
const char *Channels::getPrimaryName()
{
    static char buf[32];

    char suffix;
    // auto channelSettings = getPrimary();
    // if (channelSettings.psk.size != 1) {
    // We have a standard PSK, so generate a letter based hash.
    uint8_t code = getHash(primaryIndex);

    suffix = 'A' + (code % 26);
    /* } else {
        suffix = '0' + channelSettings.psk.bytes[0];
    } */

    snprintf(buf, sizeof(buf), "#%s-%c", getName(primaryIndex), suffix);
    return buf;
}

/** Given a channel hash setup crypto for decoding that channel (or the primary channel if that channel is unsecured)
 *
 * This method is called before decoding inbound packets
 *
 * @return false if the channel hash or channel is invalid
 */
bool Channels::decryptForHash(ChannelIndex chIndex, ChannelHash channelHash)
{
    if (chIndex > getNumChannels() || getHash(chIndex) != channelHash) {
        // DEBUG_MSG("Skipping channel %d (hash %x) due to invalid hash/index, want=%x\n", chIndex, getHash(chIndex),
        // channelHash);
        return false;
    } else {
        DEBUG_MSG("Using channel %d (hash 0x%x)\n", chIndex, channelHash);
        setCrypto(chIndex);
        return true;
    }
}

/** Given a channel index setup crypto for encoding that channel (or the primary channel if that channel is unsecured)
 *
 * This method is called before encoding outbound packets
 *
 * @eturn the (0 to 255) hash for that channel - if no suitable channel could be found, return -1
 */
int16_t Channels::setActiveByIndex(ChannelIndex channelIndex)
{
    return setCrypto(channelIndex);
}
