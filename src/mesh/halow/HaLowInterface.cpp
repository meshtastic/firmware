#include "configuration.h"
#ifdef USE_HALOW_RADIO

#include "HaLowFrame.h"
#include "HaLowInterface.h"
#include "MeshTypes.h"
#include "RTC.h" // getValidTime / RTCQualityFromNet
#include <string.h>
#if HAS_UDP_MULTICAST
#include "main.h" // for `udpHandler`
#include "mesh/generated/meshtastic/config.pb.h"
#endif

#ifdef USE_MM_IOT_ESP32
extern "C" {
#include "mmhal.h"
#include "mmipal.h"
#include "mmwlan.h"
#include "mmwlan_regdb.def"
}

#ifndef HALOW_COUNTRY_CODE
#define HALOW_COUNTRY_CODE "US"
#endif
// SSID/PSK supplied at build time. Empty SSID means "don't auto-associate" —
// the chip boots and idles, useful for testing without an AP nearby.
#ifndef HALOW_SSID
#define HALOW_SSID ""
#endif
#ifndef HALOW_PASSPHRASE
#define HALOW_PASSPHRASE ""
#endif
#endif

HaLowInterface::HaLowInterface() : concurrency::OSThread("HaLow") {}

#ifdef USE_MM_IOT_ESP32
// Static glue so the C callbacks can reach into the (non-static) instance.
// Only one HaLowInterface exists, owned by Router::iface or constructed at
// boot, so a single pointer is sufficient.
static HaLowInterface *s_instance = nullptr;

static void halow_link_state_cb(enum mmwlan_link_state link_state, void *arg)
{
    (void)arg;
    if (link_state == MMWLAN_LINK_UP) {
        struct mmipal_ip_config ipcfg = {};
        if (mmipal_get_ip_config(&ipcfg) == MMIPAL_SUCCESS) {
            LOG_INFO("HaLow: link UP, ip=%s netmask=%s gw=%s", ipcfg.ip_addr, ipcfg.netmask, ipcfg.gateway_addr);
        } else {
            LOG_INFO("HaLow: link UP (no IP yet)");
        }
        int32_t rssi = mmwlan_get_rssi();
        if (rssi != INT32_MIN) {
            LOG_INFO("HaLow: AP RSSI %ld dBm", (long)rssi);
        }
    } else {
        LOG_INFO("HaLow: link DOWN");
    }
}

// mmwlan delivers Ethernet-framed packets here: 14-byte 802.3 header + payload.
// The payload is whatever EtherType we used on the TX side — we filter for our
// own marker and decode the inner RadioBuffer.
void HaLowInterface::rxTrampoline(uint8_t *header, unsigned header_len, uint8_t *payload, unsigned payload_len, void *arg)
{
    HaLowInterface *self = static_cast<HaLowInterface *>(arg);
    if (!self || header_len < sizeof(HaLowEthFrameHeader)) {
        return;
    }
    // EtherType is big-endian in the header (bytes 12-13).
    uint16_t et = ((uint16_t)header[12] << 8) | header[13];
    if (et != ETHERTYPE_MESHTASTIC_HALOW) {
        return;
    }
    self->onFrameReceived(payload, payload_len, /*rssi*/ 0);
}
#endif

HaLowInterface::~HaLowInterface() = default;

