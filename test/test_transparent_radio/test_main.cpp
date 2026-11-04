// TransparentRadio (src/platform/portduino/TransparentRadio.cpp): meshtasticd's Lora.Module "transparent",
// which uses another node's LoRa chip through that node's client API while the node runs
// LoRaConfig.transparent_mode.
//
// Under test, against an in-memory TransparentLink standing in for the serial/TCP connection:
//   - the handshake: want_config with the config-only nonce, then a set_config pushing this node's LoRa
//     settings with transparent_mode on, then a read-back that must confirm transparent_mode;
//   - TX: a frame waits out its local delay in this node's queue, then goes to the device as an encrypted
//     MeshPacket carrying only the radio header fields and ciphertext;
//   - RX: encrypted frames from the device reach this node's router, everything else does not;
//   - flow control, cancellation, link loss and live reconfiguration.
//
// Why: the device puts frames on the air exactly as given and leaves routing, retries and airtime to this
// node. So the frequency slot must be this node's (the device would otherwise hash its own channel name),
// relays must stay cancellable here until their delay ends (managed flooding depends on it), and nothing
// but genuine LoRa frames may enter the router.
//
// Regressions guarded:
//   - Pushing config.lora unchanged, or without channel_num, leaves the device on a different frequency.
//   - Overwriting the device's frequency_offset or front-end settings miscalibrates its hardware.
//   - Treating a missing transparent_mode in the read-back as success hands frames to old firmware,
//     which would decrypt, relay and re-originate them as a normal node.
//   - Forwarding a frame as soon as it is queued makes cancelSending() useless and every relay go first.
//   - Forwarding rx metadata or tx_after lets the device apply relay delays a second time.
//   - Delivering decoded packets (the device's admin replies) or frames heard before Ready into the router.
//   - Ignoring a full device queue, or not re-handshaking after the link drops or the device reboots.
//   - Not re-pushing when this node's LoRa config changes, which leaves the device on the old settings.

#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "platform/portduino/TransparentRadio.h"

#if HAS_TRANSPARENT_RADIO

#include "UptimeClock.h"
#include "airtime.h"
#include "mesh/Channels.h"
#include "mesh/MeshRadio.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "support/MockMeshService.h"
#include <cstring>
#include <deque>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>

static constexpr NodeNum DEVICE_NODE = 0x0D0D0D0D;
static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum REMOTE_NODE = 0x0B0B0B0B;
static constexpr uint32_t CLOCK_BASE_MS = 100000;
static constexpr char PRIMARY_CHANNEL_NAME[] = "xparent";
// Stored whole, or the radio hashes the truncated name and the slot assertion compares against the wrong one.
static_assert(sizeof(PRIMARY_CHANNEL_NAME) <= sizeof(meshtastic_ChannelSettings::name), "channel name must fit");

/// The serial/TCP connection, in memory: what the radio wrote, and what the "device" will send back.
class FakeLink : public TransparentLink
{
  public:
    bool open() override
    {
        opens++;
        isOpenFlag = openOk;
        return openOk;
    }
    void close() override { isOpenFlag = false; }
    bool isOpen() const override { return isOpenFlag; }
    int read(uint8_t *buf, size_t len) override
    {
        if (!isOpenFlag || readFails)
            return -1;
        size_t n = 0;
        while (n < len && !incoming.empty()) {
            buf[n++] = incoming.front();
            incoming.pop_front();
        }
        return (int)n;
    }
    bool write(const uint8_t *buf, size_t len) override
    {
        if (!isOpenFlag)
            return false;
        written.insert(written.end(), buf, buf + len);
        return true;
    }
    const std::string &describe() const override { return name; }

    bool openOk = true;
    bool isOpenFlag = false;
    bool readFails = false;
    int opens = 0;
    std::deque<uint8_t> incoming;
    std::vector<uint8_t> written;
    std::string name = "fake-link";
};

/// Captures what the radio hands to the router instead of routing it.
class CaptureRouter : public Router
{
  public:
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override
    {
        received.push_back(*p);
        packetPool.release(p);
    }
    std::vector<meshtastic_MeshPacket> received;
};

class TestRadio : public TransparentRadio
{
  public:
    using TransparentRadio::runOnce;
    using TransparentRadio::TransparentRadio;
};

static FakeLink *fakeLink = nullptr;
static TestRadio *radio = nullptr;
static CaptureRouter *captureRouter = nullptr;
/// Keeps the last client notification's type and text; the shared mock only counts them.
class NotifyingService : public MockMeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override
    {
        lastType = n->type;
        lastMessage = n->message;
        MockMeshService::sendClientNotification(n);
    }
    meshtastic_ClientNotification_NotificationType lastType = meshtastic_ClientNotification_NotificationType_UNSET;
    std::string lastMessage;
};

