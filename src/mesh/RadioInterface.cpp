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
        Config_LoRaConfig_RegionCode_##name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted,            \
            frequency_switching, #name                                                                                           \
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

        Special Note:
        The link above describes LoRaWAN's band plan, stating a power limit of 16 dBm. This is their own suggested specification,
        we do not need to follow it. The European Union regulations clearly state that the power limit for this frequency range is 500 mW, or 27 dBm.
        It also states that we can use interference avoidance and spectrum access techniques to avoid a duty cycle.
        (Please refer to section 4.21 in the following document)
        https://ec.europa.eu/growth/tools-databases/tris/index.cfm/ro/search/?trisaction=search.detail&year=2021&num=528&dLang=EN
     */
    RDEF(EU868, 869.4f, 869.65f, 10, 0, 27, false, false),

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
    for (; r->code != Config_LoRaConfig_RegionCode_Unset && r->code != config.lora.region; r++)
        ;
    myRegion = r;
    DEBUG_MSG("Wanted region %d, using %s\n", config.lora.region, r->name);
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
    assert(slotTimeMsec); // Better be non zero
    static uint8_t bytes[MAX_RHPACKETLEN];
    size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), Data_fields, &p->decoded);
    uint32_t packetAirtime = getPacketTime(numbytes + sizeof(PacketHeader));
    // Make sure enough time has elapsed for this packet to be sent and an ACK is received. 
    // DEBUG_MSG("Waiting for flooding message with airtime %d and slotTime is %d\n", packetAirtime, slotTimeMsec);
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // Assuming we pick max. of CWsize and there will be a receiver with SNR at half the range
    return 2*packetAirtime + (pow(2, CWsize) + pow(2, int((CWmax+CWmin)/2))) * slotTimeMsec + PROCESSING_TIME_MSEC;
}

/** The delay to use when we want to send something */
uint32_t RadioInterface::getTxDelayMsec()
{
    /** We wait a random multiple of 'slotTimes' (see definition in header file) in order to avoid collisions.
    The pool to take a random multiple from is the contention window (CW), which size depends on the 
    current channel utilization. */
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // DEBUG_MSG("Current channel utilization is %f so setting CWsize to %d\n", channelUtil, CWsize);
    return random(0, pow(2, CWsize)) * slotTimeMsec;
}

/** The delay to use when we want to flood a message */
uint32_t RadioInterface::getTxDelayMsecWeighted(float snr)
{
    // The minimum value for a LoRa SNR
    const uint32_t SNR_MIN = -20;

    // The maximum value for a LoRa SNR
    const uint32_t SNR_MAX = 15;

    //  high SNR = large CW size (Long Delay)
    //  low SNR = small CW size (Short Delay)
    uint32_t delay = 0;
    uint8_t CWsize = map(snr, SNR_MIN, SNR_MAX, CWmin, CWmax);
    // DEBUG_MSG("rx_snr of %f so setting CWsize to:%d\n", snr, CWsize);
    if (config.device.role == Config_DeviceConfig_Role_Router ||
        config.device.role == Config_DeviceConfig_Role_RouterClient) {
        delay = random(0, 2*CWsize) * slotTimeMsec;
        DEBUG_MSG("rx_snr found in packet. As a router, setting tx delay:%d\n", delay);
    } else {
        delay = random(0, pow(2, CWsize)) * slotTimeMsec;
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
        DEBUG_MSG(" rxRSSI=%g", p->rx_rssi);
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
    Config_LoRaConfig &loraConfig = config.lora;
    auto channelSettings = channels.getPrimary();
    if (loraConfig.spread_factor == 0) {
        switch (loraConfig.modem_preset) {
        case Config_LoRaConfig_ModemPreset_ShortFast:
            bw = 250;
            cr = 8;
            sf = 7;
            break;
        case Config_LoRaConfig_ModemPreset_ShortSlow:
            bw = 250;
            cr = 8;
            sf = 8;
            break;
        case Config_LoRaConfig_ModemPreset_MedFast:
            bw = 250;
            cr = 8;
            sf = 9;
            break;
        case Config_LoRaConfig_ModemPreset_MedSlow:
            bw = 250;
            cr = 8;
            sf = 10;
            break;
        case Config_LoRaConfig_ModemPreset_LongFast:
            bw = 250;
            cr = 8;
            sf = 11;
            break;
        case Config_LoRaConfig_ModemPreset_LongSlow:
            bw = 125;
            cr = 8;
            sf = 12;
            break;
        case Config_LoRaConfig_ModemPreset_VLongSlow:
            bw = 31.25;
            cr = 8;
            sf = 12;
            break;
        default:
            assert(0); // Unknown enum
        }
    } else {
        sf = loraConfig.spread_factor;
        cr = loraConfig.coding_rate;
        bw = loraConfig.bandwidth;

        if (bw == 31) // This parameter is not an integer
            bw = 31.25;
        if (bw == 62) // Fix for 62.5Khz bandwidth
            bw = 62.5;
    }

    power = loraConfig.tx_power;
    assert(myRegion); // Should have been found in init

    if ((power == 0) || (power > myRegion->powerLimit))
        power = myRegion->powerLimit;

    if (power == 0)
        power = 17; // Default to default power if we don't have a valid power
    
    // Calculate the number of channels
    uint32_t numChannels = floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000)));

    // If user has manually specified a channel num, then use that, otherwise generate one by hashing the name
    const char *channelName = channels.getName(channels.getPrimaryIndex());
    int channel_num = channelSettings.channel_num ? channelSettings.channel_num - 1 : hash(channelName) % numChannels;

    // Old frequency selection formula
    // float freq = myRegion->freqStart + ((((myRegion->freqEnd - myRegion->freqStart) / numChannels) / 2) * channel_num);

    // New frequency selection formula
    float freq = myRegion->freqStart + (bw / 2000) + ( channel_num * (bw / 1000));

    saveChannelNum(channel_num);
    saveFreq(freq + config.lora.frequency_offset);

    DEBUG_MSG("Set radio: region=%s, name=%s, config=%u, ch=%d, power=%d\n", myRegion->name, channelName, loraConfig.modem_preset, channel_num, power);
    DEBUG_MSG("Radio myRegion->freqStart / myRegion->freqEnd: %f -> %f (%f mhz)\n", myRegion->freqStart, myRegion->freqEnd, myRegion->freqEnd - myRegion->freqStart);
    DEBUG_MSG("Radio myRegion->numChannels: %d\n", numChannels);
    DEBUG_MSG("Radio channel_num: %d\n", channel_num);
    DEBUG_MSG("Radio frequency: %f\n", getFreq());
    DEBUG_MSG("Slot time: %u msec\n", slotTimeMsec);
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
