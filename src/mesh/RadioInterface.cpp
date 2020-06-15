
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
    DEBUG_MSG(")\n");
}

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == 4 || sizeof(PacketHeader) == 16); // make sure the compiler did what we expected

    myNodeInfo.num_channels = NUM_CHANNELS;

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
void RadioInterface::applyModemConfig()
{
    // Set up default configuration
    // No Sync Words in LORA mode.
    modemConfig = (ModemConfigChoice)channelSettings.modem_config;

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
    int channel_num = hash(channelSettings.name) % NUM_CHANNELS;
    freq = CH0 + CH_SPACING * channel_num;
    power = channelSettings.tx_power;

    DEBUG_MSG("Set radio: name=%s, config=%u, ch=%d, power=%d\n", channelSettings.name, channelSettings.modem_config, channel_num,
              channelSettings.tx_power);
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