static NotifyingService *mockService = nullptr;
static AirTime *testAirTime = nullptr;

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

/// Every ToRadio the radio wrote since the last call, in order. Wake bytes and other non-frame bytes are skipped.
static std::vector<meshtastic_ToRadio> takeToRadio()
{
    std::vector<meshtastic_ToRadio> out;
    const std::vector<uint8_t> &w = fakeLink->written;
    size_t i = 0;
    while (i + 4 <= w.size()) {
        if (w[i] != 0x94 || w[i + 1] != 0xc3) {
            i++;
            continue;
        }
        const size_t len = (w[i + 2] << 8) | w[i + 3];
        TEST_ASSERT_TRUE_MESSAGE(i + 4 + len <= w.size(), "truncated frame on the link");
        meshtastic_ToRadio tr = meshtastic_ToRadio_init_zero;
        TEST_ASSERT_TRUE(pb_decode_from_bytes(w.data() + i + 4, len, &meshtastic_ToRadio_msg, &tr));
        out.push_back(tr);
        i += 4 + len;
    }
    fakeLink->written.clear();
    return out;
}

/// Queue one framed FromRadio for the radio to read on its next runOnce().
static void deviceSends(const meshtastic_FromRadio &fr)
{
    uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
    const size_t len = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_FromRadio_msg, &fr);
    TEST_ASSERT_GREATER_THAN(0, len);
    fakeLink->incoming.push_back(0x94);
    fakeLink->incoming.push_back(0xc3);
    fakeLink->incoming.push_back((len >> 8) & 0xff);
    fakeLink->incoming.push_back(len & 0xff);
    fakeLink->incoming.insert(fakeLink->incoming.end(), buf, buf + len);
}

static void deviceSendsLora(const meshtastic_Config_LoRaConfig &lora)
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_config_tag;
    fr.config.which_payload_variant = meshtastic_Config_lora_tag;
    fr.config.payload_variant.lora = lora;
    deviceSends(fr);
}

/// The device's answer to want_config: its node number, its LoRa config, then config_complete_id.
static void deviceSendsConfigDump(const meshtastic_Config_LoRaConfig &lora, const char *firmwareVersion = "2.8.3.abcdef0")
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    fr.my_info.my_node_num = DEVICE_NODE;
    deviceSends(fr);
    fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    strncpy(fr.metadata.firmware_version, firmwareVersion, sizeof(fr.metadata.firmware_version) - 1);
    deviceSends(fr);
    deviceSendsLora(lora);
    fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fr.config_complete_id = SPECIAL_NONCE_ONLY_CONFIG;
    deviceSends(fr);
}

static void deviceSendsQueueFree(uint32_t freeSlots)
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    fr.queueStatus.free = freeSlots;
    fr.queueStatus.maxlen = 16;
    deviceSends(fr);
}

static meshtastic_AdminMessage adminIn(const meshtastic_ToRadio &tr)
{
    TEST_ASSERT_EQUAL(meshtastic_ToRadio_packet_tag, tr.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_decoded_tag, tr.packet.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_ADMIN_APP, tr.packet.decoded.portnum);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(DEVICE_NODE, tr.packet.to, "admin must go to the device's own node number");
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(tr.packet.decoded.payload.bytes, tr.packet.decoded.payload.size,
                                          &meshtastic_AdminMessage_msg, &admin));
    return admin;
}

/// The LoRa config the radio should leave the device on: ours, transparent, pinned to our slot.
static meshtastic_Config_LoRaConfig expectedDeviceLora()
{
    meshtastic_Config_LoRaConfig lora = config.lora;
    lora.transparent_mode = true;
    lora.channel_num = radio->getChannelNum() + 1;
    lora.coding_rate = clampCodingRate(lora.coding_rate); // as the device's AdminModule stores it
    lora.spread_factor = clampSpreadFactor(lora.spread_factor);
    return lora;
}

/// The device's answer to the read-back (get_config_request) the radio sent as `request`.
static void deviceAnswersReadBack(const meshtastic_ToRadio &request, const meshtastic_Config_LoRaConfig &lora)
{
    meshtastic_FromRadio reply = meshtastic_FromRadio_init_zero;
    reply.which_payload_variant = meshtastic_FromRadio_packet_tag;
    reply.packet.from = DEVICE_NODE;
    reply.packet.to = DEVICE_NODE;
    reply.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    reply.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    reply.packet.decoded.request_id = request.packet.id;
    meshtastic_AdminMessage readBack = meshtastic_AdminMessage_init_zero;
    readBack.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
    readBack.get_config_response.which_payload_variant = meshtastic_Config_lora_tag;
    readBack.get_config_response.payload_variant.lora = lora;
    reply.packet.decoded.payload.size = pb_encode_to_bytes(
        reply.packet.decoded.payload.bytes, sizeof(reply.packet.decoded.payload.bytes), &meshtastic_AdminMessage_msg, &readBack);
    deviceSends(reply);
}

