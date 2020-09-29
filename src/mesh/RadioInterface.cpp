
#include "RadioInterface.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "assert.h"
#include "configuration.h"
#include "sleep.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

#define RDEF(name, freq, spacing, num_ch, power_limit)                                                                           \
    {                                                                                                                            \
        RegionCode_##name, num_ch, power_limit, freq, spacing, #name                                                             \
    }

const RegionInfo regions[] = {
    RDEF(US, 903.08f, 2.16f, 13, 0), RDEF(EU433, 433.175f, 0.2f, 8, 0), RDEF(EU865, 865.2f, 0.3f, 10, 0),
    RDEF(CN, 470.0f, 2.0f, 20, 0),
    RDEF(JP, 920.0f, 0.5f, 10, 13),    // See https://github.com/meshtastic/Meshtastic-device/issues/346 power level 13
    RDEF(ANZ, 916.0f, 0.5f, 20, 0),    // AU/NZ channel settings 915-928MHz
    RDEF(KR, 921.9f, 0.2f, 8, 0),      // KR channel settings (KR920-923) Start from TTN download channel
                                       // freq. (921.9f is for download, others are for uplink)
    RDEF(TW, 923.0f, 0.2f, 10, 0),     // TW channel settings (AS2 bandplan 923-925MHz)
    RDEF(Unset, 903.08f, 2.16f, 13, 0) // Assume US freqs if unset, Must be last
};

static const RegionInfo *myRegion;

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

// 1kb was too small
#define RADIO_STACK_SIZE 4096

void printPacket(const char *prefix, const MeshPacket *p)
{
    DEBUG_MSG("%s (id=0x%08x Fr0x%02x To0x%02x, WantAck%d, HopLim%d", prefix, p->id, p->from & 0xff, p->to & 0xff, p->want_ack,
              p->hop_limit);
    if (p->which_payload == MeshPacket_decoded_tag) {
        auto &s = p->decoded;
        switch (s.which_payload) {
        case SubPacket_data_tag:
            DEBUG_MSG(" Payload:Data");
            break;
        case SubPacket_position_tag:
            DEBUG_MSG(" Payload:Position");
            break;
        case SubPacket_user_tag:
            DEBUG_MSG(" Payload:User");
            break;
        case 0:
            DEBUG_MSG(" Payload:None");
            break;
        default:
            DEBUG_MSG(" Payload:%d", s.which_payload);
            break;
        }
        if (s.want_response)
            DEBUG_MSG(" WANTRESP");

        if (s.source != 0)
            DEBUG_MSG(" source=%08x", s.source);

        if (s.dest != 0)
            DEBUG_MSG(" dest=%08x", s.dest);

        if (s.which_ack == SubPacket_success_id_tag)
            DEBUG_MSG(" successId=%08x", s.ack.success_id);
        else if (s.which_ack == SubPacket_fail_id_tag)
            DEBUG_MSG(" failId=%08x", s.ack.fail_id);
    } else {
        DEBUG_MSG(" encrypted");
    }

    if (p->rx_time != 0) {
        DEBUG_MSG(" rxtime=%u", p->rx_time);
    }
    if (p->rx_snr != 0.0) {
        DEBUG_MSG(" rxSNR=%g", p->rx_snr);
    }
    DEBUG_MSG(")\n");
}

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == 4 || sizeof(PacketHeader) == 16); // make sure the compiler did what we expected

    if (!myRegion) {
        const RegionInfo *r = regions;
        for (; r->code != RegionCode_Unset && r->code != radioConfig.preferences.region; r++)
            ;
        myRegion = r;
        DEBUG_MSG("Wanted region %d, using %s\n", radioConfig.preferences.region, r->name);

        myNodeInfo.num_channels = myRegion->numChannels; // Tell our android app how many channels we have
    }

    // Can't print strings this early - serial not setup yet
    // DEBUG_MSG("Set meshradio defaults name=%s\n", channelSettings.name);
}

bool RadioInterface::init()
{
    DEBUG_MSG("Starting meshradio init...\n");

    configChangedObserver.observe(&service.configChanged);
    preflightSleepObserver.observe(&preflightSleep);
    notifyDeepSleepObserver.observe(&notifyDeepSleep);

    // we now expect interfaces to operate in promiscous mode
    // radioIf.setThisAddress(nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor
    // time.

    // we want this thread to run at very high priority, because it is effectively running as a user space ISR
    start("radio", RADIO_STACK_SIZE, configMAX_PRIORITIES - 1); // Start our worker thread

    return true;
}

/** hash a string into an integer
 *
 * djb2 by Dan Bernstein.
 * http://www.cse.yorku.ca/~oz/hash.html
 */
unsigned long hash(const char *str)
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
void RadioInterface::applyModemConfig()
{
    // Set up default configuration
    // No Sync Words in LORA mode

    power = channelSettings.tx_power;

    assert(myRegion); // Should have been found in init

    // If user has manually specified a channel num, then use that, otherwise generate one by hashing the name
    int channel_num =
        (channelSettings.channel_num ? channelSettings.channel_num - 1 : hash(channelSettings.name)) % myRegion->numChannels;
    freq = myRegion->freq + myRegion->spacing * channel_num;

    DEBUG_MSG("Set radio: name=%s, config=%u, ch=%d, power=%d\n", channelSettings.name, channelSettings.modem_config, channel_num,
              power);
    DEBUG_MSG("Radio myRegion->freq: %f\n", myRegion->freq);
    DEBUG_MSG("Radio myRegion->spacing: %f\n", myRegion->spacing);
    DEBUG_MSG("Radio myRegion->numChannels: %d\n", myRegion->numChannels);
    DEBUG_MSG("Radio channel_num: %d\n", channel_num);
    DEBUG_MSG("Radio frequency: %f\n", freq);
}

/**
 * Some regulatory regions limit xmit power.
 * This function should be called by subclasses after setting their desired power.  It might lower it
 */
void RadioInterface::limitPower()
{
    uint8_t maxPower = 255; // No limit

    if (myRegion->powerLimit)
        maxPower = myRegion->powerLimit;

    if (power > maxPower) {
        DEBUG_MSG("Lowering transmit power because of regulatory limits\n");
        power = maxPower;
    }

    DEBUG_MSG("Set radio: final power level=%d\n", power);
}

ErrorCode SimRadio::send(MeshPacket *p)
{
    DEBUG_MSG("SimRadio.send\n");
    packetPool.release(p);
    return ERRNO_OK;
}

void RadioInterface::deliverToReceiver(MeshPacket *p)
{
    assert(rxDest);
    assert(rxDest->enqueue(p, 0)); // NOWAIT - fixme, if queue is full, delete older messages
}

/***
 * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of payload bytes to send
 */
size_t RadioInterface::beginSending(MeshPacket *p)
{
    assert(!sendingPacket);

    // DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(p->which_payload == MeshPacket_encrypted_tag); // It should have already been encoded by now

    lastTxStart = millis();

    PacketHeader *h = (PacketHeader *)radiobuf;

    h->from = p->from;
    h->to = p->to;
    h->id = p->id;
    assert(p->hop_limit <= HOP_MAX);
    h->flags = p->hop_limit | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);

    // if the sender nodenum is zero, that means uninitialized
    assert(h->from);

    memcpy(radiobuf + sizeof(PacketHeader), p->encrypted.bytes, p->encrypted.size);

    sendingPacket = p;
    return p->encrypted.size + sizeof(PacketHeader);
}