bool HaLowInterface::init()
{
    RadioInterface::init();

#ifdef USE_MM_IOT_ESP32
    LOG_INFO("HaLow: mmhal_init()");
    mmhal_init();

    LOG_INFO("HaLow: mmwlan_init()");
    mmwlan_init();

    const struct mmwlan_s1g_channel_list *channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), HALOW_COUNTRY_CODE);
    if (!channel_list) {
        LOG_ERROR("HaLow: country %s not in regdb", HALOW_COUNTRY_CODE);
        return false;
    }
    if (mmwlan_set_channel_list(channel_list) != MMWLAN_SUCCESS) {
        LOG_ERROR("HaLow: set_channel_list failed");
        return false;
    }

    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    enum mmwlan_status st = mmwlan_boot(&boot_args);
    if (st != MMWLAN_SUCCESS) {
        LOG_ERROR("HaLow: mmwlan_boot failed (%d) — firmware load or SPI wiring", (int)st);
        return false;
    }

    struct mmwlan_version version;
    if (mmwlan_get_version(&version) == MMWLAN_SUCCESS) {
        LOG_INFO("HaLow: chip 0x%lx, fw %s, lib %s", (unsigned long)version.morse_chip_id, version.morse_fw_version,
                 version.morselib_version);
    }

    // Bring up the LWIP netif (DHCP by default). mmipal plugs into the
    // Arduino-ESP32 framework's LWIP — no separate stack.
    struct mmipal_init_args ipal_args = MMIPAL_INIT_ARGS_DEFAULT;
    if (mmipal_init(&ipal_args) != MMIPAL_SUCCESS) {
        LOG_ERROR("HaLow: mmipal_init failed");
        return false;
    }

    if (HALOW_SSID[0] == '\0') {
        LOG_INFO("HaLow: no SSID configured, chip will idle (set -DHALOW_SSID=...)");
        return false;
    }

    struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;
    sta_args.ssid_len = strnlen(HALOW_SSID, sizeof(sta_args.ssid));
    memcpy(sta_args.ssid, HALOW_SSID, sta_args.ssid_len);
    sta_args.passphrase_len = strnlen(HALOW_PASSPHRASE, sizeof(sta_args.passphrase));
    memcpy(sta_args.passphrase, HALOW_PASSPHRASE, sta_args.passphrase_len);
    sta_args.security_type = (sta_args.passphrase_len > 0) ? MMWLAN_SAE : MMWLAN_OPEN;

    s_instance = this;
    mmwlan_register_link_state_cb(halow_link_state_cb, NULL);
    if (mmwlan_register_rx_cb(rxTrampoline, this) != MMWLAN_SUCCESS) {
        LOG_ERROR("HaLow: register_rx_cb failed");
        return false;
    }

    LOG_INFO("HaLow: associating with SSID '%s'", HALOW_SSID);
    if (mmwlan_sta_enable(&sta_args, NULL) != MMWLAN_SUCCESS) {
        LOG_ERROR("HaLow: mmwlan_sta_enable failed");
        return false;
    }

    // From here on out, HaLow is the radio. send() encodes packets as 802.3
    // frames addressed to broadcast MAC with EtherType 0x88B5; the AP relays
    // them to all associated STAs (Phase 3 closes the AP-less gap).
    return true;
#else
    LOG_WARN("HaLow: built without USE_MM_IOT_ESP32, transport is a stub");
    return false;
#endif
}

bool HaLowInterface::reconfigure()
{
    return true;
}

bool HaLowInterface::sleep()
{
    return true;
}

bool HaLowInterface::canSleep()
{
    return true;
}