/// Run the handshake against a device already in the state we want, so no config is pushed.
static void bringReady()
{
    radio->runOnce();
    takeToRadio();
    deviceSendsConfigDump(expectedDeviceLora());
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE((int)TransparentRadio::State::Ready, (int)radio->getState(), "handshake did not reach Ready");
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "a device already set up needs no admin traffic");
}

/// An encrypted frame as Router::send() hands it to the interface, due tx_after ms from now.
static meshtastic_MeshPacket *makeOutgoing(PacketId id, uint32_t dueInMs)
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    TEST_ASSERT_NOT_NULL(p);
    p->from = LOCAL_NODE;
    p->to = REMOTE_NODE;
    p->id = id;
    p->channel = 0x42;
    p->hop_limit = 3;
    p->hop_start = 5;
    p->want_ack = true;
    p->via_mqtt = true;
    p->next_hop = 0x0b;
    p->relay_node = 0x0a;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    p->rx_snr = 7.5f; // a relayed frame carries the SNR it was heard at
    p->rx_rssi = -90;
    p->has_rx_rssi = true;
    p->rx_time = 1234;
    p->has_rx_time = true;
    p->tx_after = Time::getMillis() + dueInMs;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = 24;
    for (pb_size_t i = 0; i < p->encrypted.size; i++)
        p->encrypted.bytes[i] = (uint8_t)(0xA0 + i);
    return p;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void setUp(void)
{
    Time::setTestMillis(CLOCK_BASE_MS);
    config = meshtastic_LocalConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.tx_enabled = true;
    config.lora.hop_limit = 3;
    initRegion();
    channels.initDefaults();
    strncpy(channelFile.channels[0].settings.name, PRIMARY_CHANNEL_NAME, sizeof(channelFile.channels[0].settings.name) - 1);
    channels.onConfigChanged();

    service = mockService = new NotifyingService();
    router = captureRouter = new CaptureRouter();
    airTime = testAirTime = new AirTime();

    auto owned = std::unique_ptr<FakeLink>(new FakeLink());
    fakeLink = owned.get();
    radio = new TestRadio(std::move(owned));
    TEST_ASSERT_TRUE(radio->init());
}

void tearDown(void)
{
    delete radio;
    radio = nullptr;
    fakeLink = nullptr;
    router = nullptr;
    delete captureRouter;
    captureRouter = nullptr;
    service = nullptr;
    delete mockService;
    mockService = nullptr;
    airTime = nullptr;
    delete testAirTime;
    testAirTime = nullptr;
    Time::useRealClock();
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

// A device that is still a normal node is switched over: want_config (config only), then set_config with
// our LoRa settings plus transparent_mode, then a read-back. Only the read-back confirming transparent_mode
// makes the radio Ready.
void test_handshake_pushes_our_lora_config_with_transparent_mode(void)
{
    radio->runOnce();
    TEST_ASSERT_EQUAL(1, fakeLink->opens);
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_ToRadio_want_config_id_tag, sent[0].which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SPECIAL_NONCE_ONLY_CONFIG, sent[0].want_config_id, "only the config, not the node DB");

    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    deviceNow.use_preset = true;
    deviceNow.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    deviceNow.frequency_offset = 0.015f;
    deviceNow.sx126x_rx_boosted_gain = true;
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();

    sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());
    const meshtastic_AdminMessage set = adminIn(sent[0]);
    TEST_ASSERT_EQUAL(meshtastic_AdminMessage_set_config_tag, set.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_Config_lora_tag, set.set_config.which_payload_variant);
    const meshtastic_Config_LoRaConfig &pushed = set.set_config.payload_variant.lora;
    TEST_ASSERT_TRUE(pushed.transparent_mode);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, pushed.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, pushed.modem_preset);
    meshtastic_Config_LoRaConfig unpinned = config.lora;
    unpinned.channel_num = 0;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(RadioInterface::resolveFrequencySlot(unpinned, PRIMARY_CHANNEL_NAME), pushed.channel_num,
                                     "the device must use the slot our primary channel name hashes to");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.015f, pushed.frequency_offset, "the device keeps its own crystal calibration");
    TEST_ASSERT_TRUE_MESSAGE(pushed.sx126x_rx_boosted_gain, "the device keeps its own front-end setting");

    const meshtastic_AdminMessage get = adminIn(sent[1]);
    TEST_ASSERT_EQUAL(meshtastic_AdminMessage_get_config_request_tag, get.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_AdminMessage_ConfigType_LORA_CONFIG, get.get_config_request);
    TEST_ASSERT_TRUE(sent[1].packet.decoded.want_response);
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Pushing, (int)radio->getState());

    deviceAnswersReadBack(sent[1], pushed);
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Ready, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(0, captureRouter->received.size(), "the admin reply is not a LoRa frame");
}

