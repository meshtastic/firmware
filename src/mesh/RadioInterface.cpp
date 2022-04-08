#include "RadioInterface.h"
#include "Channels.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "assert.h"
#include "configuration.h"
#include "sleep.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

#define RDEF(name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted, frequency_switching)                 \
    {                                                                                                                            \
        RegionCode_##name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted, frequency_switching, #name   \
    }

const RegionInfo regions[] = {
    /*
        https://link.springer.com/content/pdf/bbm%3A978-1-4842-4357-2%2F1.pdf
        https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
    */
    RDEF(US, 902.0f, 928.0f, 100, 0, 30, true, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(EU433, 433.0f, 434.0f, 10, 0, 12, true, false),

    /*
        https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/
        https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
        https://www.legislation.gov.uk/uksi/1999/930/schedule/6/part/III/made/data.xht?view=snippet&wrap=true

        audio_permitted = false per regulation
     */
    RDEF(EU868, 869.4f, 869.65f, 10, 0, 16, false, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(CN, 470.0f, 510.0f, 100, 0, 19, true, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(JP, 920.8f, 927.8f, 100, 0, 16, true, false),

    /*
        https://www.iot.org.au/wp/wp-content/uploads/2016/12/IoTSpectrumFactSheet.pdf
        https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
     */
    RDEF(ANZ, 915.0f, 928.0f, 100, 0, 30, true, false),

    /*
        https://digital.gov.ru/uploaded/files/prilozhenie-12-k-reshenyu-gkrch-18-46-03-1.pdf

        Note:
            - We do LBT, so 100% is allowed.
     */
    RDEF(RU, 868.7f, 869.2f, 100, 0, 20, true, false),

    /*
        ???
     */
    RDEF(KR, 920.0f, 923.0f, 100, 0, 0, true, false),

    /*
        ???
     */
    RDEF(TW, 920.0f, 925.0f, 100, 0, 0, true, false),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(IN, 865.0f, 867.0f, 100, 0, 30, true, false),

   /*
        https://rrf.rsm.govt.nz/smart-web/smart/page/-smart/domain/licence/LicenceSummary.wdk?id=219752
        https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
     */
    RDEF(NZ865, 864.0f, 868.0f, 100, 0, 0, true, false),

     /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(TH, 920.0f, 925.0f, 100, 0, 16, true, false),

    /*
        This needs to be last. Same as US.
    */
    RDEF(Unset, 902.0f, 928.0f, 100, 0, 30, true, false)

};

const RegionInfo *myRegion;

void initRegion()
{
    const RegionInfo *r = regions;
    for (; r->code != RegionCode_Unset && r->code != radioConfig.preferences.region; r++)
        ;
    myRegion = r;
    DEBUG_MSG("Wanted region %d, using %s\n", radioConfig.preferences.region, r->name);
}

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

// 1kb was too small
#define RADIO_STACK_SIZE 4096

/**
 * Calculate airtime per
 * https://www.rs-online.com/designspark/rel-assets/ds-assets/uploads/knowledge-items/application-notes-for-the-internet-of-things/LoRa%20Design%20Guide.pdf
 * section 4
 *
 * @return num msecs for the packet
 */
uint32_t RadioInterface::getPacketTime(uint32_t pl)
{
    float bandwidthHz = bw * 1000.0f;
    bool headDisable = false; // we currently always use the header
    float tSym = (1 << sf) / bandwidthHz;

    bool lowDataOptEn = tSym > 16e-3 ? true : false; // Needed if symbol time is >16ms

    float tPreamble = (preambleLength + 4.25f) * tSym;
    float numPayloadSym =
        8 + max(ceilf(((8.0f * pl - 4 * sf + 28 + 16 - 20 * headDisable) / (4 * (sf - 2 * lowDataOptEn))) * cr), 0.0f);
    float tPayload = numPayloadSym * tSym;
    float tPacket = tPreamble + tPayload;

    uint32_t msecs = tPacket * 1000;

    DEBUG_MSG("(bw=%d, sf=%d, cr=4/%d) packet symLen=%d ms, payloadSize=%u, time %d ms\n", (int)bw, sf, cr, (int)(tSym * 1000),
              pl, msecs);
    return msecs;
}

uint32_t RadioInterface::getPacketTime(MeshPacket *p)
{
    assert(p->which_payloadVariant == MeshPacket_encrypted_tag); // It should have already been encoded by now
    uint32_t pl = p->encrypted.size + sizeof(PacketHeader);

    return getPacketTime(pl);
}

/** The delay to use for retransmitting dropped packets */
uint32_t RadioInterface::getRetransmissionMsec(const MeshPacket *p)
{
    assert(shortPacketMsec); // Better be non zero

    // was 20 and 22 secs respectively, but now with shortPacketMsec as 2269, this should give the same range
    return random(9 * shortPacketMsec, 10 * shortPacketMsec);
}

/** The delay to use when we want to send something but the ether is busy */
uint32_t RadioInterface::getTxDelayMsec()
{
    /** At the low end we want to pick a delay large enough that anyone who just completed sending (some other node)
     * has had enough time to switch their radio back into receive mode.
     */
    const uint32_t MIN_TX_WAIT_MSEC = 100;

    /**
     * At the high end, this value is used to spread node attempts across time so when they are replying to a packet
     * they don't both check that the airwaves are clear at the same moment.  As long as they are off by some amount
     * one of the two will be first to start transmitting and the other will see that.  I bet 500ms is more than enough
     * to guarantee this.
     */
    // const uint32_t MAX_TX_WAIT_MSEC = 2000; // stress test would still fail occasionally with 1000

    return random((MIN_TX_WAIT_MSEC), (MIN_TX_WAIT_MSEC + shortPacketMsec));
}


/** The delay to use when we want to send something but the ether is busy */
uint32_t RadioInterface::getTxDelayMsecWeighted(float snr)
{
    /** At the low end we want to pick a delay large enough that anyone who just completed sending (some other node)
     * has had enough time to switch their radio back into receive mode.
     */
    const uint32_t MIN_TX_WAIT_MSEC = 100;

    // The minimum value for a LoRa SNR
    const uint32_t SNR_MIN = -20;

    // The maximum value for a LoRa SNR
    const uint32_t SNR_MAX = 15;

    //  high SNR = Long Delay
    //  low SNR = Short Delay
    uint32_t delay = 0;

    if (radioConfig.preferences.role == Role_Router || radioConfig.preferences.role == Role_RouterClient) {
        delay = map(snr, SNR_MIN, SNR_MAX, MIN_TX_WAIT_MSEC, (MIN_TX_WAIT_MSEC + (shortPacketMsec / 2)));
        DEBUG_MSG("rx_snr found in packet. As a router, setting tx delay:%d\n", delay);
    } else {
        delay = map(snr, SNR_MIN, SNR_MAX, MIN_TX_WAIT_MSEC + (shortPacketMsec / 2), (MIN_TX_WAIT_MSEC + shortPacketMsec * 2));
        DEBUG_MSG("rx_snr found in packet. Setting tx delay:%d\n", delay);
    }


    return delay;
}

void printPacket(const char *prefix, const MeshPacket *p)
{
    DEBUG_MSG("%s (id=0x%08x Fr0x%02x To0x%02x, WantAck%d, HopLim%d Ch0x%x", prefix, p->id, p->from & 0xff, p->to & 0xff,
              p->want_ack, p->hop_limit, p->channel);
    if (p->which_payloadVariant == MeshPacket_decoded_tag) {
        auto &s = p->decoded;

        DEBUG_MSG(" Portnum=%d", s.portnum);

        if (s.want_response)
            DEBUG_MSG(" WANTRESP");

        if (s.source != 0)
            DEBUG_MSG(" source=%08x", s.source);

        if (s.dest != 0)
            DEBUG_MSG(" dest=%08x", s.dest);

        if (s.request_id)
            DEBUG_MSG(" requestId=%0x", s.request_id);

        /* now inside Data and therefore kinda opaque
        if (s.which_ackVariant == SubPacket_success_id_tag)
            DEBUG_MSG(" successId=%08x", s.ackVariant.success_id);
        else if (s.which_ackVariant == SubPacket_fail_id_tag)
            DEBUG_MSG(" failId=%08x", s.ackVariant.fail_id); */
    } else {
        DEBUG_MSG(" encrypted");
    }

    if (p->rx_time != 0) {
        DEBUG_MSG(" rxtime=%u", p->rx_time);
    }
    if (p->rx_snr != 0.0) {
        DEBUG_MSG(" rxSNR=%g", p->rx_snr);
    }
    if (p->rx_rssi != 0) {
        DEBUG_MSG(" rxSNR=%g", p->rx_rssi);
    }
    if (p->priority != 0)
        DEBUG_MSG(" priority=%d", p->priority);

    DEBUG_MSG(")\n");
}

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == 16); // make sure the compiler did what we expected

    // Can't print strings this early - serial not setup yet
    // DEBUG_MSG("Set meshradio defaults name=%s\n", channelSettings.name);
}

bool RadioInterface::reconfigure()
{
    applyModemConfig();
    return true;
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

    applyModemConfig();

    return true;
}

int RadioInterface::notifyDeepSleepCb(void *unused)
{
    sleep();
    return 0;
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
 * Save our frequency for later reuse.
 */
void RadioInterface::saveFreq(float freq)
{
    savedFreq = freq;
}

/**
 * Save our channel for later reuse.
 */
void RadioInterface::saveChannelNum(uint32_t channel_num)
{
    savedChannelNum = channel_num;
}

/**
 * Save our frequency for later reuse.
 */
float RadioInterface::getFreq()
{
    return savedFreq;
}

/**
 * Save our channel for later reuse.
 */
uint32_t RadioInterface::getChannelNum()
{
    return savedChannelNum;
}

/**
 * Pull our channel settings etc... from protobufs to the dumb interface settings
 */
void RadioInterface::applyModemConfig()
{
    // Set up default configuration
    // No Sync Words in LORA mode

    auto channelSettings = channels.getPrimary();
    if (channelSettings.spread_factor == 0) {
        switch (channelSettings.modem_config) {
        case ChannelSettings_ModemConfig_ShortFast:
            bw = 250;
            cr = 8;
            sf = 7;
            break;
        case ChannelSettings_ModemConfig_ShortSlow:
            bw = 250;
            cr = 8;
            sf = 8;
            break;
        case ChannelSettings_ModemConfig_MidFast:
            bw = 250;
            cr = 8;
            sf = 9;
            break;
        case ChannelSettings_ModemConfig_MidSlow:
            bw = 250;
            cr = 8;
            sf = 10;
            break;
        case ChannelSettings_ModemConfig_LongFast:
            bw = 250;
            cr = 8;
            sf = 11;
            break;
        case ChannelSettings_ModemConfig_LongSlow:
            bw = 125;
            cr = 8;
            sf = 12;
            break;
        case ChannelSettings_ModemConfig_VLongSlow:
            bw = 31.25;
            cr = 8;
            sf = 12;
            break;
        default:
            assert(0); // Unknown enum
        }
    } else {
        sf = channelSettings.spread_factor;
        cr = channelSettings.coding_rate;
        bw = channelSettings.bandwidth;

        if (bw == 31) // This parameter is not an integer
            bw = 31.25;
        if (bw == 62) // Fix for 62.5Khz bandwidth
            bw = 62.5;
    }

    power = channelSettings.tx_power;
    shortPacketMsec = getPacketTime(sizeof(PacketHeader));
    assert(myRegion); // Should have been found in init

    // Calculate the number of channels
    uint32_t numChannels = floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000)));

    // If user has manually specified a channel num, then use that, otherwise generate one by hashing the name
    const char *channelName = channels.getName(channels.getPrimaryIndex());
    int channel_num = channelSettings.channel_num ? channelSettings.channel_num - 1 : hash(channelName) % numChannels;
    float freq = myRegion->freqStart + ((((myRegion->freqEnd - myRegion->freqStart) / numChannels) / 2) * channel_num);

    saveChannelNum(channel_num);
    saveFreq(freq);

    DEBUG_MSG("Set radio: name=%s, config=%u, ch=%d, power=%d\n", channelName, channelSettings.modem_config, channel_num, power);
    DEBUG_MSG("Radio myRegion->freqStart / myRegion->freqEnd: %f -> %f (%f mhz)\n", myRegion->freqStart, myRegion->freqEnd,
              myRegion->freqEnd - myRegion->freqStart);
    DEBUG_MSG("Radio myRegion->numChannels: %d\n", numChannels);
    DEBUG_MSG("Radio channel_num: %d\n", channel_num);
    DEBUG_MSG("Radio frequency: %f\n", getFreq());
    DEBUG_MSG("Short packet time: %u msec\n", shortPacketMsec);
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
    if (router)
        router->enqueueReceivedMessage(p);
}

/***
 * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of payload bytes to send
 */
size_t RadioInterface::beginSending(MeshPacket *p)
{
    assert(!sendingPacket);

    // DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(p->which_payloadVariant == MeshPacket_encrypted_tag); // It should have already been encoded by now

    lastTxStart = millis();

    PacketHeader *h = (PacketHeader *)radiobuf;

    h->from = p->from;
    h->to = p->to;
    h->id = p->id;
    h->channel = p->channel;
    assert(p->hop_limit <= HOP_MAX);
    h->flags = p->hop_limit | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);

    // if the sender nodenum is zero, that means uninitialized
    assert(h->from);

    memcpy(radiobuf + sizeof(PacketHeader), p->encrypted.bytes, p->encrypted.size);

    sendingPacket = p;
    return p->encrypted.size + sizeof(PacketHeader);
}