ErrorCode HaLowInterface::send(meshtastic_MeshPacket *p)
{
    if (!p) {
        return ERRNO_UNKNOWN;
    }

#ifdef USE_MM_IOT_ESP32
    // beginSending() serializes the MeshPacket into radioBuffer (PacketHeader
    // + payload). We then prepend a 14-byte 802.3 header so mmwlan can wrap
    // it as an 802.11 data frame and ship it through the AP.
    size_t encoded = beginSending(p);
    if (encoded == 0) {
        packetPool.release(p);
        return ERRNO_UNKNOWN;
    }

    uint8_t txbuf[sizeof(HaLowEthFrameHeader) + sizeof(RadioBuffer)];
    if (encoded > sizeof(RadioBuffer)) {
        LOG_ERROR("HaLow: encoded %u > radioBuffer", (unsigned)encoded);
        packetPool.release(p);
        return ERRNO_UNKNOWN;
    }

    // Ethernet header: DA(6) || SA(6) || EtherType(2, big-endian).
    memcpy(txbuf, HALOW_BROADCAST_MAC, 6);
    if (mmwlan_get_mac_addr(txbuf + 6) != MMWLAN_SUCCESS) {
        memset(txbuf + 6, 0, 6); // fallback so the frame still goes out
    }
    txbuf[12] = (uint8_t)(ETHERTYPE_MESHTASTIC_HALOW >> 8);
    txbuf[13] = (uint8_t)(ETHERTYPE_MESHTASTIC_HALOW & 0xFF);
    memcpy(txbuf + sizeof(HaLowEthFrameHeader), &radioBuffer, encoded);

    enum mmwlan_status st = mmwlan_tx(txbuf, sizeof(HaLowEthFrameHeader) + encoded);
    packetPool.release(p);
    sendingPacket = NULL;
    return (st == MMWLAN_SUCCESS) ? ERRNO_OK : ERRNO_UNKNOWN;
#else
    packetPool.release(p);
    return ERRNO_DISABLED;
#endif
}

meshtastic_QueueStatus HaLowInterface::getQueueStatus()
{
    meshtastic_QueueStatus qs = meshtastic_QueueStatus_init_zero;
    qs.free = 16;
    qs.maxlen = 16;
    return qs;
}

uint32_t HaLowInterface::getPacketTime(uint32_t totalPacketLen, bool /*received*/)
{
    // bytes * 8 bits / (HALOW_NOMINAL_KBPS * 1000 bits/sec) * 1000 ms/sec.
    // Floor to 1 ms so the slot-time math upstream never divides by zero.
    uint32_t ms = (totalPacketLen * 8u + HALOW_NOMINAL_KBPS - 1u) / HALOW_NOMINAL_KBPS;
    return ms ? ms : 1u;
}

int32_t HaLowInterface::runOnce()
{
    return 1000; // nothing to do until the SDK is wired in
}

void HaLowInterface::onFrameReceived(const uint8_t *payload, size_t payload_len, int8_t rssi)
{
    if (!payload || payload_len < sizeof(PacketHeader)) {
        return;
    }
    // Cap at our RadioBuffer size — anything larger is malformed for our wire
    // format and we drop it rather than corrupt memory.
    if (payload_len > sizeof(RadioBuffer)) {
        LOG_WARN("HaLow: rx %u > RadioBuffer, dropping", (unsigned)payload_len);
        return;
    }

    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    if (!p) {
        return;
    }

    // Unpack the RadioBuffer into a MeshPacket (mirrors the LoRa RX decode).
    const PacketHeader *h = reinterpret_cast<const PacketHeader *>(payload);
    p->from = h->from;
    p->to = h->to;
    p->id = h->id;
    p->channel = h->channel;
    p->hop_limit = h->flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    p->want_ack = !!(h->flags & PACKET_FLAGS_WANT_ACK_MASK);
    p->via_mqtt = !!(h->flags & PACKET_FLAGS_VIA_MQTT_MASK);
    p->hop_start = (h->flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    p->relay_node = h->relay_node;
    p->next_hop = h->next_hop;

    size_t payload_only = payload_len - sizeof(PacketHeader);
    if (payload_only > sizeof(p->encrypted.bytes)) {
        packetPool.release(p);
        return;
    }
    memcpy(p->encrypted.bytes, payload + sizeof(PacketHeader), payload_only);
    p->encrypted.size = payload_only;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    // mmwlan's rx callback doesn't carry per-frame RSSI on this SDK version —
    // fall back to the connection-level RSSI for visibility in the phone UI.
    (void)rssi;
    int32_t link_rssi = mmwlan_get_rssi();
    p->rx_rssi = (link_rssi == INT32_MIN) ? 0 : (int8_t)link_rssi;
    p->rx_snr = 0;
    p->rx_time = getValidTime(RTCQualityFromNet);

    deliverToReceiver(p);
}

#endif // USE_HALOW_RADIO