// A device already transparent with our settings needs no set_config: reconnects must not rewrite its flash.
void test_device_already_set_up_goes_ready_without_pushing(void)
{
    bringReady();
}

// Firmware that predates transparent_mode drops the unknown field: it takes our radio settings, but the
// read-back comes back without transparent_mode. The radio must refuse that device rather than hand frames
// to a node that will route them itself.
void test_firmware_without_transparent_mode_is_refused(void)
{
    radio->runOnce();
    takeToRadio();
    deviceSendsConfigDump(meshtastic_Config_LoRaConfig_init_zero);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());

    meshtastic_Config_LoRaConfig tookItAll = adminIn(sent[0]).set_config.payload_variant.lora;
    tookItAll.transparent_mode = false;
    deviceAnswersReadBack(sent[1], tookItAll);
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Unsupported, (int)radio->getState());
    TEST_ASSERT_FALSE(fakeLink->isOpen());
    Time::advanceTestMillis(60000);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE(1, fakeLink->opens, "an unsupported device is not retried");
}

// A release the device reports as older than transparent mode is refused before anything is written to it:
// old firmware would apply and save our region and preset, and only then fail the read-back.
void test_old_firmware_version_is_refused_before_any_write(void)
{
    radio->runOnce();
    takeToRadio();
    deviceSendsConfigDump(meshtastic_Config_LoRaConfig_init_zero, "2.7.15.1234abc");
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Unsupported, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "no set_config may reach old firmware");
}

// A device that rejects a LoRa config restores its old one whole, so transparent_mode reads back false
// there too. That is not old firmware: the radio waits and retries, and retries at once when this node's
// config changes, instead of writing the device off until restart.
void test_device_refusing_our_settings_is_retried_not_written_off(void)
{
    radio->runOnce();
    takeToRadio();
    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    deviceNow.use_preset = true;
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());

    deviceAnswersReadBack(sent[1], deviceNow); // kept its own settings
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Refused, (int)radio->getState());
    TEST_ASSERT_TRUE_MESSAGE(fakeLink->isOpen(), "a refusal is not a reason to drop the link");

    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    TEST_ASSERT_TRUE(radio->reconfigure());
    sent = takeToRadio();
    TEST_ASSERT_EQUAL_MESSAGE(2, sent.size(), "a config change retries straight away");
    TEST_ASSERT_EQUAL(meshtastic_AdminMessage_set_config_tag, adminIn(sent[0]).which_payload_variant);
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Pushing, (int)radio->getState());
}

// The timed retry from Refused, without a config change to prompt it: after REFUSED_RETRY_MS (5 min) the
// radio asks for the config again, and not before. If the device has since been set up the way we want it
// (its owner changed it by hand), that retry reaches Ready; a retry that re-entered Configuring but could never
// leave it would strand the radio until a restart.
void test_refused_device_is_retried_after_the_backoff(void)
{
    radio->runOnce();
    takeToRadio();
    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    deviceNow.use_preset = true;
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());
    deviceAnswersReadBack(sent[1], deviceNow);
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Refused, (int)radio->getState());

    Time::advanceTestMillis(4 * 60 * 1000);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE((int)TransparentRadio::State::Refused, (int)radio->getState(), "no retry inside the backoff");

    Time::advanceTestMillis(60 * 1000);
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Configuring, (int)radio->getState());
    bool askedAgain = false;
    for (const meshtastic_ToRadio &tr : takeToRadio()) // a heartbeat can share this pass
        askedAgain |= tr.which_payload_variant == meshtastic_ToRadio_want_config_id_tag;
    TEST_ASSERT_TRUE(askedAgain);

    deviceSendsConfigDump(expectedDeviceLora());
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE((int)TransparentRadio::State::Ready, (int)radio->getState(), "the retry reaches Ready");
}

// ---------------------------------------------------------------------------
// LORA_24 (2.4 GHz)
// ---------------------------------------------------------------------------

/// The get_config read-back among `sent`; a heartbeat can share the pass, so don't assume its position.
static meshtastic_ToRadio readBackIn(const std::vector<meshtastic_ToRadio> &sent)
{
    for (const meshtastic_ToRadio &tr : sent)
        if (tr.which_payload_variant == meshtastic_ToRadio_packet_tag &&
            adminIn(tr).which_payload_variant == meshtastic_AdminMessage_get_config_request_tag)
            return tr;
    TEST_FAIL_MESSAGE("no read-back request was sent");
    return meshtastic_ToRadio_init_zero;
}

/// Put this node on LORA_24 and run the handshake against a device on `deviceNow` up to the read-back.
/// Returns the LoRa config pushed; `readBack` is the request the device answers next.
static meshtastic_Config_LoRaConfig pushLora24(const meshtastic_Config_LoRaConfig &deviceNow, meshtastic_ToRadio &readBack)
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    initRegion();
    TEST_ASSERT_TRUE(radio->reconfigure()); // still Disconnected: this only re-derives the modem settings
    radio->runOnce();
    takeToRadio();
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());
    readBack = readBackIn(sent);
    return adminIn(sent[0]).set_config.payload_variant.lora;
}

// meshtasticd can't see the device's chip, so it lets the device decide. wideLora() reports 2.4 GHz, which is
// what AdminModule's LORA_24 gate asks the router's interface (refusing LORA_24 on meshtasticd outright when it
// said no). LORA_24 then goes to the device unchanged, and a device with a 2.4 GHz chip goes Ready, unreported.
void test_lora_24_is_pushed_and_a_device_that_takes_it_goes_ready(void)
{
    TEST_ASSERT_TRUE_MESSAGE(radio->wideLora(), "AdminModule refuses LORA_24 unless the interface reports 2.4 GHz");
    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    deviceNow.use_preset = true;
    meshtastic_ToRadio readBack = meshtastic_ToRadio_init_zero;
    const meshtastic_Config_LoRaConfig pushed = pushLora24(deviceNow, readBack);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_LORA_24, pushed.region);
    const uint32_t notifiedBefore = mockService->notificationCount;

    deviceAnswersReadBack(readBack, pushed);
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Ready, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(notifiedBefore, mockService->notificationCount, "nothing to tell the client");
}

// A device without a 2.4 GHz chip keeps its own region, so nothing is on air: that has to reach the client, not
// only the log. It is told once, as LORA_REGION_UNSUPPORTED_BY_RADIO naming the region. The timed retries that
// follow stay quiet (one notification every 5 minutes would bury the app), and a config change re-arms it.
void test_a_device_that_refuses_lora_24_is_reported_to_the_client_once(void)
{
    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    deviceNow.use_preset = true;
    meshtastic_ToRadio readBack = meshtastic_ToRadio_init_zero;
    pushLora24(deviceNow, readBack);
    const uint32_t notifiedBefore = mockService->notificationCount;

    deviceAnswersReadBack(readBack, deviceNow); // kept US
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Refused, (int)radio->getState());
    TEST_ASSERT_EQUAL(notifiedBefore + 1, mockService->notificationCount);
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_REGION_UNSUPPORTED_BY_RADIO, mockService->lastType);
    TEST_ASSERT_TRUE_MESSAGE(mockService->lastMessage.find("LORA_24") != std::string::npos, "the message names the region");

    Time::advanceTestMillis(5 * 60 * 1000); // the timed retry, refused the same way
    radio->runOnce();
    takeToRadio();
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();
    deviceAnswersReadBack(readBackIn(takeToRadio()), deviceNow);
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Refused, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(notifiedBefore + 1, mockService->notificationCount, "a timed retry is not news");

    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST; // a new config, refused again
    TEST_ASSERT_TRUE(radio->reconfigure());
    deviceAnswersReadBack(readBackIn(takeToRadio()), deviceNow);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE(notifiedBefore + 2, mockService->notificationCount, "a config change is reported afresh");
}

// A device with lockdown on drops an unauthenticated client's admin traffic and mesh packets alike. The radio
// says so and backs off, instead of pushing into silence and timing out every 15 s.
void test_locked_device_is_reported_not_pushed_to(void)
{
    radio->runOnce();
    takeToRadio();
    meshtastic_FromRadio locked = meshtastic_FromRadio_init_zero;
    locked.which_payload_variant = meshtastic_FromRadio_lockdown_status_tag;
    locked.lockdown_status.state = meshtastic_LockdownStatus_State_LOCKED;
    deviceSends(locked);
    deviceSendsConfigDump(meshtastic_Config_LoRaConfig_init_zero);
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Refused, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "nothing is pushed to a locked device");
}

// Without the device's own LoRa config the radio can't keep the device's calibration fields, so it pushes
// nothing and lets the handshake timeout ask again.
void test_no_lora_config_from_the_device_means_no_push(void)
{
    radio->runOnce();
    takeToRadio();
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    fr.my_info.my_node_num = DEVICE_NODE;
    deviceSends(fr);
    fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fr.config_complete_id = SPECIAL_NONCE_ONLY_CONFIG;
    deviceSends(fr);
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Configuring, (int)radio->getState());
    TEST_ASSERT_EQUAL(0, takeToRadio().size());

    Time::advanceTestMillis(15000);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_ToRadio_want_config_id_tag, sent[0].which_payload_variant,
                              "the handshake timeout asks again");
}

// With no region set here, pushing would set the device's region to UNSET (and turn its TX off) and persist
// that. Nothing is pushed until this node has a region; setting one pushes at once.
void test_no_region_here_leaves_the_device_alone_until_one_is_set(void)
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();
    TEST_ASSERT_TRUE(radio->reconfigure());
    radio->runOnce();
    takeToRadio();
    meshtastic_Config_LoRaConfig deviceNow = meshtastic_Config_LoRaConfig_init_zero;
    deviceNow.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    deviceSendsConfigDump(deviceNow);
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::WaitingForRegion, (int)radio->getState());
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "no admin traffic without a region");

    Time::advanceTestMillis(60000);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE((int)TransparentRadio::State::WaitingForRegion, (int)radio->getState(),
                              "waiting for a region is not a stuck handshake");

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
    TEST_ASSERT_TRUE(radio->reconfigure());
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, adminIn(sent[0]).set_config.payload_variant.lora.region);
}

// ---------------------------------------------------------------------------
// TX
// ---------------------------------------------------------------------------

// The frame waits out its delay here, then goes to the device with the header fields and ciphertext only.
void test_frame_goes_out_at_its_due_time_with_header_and_ciphertext_only(void)
{
    bringReady();
    meshtastic_MeshPacket *p = makeOutgoing(0x5000, 3000);
    const meshtastic_MeshPacket original = *p;
    TEST_ASSERT_EQUAL(ERRNO_OK, radio->send(p));

    Time::advanceTestMillis(2999);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "nothing goes to the device before the frame is due");

    Time::advanceTestMillis(1);
    radio->runOnce();
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_ToRadio_packet_tag, sent[0].which_payload_variant);
    const meshtastic_MeshPacket &out = sent[0].packet;
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, out.which_payload_variant);
    TEST_ASSERT_EQUAL_HEX32(original.from, out.from);
    TEST_ASSERT_EQUAL_HEX32(original.to, out.to);
    TEST_ASSERT_EQUAL_HEX32(original.id, out.id);
    TEST_ASSERT_EQUAL_HEX8(original.channel, out.channel);
    TEST_ASSERT_EQUAL_UINT8(original.hop_limit, out.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(original.hop_start, out.hop_start);
    TEST_ASSERT_TRUE(out.want_ack);
    TEST_ASSERT_TRUE(out.via_mqtt);
    TEST_ASSERT_EQUAL_HEX8(original.next_hop, out.next_hop);
    TEST_ASSERT_EQUAL_HEX8(original.relay_node, out.relay_node);
    TEST_ASSERT_EQUAL_UINT32(original.encrypted.size, out.encrypted.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original.encrypted.bytes, out.encrypted.bytes, original.encrypted.size);
    TEST_ASSERT_EQUAL_MESSAGE(0, out.tx_after, "the device must not delay it again");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, out.rx_snr, "rx metadata would make the device weight a relay delay");
    TEST_ASSERT_FALSE(out.has_rx_rssi);
    TEST_ASSERT_FALSE(out.has_rx_time);
    TEST_ASSERT_EQUAL_UINT32(1, radio->txGood);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, testAirTime->utilizationTXPercent());
}

// Managed flooding cancels a relay when someone else relays first. That only works while the frame is
// still here, so a cancelled frame must never reach the device.
void test_cancelled_frame_never_reaches_the_device(void)
{
    bringReady();
    TEST_ASSERT_EQUAL(ERRNO_OK, radio->send(makeOutgoing(0x5001, 3000)));
    TEST_ASSERT_TRUE(radio->findInTxQueue(LOCAL_NODE, 0x5001));
    TEST_ASSERT_TRUE(radio->cancelSending(LOCAL_NODE, 0x5001));

    Time::advanceTestMillis(10000);
    radio->runOnce();
    TEST_ASSERT_EQUAL(0, takeToRadio().size());
}

// The device answers each frame with its queue's free slots. At zero the radio holds its frames until a
// QueueStatus says there is room, instead of having the device drop them.
void test_full_device_queue_holds_frames_until_space_is_reported(void)
{
    bringReady();
    deviceSendsQueueFree(0);
    TEST_ASSERT_EQUAL(ERRNO_OK, radio->send(makeOutgoing(0x5002, 1000)));
    Time::advanceTestMillis(5000);
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE(0, takeToRadio().size(), "held while the device queue is full");

    deviceSendsQueueFree(3);
    Time::advanceTestMillis(100);
    radio->runOnce(); // reads the QueueStatus, then forwards the held frame in the same pass
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL_HEX32(0x5002, sent[0].packet.id);
}

// ---------------------------------------------------------------------------
// RX
// ---------------------------------------------------------------------------

// Encrypted frames are LoRa traffic and go to the router as heard. Decoded packets are the device's own
// local traffic, and frames heard before Ready came from a device that may still be routing for itself.
void test_only_encrypted_frames_after_ready_reach_the_router(void)
{
    meshtastic_FromRadio heard = meshtastic_FromRadio_init_zero;
    heard.which_payload_variant = meshtastic_FromRadio_packet_tag;
    heard.packet.from = REMOTE_NODE;
    heard.packet.to = NODENUM_BROADCAST;
    heard.packet.id = 0x6000;
    heard.packet.channel = 0x42;
    heard.packet.hop_limit = 2;
    heard.packet.hop_start = 3;
    heard.packet.relay_node = 0x0b;
    heard.packet.rx_snr = 4.25f;
    heard.packet.rx_rssi = -101;
    heard.packet.has_rx_rssi = true;
    heard.packet.rx_time = 999;
    heard.packet.has_rx_time = true;
    heard.packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    heard.packet.encrypted.size = 20;
    memset(heard.packet.encrypted.bytes, 0x5A, heard.packet.encrypted.size);

    radio->runOnce();
    takeToRadio();
    deviceSends(heard); // before the handshake completes
    radio->runOnce();
    TEST_ASSERT_EQUAL_MESSAGE(0, captureRouter->received.size(), "nothing is delivered before Ready");

    deviceSendsConfigDump(expectedDeviceLora());
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Ready, (int)radio->getState());

    deviceSends(heard);
    meshtastic_FromRadio local = meshtastic_FromRadio_init_zero;
    local.which_payload_variant = meshtastic_FromRadio_packet_tag;
    local.packet.from = DEVICE_NODE;
    local.packet.to = DEVICE_NODE;
    local.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    local.packet.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    deviceSends(local);
    radio->runOnce();

    TEST_ASSERT_EQUAL(1, captureRouter->received.size());
    const meshtastic_MeshPacket &in = captureRouter->received[0];
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, in.which_payload_variant);
    TEST_ASSERT_EQUAL_HEX32(0x6000, in.id);
    TEST_ASSERT_EQUAL_HEX32(REMOTE_NODE, in.from);
    TEST_ASSERT_EQUAL_HEX8(0x42, in.channel);
    TEST_ASSERT_EQUAL_HEX8(0x0b, in.relay_node);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(4.25f, in.rx_snr, "the device's SNR drives this node's relay delay");
    TEST_ASSERT_EQUAL_INT32(-101, in.rx_rssi);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA, in.transport_mechanism);
    TEST_ASSERT_FALSE_MESSAGE(in.has_rx_time, "the router stamps this node's own arrival time");
    TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(heard.packet.encrypted.bytes, in.encrypted.bytes, heard.packet.encrypted.size),
                              "ciphertext unchanged");
    TEST_ASSERT_EQUAL_UINT32(1, radio->rxGood);
}

// ---------------------------------------------------------------------------
// Link and lifecycle
// ---------------------------------------------------------------------------

// A dropped link makes the radio "off": due frames are dropped, not held. It reconnects on its own and
// redoes the handshake, since the device may have rebooted or been swapped.
void test_link_loss_drops_due_frames_then_reconnects_and_handshakes_again(void)
{
    bringReady();
    TEST_ASSERT_EQUAL(ERRNO_OK, radio->send(makeOutgoing(0x5003, 2000)));
    fakeLink->readFails = true;
    radio->runOnce();
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Disconnected, (int)radio->getState());

    fakeLink->readFails = false;
    Time::advanceTestMillis(2000);
    radio->runOnce();
    TEST_ASSERT_EQUAL_UINT32(1, radio->txDrop);
    TEST_ASSERT_EQUAL_MESSAGE(1, fakeLink->opens, "reconnect waits out its interval");

    Time::advanceTestMillis(5000);
    radio->runOnce();
    TEST_ASSERT_EQUAL(2, fakeLink->opens);
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_ToRadio_want_config_id_tag, sent[0].which_payload_variant);
}

// A device that reboots says so; its settings persisted, but the radio re-checks them before trusting it.
void test_device_reboot_restarts_the_handshake(void)
{
    bringReady();
    meshtastic_FromRadio rebooted = meshtastic_FromRadio_init_zero;
    rebooted.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
    rebooted.rebooted = true;
    deviceSends(rebooted);
    radio->runOnce();

    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Configuring, (int)radio->getState());
    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL(meshtastic_ToRadio_want_config_id_tag, sent[0].which_payload_variant);
}

// This node's LoRa config can change at runtime (admin, the app). The device must follow.
void test_lora_config_change_is_pushed_to_the_device(void)
{
    bringReady();
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    TEST_ASSERT_TRUE(radio->reconfigure());

    std::vector<meshtastic_ToRadio> sent = takeToRadio();
    TEST_ASSERT_EQUAL(2, sent.size());
    const meshtastic_AdminMessage set = adminIn(sent[0]);
    TEST_ASSERT_EQUAL(meshtastic_AdminMessage_set_config_tag, set.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, set.set_config.payload_variant.lora.modem_preset);
    TEST_ASSERT_TRUE(set.set_config.payload_variant.lora.transparent_mode);
    TEST_ASSERT_EQUAL((int)TransparentRadio::State::Pushing, (int)radio->getState());
}

// The endpoint string picks the transport, and nonsense is refused rather than guessed at.
void test_endpoint_parsing(void)
{
    TEST_ASSERT_NOT_NULL(makeTransparentLink("/dev/ttyACM0").get());
    TEST_ASSERT_NOT_NULL(makeTransparentLink("meshnode.local").get());
    TEST_ASSERT_NOT_NULL(makeTransparentLink("192.168.1.20:4403").get());
    TEST_ASSERT_NOT_NULL(makeTransparentLink("[fe80::1]:4403").get());
    TEST_ASSERT_NOT_NULL(makeTransparentLink("fe80::1").get());
    TEST_ASSERT_EQUAL_STRING("192.168.1.20:4403", makeTransparentLink("192.168.1.20:4403")->describe().c_str());

    TEST_ASSERT_NULL(makeTransparentLink("").get());
    TEST_ASSERT_NULL(makeTransparentLink("host:0").get());
    TEST_ASSERT_NULL(makeTransparentLink("host:70000").get());
    TEST_ASSERT_NULL(makeTransparentLink("host:abc").get());
    TEST_ASSERT_NULL_MESSAGE(makeTransparentLink("host:").get(), "a separator with no port is not the default port");
    TEST_ASSERT_NULL(makeTransparentLink("[fe80::1]:").get());
    TEST_ASSERT_NULL(makeTransparentLink("[fe80::1").get());
    TEST_ASSERT_NULL(makeTransparentLink(":4403").get());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_handshake_pushes_our_lora_config_with_transparent_mode);
    RUN_TEST(test_device_already_set_up_goes_ready_without_pushing);
    RUN_TEST(test_firmware_without_transparent_mode_is_refused);
    RUN_TEST(test_old_firmware_version_is_refused_before_any_write);
    RUN_TEST(test_device_refusing_our_settings_is_retried_not_written_off);
    RUN_TEST(test_no_region_here_leaves_the_device_alone_until_one_is_set);
    RUN_TEST(test_refused_device_is_retried_after_the_backoff);
    RUN_TEST(test_lora_24_is_pushed_and_a_device_that_takes_it_goes_ready);
    RUN_TEST(test_a_device_that_refuses_lora_24_is_reported_to_the_client_once);
    RUN_TEST(test_locked_device_is_reported_not_pushed_to);
    RUN_TEST(test_no_lora_config_from_the_device_means_no_push);
    RUN_TEST(test_frame_goes_out_at_its_due_time_with_header_and_ciphertext_only);
    RUN_TEST(test_cancelled_frame_never_reaches_the_device);
    RUN_TEST(test_full_device_queue_holds_frames_until_space_is_reported);
    RUN_TEST(test_only_encrypted_frames_after_ready_reach_the_router);
    RUN_TEST(test_link_loss_drops_due_frames_then_reconnects_and_handshakes_again);
    RUN_TEST(test_device_reboot_restarts_the_handshake);
    RUN_TEST(test_lora_config_change_is_pushed_to_the_device);
    RUN_TEST(test_endpoint_parsing);
    exit(UNITY_END());
}

void loop() {}

#else // no POSIX serial/sockets on this build

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
