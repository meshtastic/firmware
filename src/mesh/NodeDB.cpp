#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "../detect/ScanI2C.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "Default.h"
#include "FSCommon.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "Router.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TransmitHistory.h"
#include "TypeConversions.h"
#include "error.h"
#include "main.h"
#include "memory/MemAudit.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/deviceonly_legacy.pb.h"
#include "meshUtils.h"
#include "modules/NeighborInfoModule.h"
#if HAS_VARIABLE_HOPS
#include "modules/HopScalingModule.h"
#endif
#include "xmodem.h"
#include <ErriezCRC32.h>
#include <algorithm>
#include <pb_decode.h>
#include <pb_encode.h>
#include <power/PowerHAL.h>
#include <vector>

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#include "security/SecureZero.h"
#endif

#ifdef ARCH_ESP32
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif
#include "SPILock.h"
#include "modules/StoreForwardModule.h"
#include <Preferences.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <nvs_flash.h>
#include <soc/efuse_reg.h>
#include <soc/soc.h>
#endif

#ifdef ARCH_PORTDUINO
#include "modules/StoreForwardModule.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

#ifdef ARCH_NRF52
#include <bluefruit.h>
#include <utility/bonding.h>
#endif

#ifdef ARCH_RP2040
#include <hardware/watchdog.h>
#endif

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_WIFI
#include <MeshtasticOTA.h>
#endif

NodeDB *nodeDB = nullptr;

// we have plenty of ram so statically alloc this tempbuf (for now)
EXT_RAM_BSS_ATTR meshtastic_DeviceState devicestate;
meshtastic_MyNodeInfo &myNodeInfo = devicestate.my_node;
meshtastic_NodeDatabase nodeDatabase;
meshtastic_LocalConfig config;
meshtastic_DeviceUIConfig uiconfig{.screen_brightness = 153, .screen_timeout = 30};
meshtastic_LocalModuleConfig moduleConfig;
meshtastic_ChannelFile channelFile;

#ifdef USERPREFS_USE_ADMIN_KEY_0
static unsigned char userprefs_admin_key_0[] = USERPREFS_USE_ADMIN_KEY_0;
#endif
#ifdef USERPREFS_USE_ADMIN_KEY_1
static unsigned char userprefs_admin_key_1[] = USERPREFS_USE_ADMIN_KEY_1;
#endif
#ifdef USERPREFS_USE_ADMIN_KEY_2
static unsigned char userprefs_admin_key_2[] = USERPREFS_USE_ADMIN_KEY_2;
#endif

// Weak empty variant initialization function.
// May be redefined by variant files.
void variantDefaultConfig() __attribute__((weak));
void variantDefaultConfig() {}

void variantDefaultModuleConfig() __attribute__((weak));
void variantDefaultModuleConfig() {}

#ifdef HELTEC_MESH_NODE_T114

uint32_t read8(uint8_t bits, uint8_t dummy, uint8_t cs, uint8_t sck, uint8_t mosi, uint8_t dc, uint8_t rst)
{
    uint32_t ret = 0;
    uint8_t SDAPIN = mosi;
    pinMode(SDAPIN, INPUT_PULLUP);
    digitalWrite(dc, HIGH);
    for (int i = 0; i < dummy; i++) { // any dummy clocks
        digitalWrite(sck, HIGH);
        delay(1);
        digitalWrite(sck, LOW);
        delay(1);
    }
    for (int i = 0; i < bits; i++) { // read results
        ret <<= 1;
        delay(1);
        if (digitalRead(SDAPIN))
            ret |= 1;
        ;
        digitalWrite(sck, HIGH);
        delay(1);
        digitalWrite(sck, LOW);
    }
    return ret;
}

void write9(uint8_t val, uint8_t dc_val, uint8_t cs, uint8_t sck, uint8_t mosi, uint8_t dc, uint8_t rst)
{
    pinMode(mosi, OUTPUT);
    digitalWrite(dc, dc_val);
    for (int i = 0; i < 8; i++) { // send command
        digitalWrite(mosi, (val & 0x80) != 0);
        delay(1);
        digitalWrite(sck, HIGH);
        delay(1);
        digitalWrite(sck, LOW);
        val <<= 1;
    }
}

uint32_t readwrite8(uint8_t cmd, uint8_t bits, uint8_t dummy, uint8_t cs, uint8_t sck, uint8_t mosi, uint8_t dc, uint8_t rst)
{
    digitalWrite(cs, LOW);
    write9(cmd, 0, cs, sck, mosi, dc, rst);
    uint32_t ret = read8(bits, dummy, cs, sck, mosi, dc, rst);
    digitalWrite(cs, HIGH);
    return ret;
}

uint32_t get_st7789_id(uint8_t cs, uint8_t sck, uint8_t mosi, uint8_t dc, uint8_t rst)
{
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    pinMode(cs, OUTPUT);
    pinMode(sck, OUTPUT);
    pinMode(mosi, OUTPUT);
    pinMode(dc, OUTPUT);
    pinMode(rst, OUTPUT);
    digitalWrite(rst, LOW); // Hardware Reset
    delay(10);
    digitalWrite(rst, HIGH);
    delay(10);

    readwrite8(0x04, 24, 1, cs, sck, mosi, dc, rst);
    uint32_t ID = readwrite8(0x04, 24, 1, cs, sck, mosi, dc, rst); // ST7789 needs twice
    return ID;
}

#endif

// When armed by loadFromDisk, the decode callback writes satellite entries
// straight into these maps instead of the temp vectors. Nullptr = legacy
// push_back-to-vector path for backup/restore and other decoders.
namespace
{
#if !MESHTASTIC_EXCLUDE_POSITIONDB
std::map<NodeNum, meshtastic_PositionLite> *s_decodePositionsTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
std::map<NodeNum, meshtastic_DeviceMetrics> *s_decodeTelemetryTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
std::map<NodeNum, meshtastic_EnvironmentMetrics> *s_decodeEnvironmentTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_STATUSDB
std::map<NodeNum, meshtastic_StatusMessage> *s_decodeStatusTarget = nullptr;
#endif
} // namespace

bool meshtastic_NodeDatabase_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field)
{
    const auto *iter = reinterpret_cast<const pb_field_iter_t *>(field);
    switch (iter->tag) {
    case meshtastic_NodeDatabase_nodes_tag: {
        if (ostream) {
            const auto *vec = static_cast<const std::vector<meshtastic_NodeInfoLite> *>(iter->pData);
            for (auto item : *vec) {
                item.snr_q4 = (int32_t)(item.snr * 4.0f);
                item.snr = 0.0f;
                if (!pb_encode_tag_for_field(ostream, iter))
                    return false;
                if (!pb_encode_submessage(ostream, meshtastic_NodeInfoLite_fields, &item))
                    return false;
            }
        }
        if (istream && istream->bytes_left) {
            meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
            auto *vec = static_cast<std::vector<meshtastic_NodeInfoLite> *>(iter->pData);
            if (pb_decode(istream, meshtastic_NodeInfoLite_fields, &node)) {
                if (node.snr_q4)
                    node.snr = node.snr_q4 / 4.0f;
                node.snr_q4 = 0;
                vec->push_back(node);
            }
        }
        return true;
    }
    case meshtastic_NodeDatabase_positions_tag: {
        if (ostream) {
            const auto *vec = static_cast<const std::vector<meshtastic_NodePositionEntry> *>(iter->pData);
            for (auto item : *vec) {
                if (!pb_encode_tag_for_field(ostream, iter))
                    return false;
                if (!pb_encode_submessage(ostream, meshtastic_NodePositionEntry_fields, &item))
                    return false;
            }
        }
        if (istream && istream->bytes_left) {
            meshtastic_NodePositionEntry entry = meshtastic_NodePositionEntry_init_zero;
            if (pb_decode(istream, meshtastic_NodePositionEntry_fields, &entry)) {
#if !MESHTASTIC_EXCLUDE_POSITIONDB
                if (s_decodePositionsTarget) {
                    if (entry.has_position)
                        (*s_decodePositionsTarget)[entry.num] = entry.position;
                    return true;
                }
#endif
                auto *vec = static_cast<std::vector<meshtastic_NodePositionEntry> *>(iter->pData);
                vec->push_back(entry);
            }
        }
        return true;
    }
    case meshtastic_NodeDatabase_telemetry_tag: {
        if (ostream) {
            const auto *vec = static_cast<const std::vector<meshtastic_NodeTelemetryEntry> *>(iter->pData);
            for (auto item : *vec) {
                if (!pb_encode_tag_for_field(ostream, iter))
                    return false;
                if (!pb_encode_submessage(ostream, meshtastic_NodeTelemetryEntry_fields, &item))
                    return false;
            }
        }
        if (istream && istream->bytes_left) {
            meshtastic_NodeTelemetryEntry entry = meshtastic_NodeTelemetryEntry_init_zero;
            if (pb_decode(istream, meshtastic_NodeTelemetryEntry_fields, &entry)) {
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
                if (s_decodeTelemetryTarget) {
                    if (entry.has_device_metrics)
                        (*s_decodeTelemetryTarget)[entry.num] = entry.device_metrics;
                    return true;
                }
#endif
                auto *vec = static_cast<std::vector<meshtastic_NodeTelemetryEntry> *>(iter->pData);
                vec->push_back(entry);
            }
        }
        return true;
    }
    case meshtastic_NodeDatabase_status_tag: {
        if (ostream) {
            const auto *vec = static_cast<const std::vector<meshtastic_NodeStatusEntry> *>(iter->pData);
            for (auto item : *vec) {
                if (!pb_encode_tag_for_field(ostream, iter))
                    return false;
                if (!pb_encode_submessage(ostream, meshtastic_NodeStatusEntry_fields, &item))
                    return false;
            }
        }
        if (istream && istream->bytes_left) {
            meshtastic_NodeStatusEntry entry = meshtastic_NodeStatusEntry_init_zero;
            if (pb_decode(istream, meshtastic_NodeStatusEntry_fields, &entry)) {
#if !MESHTASTIC_EXCLUDE_STATUSDB
                if (s_decodeStatusTarget) {
                    if (entry.has_status)
                        (*s_decodeStatusTarget)[entry.num] = entry.status;
                    return true;
                }
#endif
                auto *vec = static_cast<std::vector<meshtastic_NodeStatusEntry> *>(iter->pData);
                vec->push_back(entry);
            }
        }
        return true;
    }
    case meshtastic_NodeDatabase_environment_tag: {
        if (ostream) {
            const auto *vec = static_cast<const std::vector<meshtastic_NodeEnvironmentEntry> *>(iter->pData);
            for (auto item : *vec) {
                if (!pb_encode_tag_for_field(ostream, iter))
                    return false;
                if (!pb_encode_submessage(ostream, meshtastic_NodeEnvironmentEntry_fields, &item))
                    return false;
            }
        }
        if (istream && istream->bytes_left) {
            meshtastic_NodeEnvironmentEntry entry = meshtastic_NodeEnvironmentEntry_init_zero;
            if (pb_decode(istream, meshtastic_NodeEnvironmentEntry_fields, &entry)) {
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
                if (s_decodeEnvironmentTarget) {
                    if (entry.has_environment_metrics)
                        (*s_decodeEnvironmentTarget)[entry.num] = entry.environment_metrics;
                    return true;
                }
#endif
                auto *vec = static_cast<std::vector<meshtastic_NodeEnvironmentEntry> *>(iter->pData);
                vec->push_back(entry);
            }
        }
        return true;
    }
    default:
        return true;
    }
}

void NodeDB::armNodeDatabaseDecodeTargets()
{
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    nodePositions.clear();
    s_decodePositionsTarget = &nodePositions;
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    nodeTelemetry.clear();
    s_decodeTelemetryTarget = &nodeTelemetry;
#endif
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    nodeEnvironment.clear();
    s_decodeEnvironmentTarget = &nodeEnvironment;
#endif
#if !MESHTASTIC_EXCLUDE_STATUSDB
    nodeStatus.clear();
    s_decodeStatusTarget = &nodeStatus;
#endif
}

void NodeDB::disarmNodeDatabaseDecodeTargets()
{
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    s_decodePositionsTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    s_decodeTelemetryTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    s_decodeEnvironmentTarget = nullptr;
#endif
#if !MESHTASTIC_EXCLUDE_STATUSDB
    s_decodeStatusTarget = nullptr;
#endif
}

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
uint32_t radioGeneration;

// FIXME - move this somewhere else
extern void getMacAddr(uint8_t *dmac);

/**
 *
 * Normally userids are unique and start with +country code to look like Signal phone numbers.
 * But there are some special ids used when we haven't yet been configured by a user.  In that case
 * we use !macaddr (no colons).
 */
meshtastic_User &owner = devicestate.owner;

// The slim NodeInfoLite header defines the local long_name cap; the wire-facing
// meshtastic_User stays wider so names from senders built against the older
// 39-byte limit still decode (nanopb halts on string overflow).
static_assert(MAX_LONG_NAME_BYTES + 1 == sizeof(meshtastic_NodeInfoLite::long_name),
              "MAX_LONG_NAME_BYTES must match the NodeInfoLite storage width");
static_assert(sizeof(meshtastic_User::long_name) > MAX_LONG_NAME_BYTES,
              "wire User.long_name must be wider than the local cap so clampLongName stays in bounds");

meshtastic_Position localPosition = meshtastic_Position_init_default;
meshtastic_CriticalErrorCode error_code =
    meshtastic_CriticalErrorCode_NONE; // For the error code, only show values from this boot (discard value from flash)
uint32_t error_address = 0;

static uint8_t ourMacAddr[6];

NodeDB::NodeDB()
{
    LOG_INFO("Init NodeDB");
    loadFromDisk();
    cleanupMeshDB();

    uint32_t devicestateCRC = crc32Buffer(&devicestate, sizeof(devicestate));
    uint32_t nodeDatabaseCRC = crc32Buffer(&nodeDatabase, sizeof(nodeDatabase));
    uint32_t configCRC = crc32Buffer(&config, sizeof(config));
    uint32_t channelFileCRC = crc32Buffer(&channelFile, sizeof(channelFile));

    int saveWhat = 0;
    // Get device unique id
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    uint32_t unique_id[4];
    // ESP32 factory burns a unique id in efuse for S2+ series and evidently C3+ series
    // This is used for HMACs in the esp-rainmaker AIOT platform and seems to be a good choice for us
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, unique_id, sizeof(unique_id) * 8);
    if (err == ESP_OK) {
        memcpy(myNodeInfo.device_id.bytes, unique_id, sizeof(unique_id));
        myNodeInfo.device_id.size = 16;
    } else {
        LOG_WARN("Failed to read unique id from efuse");
    }
#elif defined(ARCH_NRF54L15)
    // nRF54L15: DEVICEID is under FICR->INFO sub-struct (not top-level as on nRF52)
    uint64_t device_id_start = ((uint64_t)NRF_FICR->INFO.DEVICEID[1] << 32) | NRF_FICR->INFO.DEVICEID[0];
    uint64_t device_id_end = ((uint64_t)NRF_FICR->DEVICEADDR[1] << 32) | NRF_FICR->DEVICEADDR[0];
    memcpy(myNodeInfo.device_id.bytes, &device_id_start, sizeof(device_id_start));
    memcpy(myNodeInfo.device_id.bytes + sizeof(device_id_start), &device_id_end, sizeof(device_id_end));
    myNodeInfo.device_id.size = 16;
#elif defined(ARCH_NRF52)
    // Nordic applies a FIPS compliant Random ID to each chip at the factory
    // We concatenate the device address to the Random ID to create a unique ID for now
    // This will likely utilize a crypto module in the future
    uint64_t device_id_start = ((uint64_t)NRF_FICR->DEVICEID[1] << 32) | NRF_FICR->DEVICEID[0];
    uint64_t device_id_end = ((uint64_t)NRF_FICR->DEVICEADDR[1] << 32) | NRF_FICR->DEVICEADDR[0];
    memcpy(myNodeInfo.device_id.bytes, &device_id_start, sizeof(device_id_start));
    memcpy(myNodeInfo.device_id.bytes + sizeof(device_id_start), &device_id_end, sizeof(device_id_end));
    myNodeInfo.device_id.size = 16;
    // Uncomment below to print the device id
#elif ARCH_PORTDUINO
    if (portduino_config.has_device_id) {
        memcpy(myNodeInfo.device_id.bytes, portduino_config.device_id, 16);
        myNodeInfo.device_id.size = 16;
    }
#else
    // FIXME - implement for other platforms
#endif

    // if (myNodeInfo.device_id.size == 16) {
    //     std::string deviceIdHex;
    //     for (size_t i = 0; i < myNodeInfo.device_id.size; ++i) {
    //         char buf[3];
    //         snprintf(buf, sizeof(buf), "%02X", myNodeInfo.device_id.bytes[i]);
    //         deviceIdHex += buf;
    //     }
    //     LOG_DEBUG("Device ID (HEX): %s", deviceIdHex.c_str());
    // }

    // likewise - we always want the app requirements to come from the running appload
    myNodeInfo.min_app_version = 30200; // format is Mmmss (where M is 1+the numeric major number. i.e. 30200 means 2.2.00
    pickNewNodeNum();

    // Set our board type so we can share it with others
    owner.hw_model = HW_VENDOR;
    // Ensure user (nodeinfo) role is set to whatever we're configured to
    owner.role = config.device.role;
    // Ensure macaddr is set to our macaddr as it will be copied in our info below
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));
    // Ensure owner.id is always derived from the node number
    snprintf(owner.id, sizeof(owner.id), "!%08x", getNodeNum());

    if (!config.has_security) {
        config.has_security = true;
        config.security = meshtastic_Config_SecurityConfig_init_default;
        config.security.serial_enabled = config.device.serial_enabled;
        config.security.is_managed = config.device.is_managed;
    }

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    // Generate crypto keys if needed using consolidated function
    // Set my node num uint32 value to bytes from the public key (if we have one)
    // Generate identity and crypto keys if needed; this will create a new identity if one does not exist.
    // Skip on a degraded boot: the keypair isn't in RAM, so minting one would change our NodeNum.
    if (!configDecodeFailed)
        generateCryptoKeyPair(nullptr);
#elif !(MESHTASTIC_EXCLUDE_PKI)
    // Calculate Curve25519 public and private keys
    if (config.security.private_key.size == 32 && config.security.public_key.size == 32) {
        owner.public_key.size = config.security.public_key.size;
        memcpy(owner.public_key.bytes, config.security.public_key.bytes, config.security.public_key.size);
        crypto->setDHPrivateKey(config.security.private_key.bytes);
        // Set my node num uint32 value to bytes from the new public key
        myNodeInfo.my_node_num = crc32Buffer(config.security.public_key.bytes, config.security.public_key.size);
    }
#endif
    // Identity is now established, so run the self-care pass on the store
    // loadFromDisk() deliberately left untrimmed: confirm self, trim/demote only
    // non-self overflow, pin self to index 0, rewrite once if healed.
    nodeDBSelfCare();

    // If we migrated from legacy during loadFromDisk(), persist the migrated DB
    // only after identity and self-care are established.
    if (migrationSavePending) {
        saveNodeDatabaseToDisk();
        migrationSavePending = false;
    }

    // If node database has not been saved for the first time, save it now
#ifdef FSCom
    if (!FSCom.exists(nodeDatabaseFileName)) {
        saveNodeDatabaseToDisk();
    }
#endif

#ifdef ARCH_ESP32
    Preferences preferences;
    preferences.begin("meshtastic", false);
    myNodeInfo.reboot_count = preferences.getUInt("rebootCounter", 0);
    preferences.end();
    LOG_DEBUG("Number of Device Reboots: %d", myNodeInfo.reboot_count);
#endif

    resetRadioConfig(); // If bogus settings got saved, then fix them
    // nodeDB->LOG_DEBUG("region=%d, NODENUM=0x%x, dbsize=%d", config.lora.region, myNodeInfo.my_node_num, numMeshNodes);

    // Uncomment below to always enable UDP broadcasts
    // config.network.enabled_protocols = meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST;

    // If we are setup to broadcast on any default channel slot (with default frequency slot semantics),
    // ensure that the telemetry intervals are coerced to the role-aware minimum value.
    if (channels.hasDefaultChannel()) {
        LOG_DEBUG("Coerce telemetry to role-aware minimum on defaults");
        moduleConfig.telemetry.device_update_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.device_update_interval, min_default_telemetry_interval_secs);
        moduleConfig.telemetry.environment_update_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.environment_update_interval, min_default_telemetry_interval_secs);
        moduleConfig.telemetry.air_quality_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.air_quality_interval, min_default_telemetry_interval_secs);
        moduleConfig.telemetry.power_update_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.power_update_interval, min_default_telemetry_interval_secs);
        moduleConfig.telemetry.health_update_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.health_update_interval, min_default_telemetry_interval_secs);
    }
    // Enforce position broadcast minimums if we would send positions over a default channel
    // Check channels the same way PositionModule::sendOurPosition() does - first channel with position_precision set
    bool positionUsesDefaultChannel = false;
    for (uint8_t i = 0; i < channels.getNumChannels(); i++) {
        if (channels.getByIndex(i).settings.has_module_settings &&
            channels.getByIndex(i).settings.module_settings.position_precision != 0) {
            positionUsesDefaultChannel = channels.isDefaultChannel(i);
            break;
        }
    }
    if (positionUsesDefaultChannel) {
        LOG_DEBUG("Coerce position broadcasts to role-aware minimum and smart broadcast min of 5 minutes on defaults");
        config.position.position_broadcast_secs =
            Default::getConfiguredOrMinimumValue(config.position.position_broadcast_secs, min_default_broadcast_interval_secs);
        config.position.broadcast_smart_minimum_interval_secs = Default::getConfiguredOrMinimumValue(
            config.position.broadcast_smart_minimum_interval_secs, min_default_broadcast_smart_minimum_interval_secs);
    }
    // FIXME: UINT32_MAX intervals overflows Apple clients until they are fully patched
    if (config.device.node_info_broadcast_secs > MAX_INTERVAL)
        config.device.node_info_broadcast_secs = MAX_INTERVAL;
    if (config.position.position_broadcast_secs > MAX_INTERVAL)
        config.position.position_broadcast_secs = MAX_INTERVAL;
    if (config.position.gps_update_interval > MAX_INTERVAL)
        config.position.gps_update_interval = MAX_INTERVAL;
    if (config.position.gps_attempt_time > MAX_INTERVAL)
        config.position.gps_attempt_time = MAX_INTERVAL;
    if (config.position.position_flags > MAX_INTERVAL)
        config.position.position_flags = MAX_INTERVAL;
    if (config.position.rx_gpio > MAX_INTERVAL)
        config.position.rx_gpio = MAX_INTERVAL;
    if (config.position.tx_gpio > MAX_INTERVAL)
        config.position.tx_gpio = MAX_INTERVAL;
    if (config.position.broadcast_smart_minimum_distance > MAX_INTERVAL)
        config.position.broadcast_smart_minimum_distance = MAX_INTERVAL;
    if (config.position.broadcast_smart_minimum_interval_secs > MAX_INTERVAL)
        config.position.broadcast_smart_minimum_interval_secs = MAX_INTERVAL;
    if (config.position.gps_en_gpio > MAX_INTERVAL)
        config.position.gps_en_gpio = MAX_INTERVAL;
    if (moduleConfig.neighbor_info.update_interval > MAX_INTERVAL)
        moduleConfig.neighbor_info.update_interval = MAX_INTERVAL;
    if (moduleConfig.telemetry.device_update_interval > MAX_INTERVAL)
        moduleConfig.telemetry.device_update_interval = MAX_INTERVAL;
    if (moduleConfig.telemetry.environment_update_interval > MAX_INTERVAL)
        moduleConfig.telemetry.environment_update_interval = MAX_INTERVAL;
    if (moduleConfig.telemetry.air_quality_interval > MAX_INTERVAL)
        moduleConfig.telemetry.air_quality_interval = MAX_INTERVAL;
    if (moduleConfig.telemetry.health_update_interval > MAX_INTERVAL)
        moduleConfig.telemetry.health_update_interval = MAX_INTERVAL;

    if (moduleConfig.mqtt.has_map_report_settings &&
        moduleConfig.mqtt.map_report_settings.publish_interval_secs < default_map_publish_interval_secs) {
        moduleConfig.mqtt.map_report_settings.publish_interval_secs = default_map_publish_interval_secs;
    }

    // If a fixed position is configured, restore the persisted position into localPosition at boot.
    // This keeps position broadcasts / MQTT map reports working after reboot on GPS-less nodes.
    if (config.position.fixed_position) {
        meshtastic_PositionLite fixedPos;
        if (copyNodePosition(getNodeNum(), fixedPos) && (fixedPos.latitude_i != 0 || fixedPos.longitude_i != 0)) {
            setLocalPosition(TypeConversions::ConvertToPosition(fixedPos));
            LOG_INFO("Restored fixed position to localPosition: lat=%d lon=%d", fixedPos.latitude_i, fixedPos.longitude_i);
        }
    }

    // Ensure that the neighbor info update interval is coerced to the minimum
    moduleConfig.neighbor_info.update_interval =
        Default::getConfiguredOrMinimumValue(moduleConfig.neighbor_info.update_interval, min_neighbor_info_broadcast_secs);

    // Don't let licensed users to rebroadcast encrypted packets
    if (owner.is_licensed) {
        config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY;
    }

#if !HAS_TFT
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        // On a device without MUI, this display mode makes no sense, and will break logic.
        config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
        config.bluetooth.enabled = true;
    }
#endif

    if (devicestateCRC != crc32Buffer(&devicestate, sizeof(devicestate)))
        saveWhat |= SEGMENT_DEVICESTATE;
    if (nodeDatabaseCRC != crc32Buffer(&nodeDatabase, sizeof(nodeDatabase)))
        saveWhat |= SEGMENT_NODEDATABASE;
    // Don't persist on a degraded boot: it would overwrite the unreadable-but-maybe-transient config file
    // with no-key UNSET defaults. Runtime reconfiguration (admin set) still persists normally.
    if (!configDecodeFailed && configCRC != crc32Buffer(&config, sizeof(config)))
        saveWhat |= SEGMENT_CONFIG;
    if (channelFileCRC != crc32Buffer(&channelFile, sizeof(channelFile)))
        saveWhat |= SEGMENT_CHANNELS;

    if (config.position.gps_enabled) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        config.position.gps_enabled = 0;
    }
#ifdef USERPREFS_FIRMWARE_EDITION
    myNodeInfo.firmware_edition = USERPREFS_FIRMWARE_EDITION;
#endif
#ifdef USERPREFS_FIXED_GPS
    if (myNodeInfo.reboot_count == 1) { // Check if First boot ever or after Factory Reset.
        meshtastic_Position fixedGPS = meshtastic_Position_init_default;
#ifdef USERPREFS_FIXED_GPS_LAT
        fixedGPS.latitude_i = (int32_t)(USERPREFS_FIXED_GPS_LAT * 1e7);
        fixedGPS.has_latitude_i = true;
#endif
#ifdef USERPREFS_FIXED_GPS_LON
        fixedGPS.longitude_i = (int32_t)(USERPREFS_FIXED_GPS_LON * 1e7);
        fixedGPS.has_longitude_i = true;
#endif
#ifdef USERPREFS_FIXED_GPS_ALT
        fixedGPS.altitude = USERPREFS_FIXED_GPS_ALT;
        fixedGPS.has_altitude = true;
#endif
#if defined(USERPREFS_FIXED_GPS_LAT) && defined(USERPREFS_FIXED_GPS_LON)
        fixedGPS.location_source = meshtastic_Position_LocSource_LOC_MANUAL;
        config.has_position = true;
#if !MESHTASTIC_EXCLUDE_POSITIONDB
        {
            concurrency::LockGuard guard(&satelliteMutex);
            nodePositions[info->num] = TypeConversions::ConvertToPositionLite(fixedGPS);
        }
#endif
        nodeDB->setLocalPosition(fixedGPS);
        config.position.fixed_position = true;
#endif
    }
#endif
    sortMeshDB();
    saveToDisk(saveWhat);
}

/**
 * Most (but not always) of the time we want to treat packets 'from' the local phone (where from == 0), as if they originated on
 * the local node. If from is zero this function returns our node number instead
 */
NodeNum getFrom(const meshtastic_MeshPacket *p)
{
    return (p->from == 0) ? nodeDB->getNodeNum() : p->from;
}

// Returns true if the packet originated from the local node
bool isFromUs(const meshtastic_MeshPacket *p)
{
    return p->from == 0 || p->from == nodeDB->getNodeNum();
}

// Returns true if the packet is destined to us
bool isToUs(const meshtastic_MeshPacket *p)
{
    return p->to == nodeDB->getNodeNum();
}

bool isBroadcast(uint32_t dest)
{
    return dest == NODENUM_BROADCAST || dest == NODENUM_BROADCAST_NO_LORA;
}

namespace
{
template <typename Map, typename Value> bool copySatelliteEntry(const Map &map, NodeNum n, Value &out)
{
    auto it = map.find(n);
    if (it == map.end())
        return false;
    out = it->second;
    return true;
}

template <typename Map> std::vector<NodeNum> snapshotSatelliteNodeNums(const Map &map, NodeNum exclude)
{
    std::vector<NodeNum> result;
    result.reserve(map.size());
    for (const auto &kv : map) {
        if (kv.first != exclude)
            result.push_back(kv.first);
    }
    return result;
}

// Drop the stalest entry of `map` (staleness proxied via the owner's
// last_heard; 0 = owner evicted, i.e. an orphan - first out). Never evicts our
// own node's entry. Caller holds satelliteMutex. Returns false if nothing
// could be evicted.
template <typename Map> bool evictStalestSatellite(NodeDB &db, Map &map)
{
    auto victim = map.end();
    uint32_t victimTs = UINT32_MAX;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->first == db.getNodeNum())
            continue;
        uint32_t ts = db.hotNodeLastHeard(it->first);
        if (ts < victimTs) {
            victimTs = ts;
            victim = it;
        }
    }
    if (victim == map.end())
        return false;
    map.erase(victim);
    return true;
}

// Keep `map` within MAX_SATELLITE_NODES ahead of inserting `incoming` (the
// tier-1/tier-2 split: only the freshest MAX_SATELLITE_NODES nodes carry
// satellite payloads). Caller holds satelliteMutex.
template <typename Map> void evictSatelliteOverCap(NodeDB &db, Map &map, NodeNum incoming)
{
    if (map.size() < MAX_SATELLITE_NODES || map.count(incoming))
        return;
    evictStalestSatellite(db, map);
}
} // namespace

void NodeDB::resetRadioConfig(bool is_fresh_install)
{
    if (is_fresh_install) {
        radioGeneration++;
    }

    if (channelFile.channels_count != MAX_NUM_CHANNELS) {
        LOG_INFO("Set default channel and radio preferences!");

        channels.initDefaults();
    }

    channels.onConfigChanged();

    // Update the global myRegion
    initRegion();
}

bool NodeDB::factoryReset(bool eraseBleBonds)
{
    LOG_INFO("Perform factory reset!");
    // first, remove the "/prefs" (this removes most prefs)
    spiLock->lock();
    rmDir("/prefs"); // this uses spilock internally...

#ifdef FSCom
    if (FSCom.exists("/static/rangetest.csv") && !FSCom.remove("/static/rangetest.csv")) {
        LOG_ERROR("Could not remove rangetest.csv file");
    }
#endif

    spiLock->unlock();

    // rmDir above nuked the .dat file, but TransmitHistory's in-memory
    // cache auto-flushes every 5 min and would resurrect it.
    if (transmitHistory) {
        transmitHistory->clear();
    }
#if HAS_SCREEN
    messageStore.clearAllMessages();
#endif

#if WARM_NODE_COUNT > 0
    // On nRF52840 the warm tier lives in raw flash outside /prefs, so rmDir
    // didn't touch it; clear it and persist the empty store.
    warmStore.clear();
    warmStore.saveIfDirty();
#endif

    // second, install default state (this will deal with the duplicate mac address issue)
    installDefaultNodeDatabase();
    installDefaultDeviceState();
    installDefaultConfig(!eraseBleBonds); // Also preserve the private key if we're not erasing BLE bonds
    installDefaultModuleConfig();
    installDefaultChannels();
    // third, write everything to disk
    saveToDisk();
    if (eraseBleBonds) {
        LOG_INFO("Erase BLE bonds");
#ifdef ARCH_ESP32
        // This will erase what's in NVS including ssl keys, persistent variables and ble pairing
        nvs_flash_erase();
#endif

#ifdef ARCH_NRF52
        LOG_INFO("Clear bluetooth bonds!");
        bond_print_list(BLE_GAP_ROLE_PERIPH);
        bond_print_list(BLE_GAP_ROLE_CENTRAL);
        Bluefruit.Periph.clearBonds();
        Bluefruit.Central.clearBonds();
#endif
    }
    return true;
}

void NodeDB::installDefaultNodeDatabase()
{
    LOG_DEBUG("Install default NodeDatabase");
    nodeDatabase.version = DEVICESTATE_CUR_VER;
    nodeDatabase.nodes = std::vector<meshtastic_NodeInfoLite>(MAX_NUM_NODES);
    numMeshNodes = 0;
    meshNodes = &nodeDatabase.nodes;
    concurrency::LockGuard satelliteGuard(&satelliteMutex);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    nodePositions.clear();
#endif

#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    nodeTelemetry.clear();
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    nodeEnvironment.clear();
#endif

#if !MESHTASTIC_EXCLUDE_STATUSDB
    nodeStatus.clear();
#endif
}

void NodeDB::installDefaultConfig(bool preserveKey = false)
{
    uint8_t private_key_temp[32];
    bool shouldPreserveKey = preserveKey && config.has_security && config.security.private_key.size > 0;
    if (shouldPreserveKey) {
        memcpy(private_key_temp, config.security.private_key.bytes, config.security.private_key.size);
    }
    LOG_INFO("Install default LocalConfig");
    memset(&config, 0, sizeof(meshtastic_LocalConfig));
    config.version = DEVICESTATE_CUR_VER;
    config.has_device = true;
    config.has_display = true;
    config.has_lora = true;
    config.has_position = true;
    config.has_power = true;
    config.has_network = true;
    config.has_bluetooth = (HAS_BLUETOOTH ? true : false);
    config.has_security = true;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;

    config.lora.sx126x_rx_boosted_gain = true;
    config.lora.tx_enabled =
        true; // FIXME: maybe false in the future, and setting region to enable it. (unset region forces it off)
    config.lora.override_duty_cycle = false;
    config.lora.config_ok_to_mqtt = false;
#if HAS_LORA_FEM
    config.lora.fem_lna_mode = meshtastic_Config_LoRaConfig_FEM_LNA_Mode_ENABLED;
#else
    config.lora.fem_lna_mode = meshtastic_Config_LoRaConfig_FEM_LNA_Mode_NOT_PRESENT;
#endif

#if HAS_TFT // For the devices that support MUI, default to that
    config.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_COLOR;
#endif

#if defined(TFT_WIDTH) && defined(TFT_HEIGHT) && (TFT_WIDTH >= 200 || TFT_HEIGHT >= 200)
    config.display.enable_message_bubbles = true;
#endif

#ifdef USERPREFS_CONFIG_DEVICE_ROLE
    // Restrict ROUTER*, LOST AND FOUND roles for security reasons
    if (IS_ONE_OF(USERPREFS_CONFIG_DEVICE_ROLE, meshtastic_Config_DeviceConfig_Role_ROUTER,
                  meshtastic_Config_DeviceConfig_Role_ROUTER_LATE, meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND)) {
        LOG_WARN("ROUTER roles are restricted, falling back to CLIENT role");
        config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    } else {
        config.device.role = USERPREFS_CONFIG_DEVICE_ROLE;
    }
#else
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT; // Default to client.
#endif

#ifdef USERPREFS_CONFIG_LORA_REGION
    config.lora.region = USERPREFS_CONFIG_LORA_REGION;
#else
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
#endif

#ifdef USERPREFS_LORACONFIG_TX_POWER
    config.lora.tx_power = USERPREFS_LORACONFIG_TX_POWER;
#endif

#ifdef USERPREFS_LORACONFIG_MODEM_PRESET
    config.lora.modem_preset = USERPREFS_LORACONFIG_MODEM_PRESET;
#else
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
#endif

#ifdef USERPREFS_LORACONFIG_USE_PRESET
    config.lora.use_preset = USERPREFS_LORACONFIG_USE_PRESET;
#else
    config.lora.use_preset = true;
#endif

#ifdef USERPREFS_LORACONFIG_BANDWIDTH
    config.lora.bandwidth = USERPREFS_LORACONFIG_BANDWIDTH;
#endif

#ifdef USERPREFS_LORACONFIG_SPREAD_FACTOR
    config.lora.spread_factor = USERPREFS_LORACONFIG_SPREAD_FACTOR;
#endif

#ifdef USERPREFS_LORACONFIG_CODING_RATE
    config.lora.coding_rate = USERPREFS_LORACONFIG_CODING_RATE;
#endif

#ifdef USERPREFS_LORACONFIG_OVERRIDE_FREQUENCY
    config.lora.override_frequency = USERPREFS_LORACONFIG_OVERRIDE_FREQUENCY;
#endif

    config.lora.hop_limit = HOP_RELIABLE;
#ifdef USERPREFS_CONFIG_LORA_IGNORE_MQTT
    config.lora.ignore_mqtt = USERPREFS_CONFIG_LORA_IGNORE_MQTT;
#else
    config.lora.ignore_mqtt = false;
#endif

    // Initialize admin_key_count to zero
    byte numAdminKeys = 0;

#ifdef USERPREFS_USE_ADMIN_KEY_0
    // Check if USERPREFS_ADMIN_KEY_0 is non-empty
    if (sizeof(userprefs_admin_key_0) > 0) {
        memcpy(config.security.admin_key[0].bytes, userprefs_admin_key_0, 32);
        config.security.admin_key[0].size = 32;
        numAdminKeys++;
    }
#endif

#ifdef USERPREFS_USE_ADMIN_KEY_1
    // Check if USERPREFS_ADMIN_KEY_1 is non-empty
    if (sizeof(userprefs_admin_key_1) > 0) {
        memcpy(config.security.admin_key[1].bytes, userprefs_admin_key_1, 32);
        config.security.admin_key[1].size = 32;
        numAdminKeys++;
    }
#endif

#ifdef USERPREFS_USE_ADMIN_KEY_2
    // Check if USERPREFS_ADMIN_KEY_2 is non-empty
    if (sizeof(userprefs_admin_key_2) > 0) {
        memcpy(config.security.admin_key[2].bytes, userprefs_admin_key_2, 32);
        config.security.admin_key[2].size = 32;
        numAdminKeys++;
    }
#endif

    config.security.admin_key_count = numAdminKeys;

    if (shouldPreserveKey) {
        config.security.private_key.size = 32;
        memcpy(config.security.private_key.bytes, private_key_temp, config.security.private_key.size);
        printBytes("Restored key", config.security.private_key.bytes, config.security.private_key.size);
    } else {
        config.security.private_key.size = 0;
    }
    config.security.public_key.size = 0;

#ifdef PIN_GPS_EN
    config.position.gps_en_gpio = PIN_GPS_EN;
#endif

#if defined(USERPREFS_CONFIG_GPS_MODE)
    config.position.gps_mode = USERPREFS_CONFIG_GPS_MODE;
#elif !HAS_GPS || GPS_DEFAULT_NOT_PRESENT
    config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
#elif !defined(GPS_RX_PIN)
    if (config.position.rx_gpio == 0)
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
    else
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
#else
    config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
#endif

#ifdef USERPREFS_CONFIG_SMART_POSITION_ENABLED
    config.position.position_broadcast_smart_enabled = USERPREFS_CONFIG_SMART_POSITION_ENABLED;
#else
    config.position.position_broadcast_smart_enabled = true;
#endif

    config.position.broadcast_smart_minimum_distance = 100;
    config.position.broadcast_smart_minimum_interval_secs = default_broadcast_smart_minimum_interval_secs;
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)
        config.device.node_info_broadcast_secs = default_node_info_broadcast_secs;
    config.security.serial_enabled = true;
    config.security.admin_channel_enabled = false;
    resetRadioConfig(true); // This also triggers NodeInfo/Position requests since we're fresh
    strncpy(config.network.ntp_server, "meshtastic.pool.ntp.org", 32);

#if (defined(T_DECK) || defined(T_WATCH_S3) || defined(UNPHONE) || defined(PICOMPUTER_S3) || defined(SENSECAP_INDICATOR) ||      \
     defined(ELECROW_PANEL) || defined(HELTEC_V4_TFT) || defined(HELTEC_V4_R8_TFT) || defined(RAK_WISMESH_TAP_V2)) &&            \
    HAS_TFT
    // switch BT off by default; use TFT programming mode or hotkey to enable
    config.bluetooth.enabled = false;
#else
    // default to bluetooth capability of platform as default
    config.bluetooth.enabled = true;
#endif

    config.bluetooth.fixed_pin = defaultBLEPin;

#if defined(USE_EINK) || defined(HAS_SPI_TFT) || defined(USE_SPISSD1306)
    bool hasScreen = true;
#ifdef HELTEC_MESH_NODE_T114
    uint32_t st7789_id = get_st7789_id(ST7789_NSS, ST7789_SCK, ST7789_SDA, ST7789_RS, ST7789_RESET);
    if (st7789_id == 0xFFFFFF) {
        hasScreen = false;
    }
#endif // HELTEC_MESH_NODE_T114
#elif ARCH_PORTDUINO
    bool hasScreen = false;
    if (portduino_config.displayPanel)
        hasScreen = true;
    else
        hasScreen = screen_found.port != ScanI2C::I2CPort::NO_I2C;
#elif MESHTASTIC_INCLUDE_NICHE_GRAPHICS // See "src/graphics/niche"
    bool hasScreen = true; // Use random pin for Bluetooth pairing
#else
    bool hasScreen = screen_found.port != ScanI2C::I2CPort::NO_I2C;
#endif

#ifdef USERPREFS_FIXED_BLUETOOTH
    config.bluetooth.fixed_pin = USERPREFS_FIXED_BLUETOOTH;
    config.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
#else
    config.bluetooth.mode = hasScreen ? meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN
                                      : meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
#endif

    // for backward compat, default position flags are ALT+MSL
    config.position.position_flags =
        (meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL |
         meshtastic_Config_PositionConfig_PositionFlags_SPEED | meshtastic_Config_PositionConfig_PositionFlags_HEADING |
         meshtastic_Config_PositionConfig_PositionFlags_DOP | meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW);

// Set default value for 'Mesh via UDP'
#if HAS_UDP_MULTICAST
#ifdef USERPREFS_NETWORK_ENABLED_PROTOCOLS
    config.network.enabled_protocols = USERPREFS_NETWORK_ENABLED_PROTOCOLS;
#else
    config.network.enabled_protocols = 0;
#endif // Network enabled protocols
#endif // UDP Multicast

#ifdef USERPREFS_NETWORK_WIFI_ENABLED
    config.network.wifi_enabled = USERPREFS_NETWORK_WIFI_ENABLED;
#endif

#if USE_ETHERNET_DEFAULT
    config.network.eth_enabled = true;
#endif

#ifdef USERPREFS_NETWORK_WIFI_SSID
    strncpy(config.network.wifi_ssid, USERPREFS_NETWORK_WIFI_SSID, sizeof(config.network.wifi_ssid));
#endif

#ifdef USERPREFS_NETWORK_WIFI_PSK
    strncpy(config.network.wifi_psk, USERPREFS_NETWORK_WIFI_PSK, sizeof(config.network.wifi_psk));
#endif

#if defined(USERPREFS_NETWORK_IPV6_ENABLED)
    config.network.ipv6_enabled = USERPREFS_NETWORK_IPV6_ENABLED;
#else
    config.network.ipv6_enabled = default_network_ipv6_enabled;
#endif

#ifdef DISPLAY_FLIP_SCREEN
    config.display.flip_screen = true;
#endif

#ifdef RAK4630
    config.display.wake_on_tap_or_motion = true;
#endif

#if defined(T_WATCH_S3) || defined(SENSECAP_INDICATOR)
    config.display.screen_on_secs = 30;
    config.display.wake_on_tap_or_motion = true;
#endif

#ifdef COMPASS_ORIENTATION
    config.display.compass_orientation = COMPASS_ORIENTATION;
#endif

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_WIFI
    if (MeshtasticOTA::isUpdated()) {
        MeshtasticOTA::recoverConfig(&config.network);
    }
#endif

#ifdef USERPREFS_CONFIG_DEVICE_ROLE
    // Apply role-specific defaults when role is set via user preferences
    installRoleDefaults(config.device.role);
#endif

    initConfigIntervals();
    variantDefaultConfig();
    variantDefaultModuleConfig();
}

void NodeDB::initConfigIntervals()
{
#ifdef USERPREFS_CONFIG_GPS_UPDATE_INTERVAL
    config.position.gps_update_interval = USERPREFS_CONFIG_GPS_UPDATE_INTERVAL;
#else
    config.position.gps_update_interval = default_gps_update_interval;
#endif

#ifdef USERPREFS_CONFIG_POSITION_BROADCAST_INTERVAL
    config.position.position_broadcast_secs = USERPREFS_CONFIG_POSITION_BROADCAST_INTERVAL;
#else
    config.position.position_broadcast_secs = default_broadcast_interval_secs;
#endif

    config.power.ls_secs = default_ls_secs;
    config.power.min_wake_secs = default_min_wake_secs;
    config.power.sds_secs = default_sds_secs;
    config.power.wait_bluetooth_secs = default_wait_bluetooth_secs;

    config.display.screen_on_secs = default_screen_on_secs;

#if defined(USE_POWERSAVE)
    config.power.is_power_saving = true;
    config.display.screen_on_secs = 30;
    config.power.wait_bluetooth_secs = 30;
#endif
}

// Always-on traffic management defaults. Only booleans are written; every
// numeric field stays 0 and resolves to its default_traffic_mgmt_* macro at
// use (e.g. position dedup precision/interval), so fork-wide tuning changes
// take effect without another migration. Rate limiting and the features that
// exhaust or reshape relayed traffic (exhaust_hop_*, drop_unknown_enabled,
// nodeinfo_direct_response) stay opt-in.
static void installTrafficManagementDefaults(meshtastic_LocalModuleConfig &mc)
{
    mc.has_traffic_management = true;
    mc.traffic_management = meshtastic_ModuleConfig_TrafficManagementConfig_init_zero;
#if HAS_TRAFFIC_MANAGEMENT
    // Position dedup ships enabled at the 11-hour default window on all supported targets.
    // STM32WL is excluded at compile time (HAS_TRAFFIC_MANAGEMENT=0 in mesh-pb-constants.h).
    // Set position_min_interval_secs=0 at runtime to disable dedup.
    mc.traffic_management.position_min_interval_secs = default_traffic_mgmt_position_min_interval_secs;
#endif
}

void NodeDB::installDefaultModuleConfig()
{
    LOG_INFO("Install default ModuleConfig");
    memset(&moduleConfig, 0, sizeof(meshtastic_ModuleConfig));

    moduleConfig.version = DEVICESTATE_CUR_VER;
    moduleConfig.has_mqtt = true;
    moduleConfig.has_range_test = true;
    moduleConfig.has_serial = true;
    moduleConfig.has_store_forward = true;
    moduleConfig.has_telemetry = true;
    moduleConfig.has_external_notification = true;
#if defined(PIN_BUZZER) || defined(PIN_VIBRATION) || defined(LED_NOTIFICATION) || defined(PCA_LED_NOTIFICATION) ||               \
    defined(NEOPIXEL_STATUS_NOTIFICATION_PIN)
    moduleConfig.external_notification.enabled = true;
#endif

#if defined(PIN_BUZZER)
    moduleConfig.external_notification.output_buzzer = PIN_BUZZER;
    moduleConfig.external_notification.use_pwm = true;
    moduleConfig.external_notification.alert_message_buzzer = true;
#endif

#if defined(PIN_VIBRATION)
    moduleConfig.external_notification.output_vibra = PIN_VIBRATION;
    moduleConfig.external_notification.alert_message_vibra = true;
    moduleConfig.external_notification.output_ms = 500;
#endif

#if defined(LED_NOTIFICATION)
    moduleConfig.external_notification.output = LED_NOTIFICATION;
    moduleConfig.external_notification.active = LED_STATE_ON;
    moduleConfig.external_notification.alert_message = true;
    moduleConfig.external_notification.output_ms = 1000;
#endif

#if defined(PIN_VIBRATION)
    moduleConfig.external_notification.nag_timeout = 2;
#elif defined(PIN_BUZZER) || defined(LED_NOTIFICATION) || defined(NEOPIXEL_STATUS_NOTIFICATION_PIN)
    moduleConfig.external_notification.nag_timeout = default_ringtone_nag_secs;
#endif

#ifdef HAS_I2S
    // Don't worry about the other settings for T-Watch, we'll also use the DRV2056 behavior for notifications
    moduleConfig.external_notification.enabled = true;
    moduleConfig.external_notification.use_i2s_as_buzzer = true;
    moduleConfig.external_notification.alert_message_buzzer = true;
#if HAS_TFT
    if (moduleConfig.external_notification.nag_timeout == default_ringtone_nag_secs)
        moduleConfig.external_notification.nag_timeout = 0;
#else
    moduleConfig.external_notification.nag_timeout = default_ringtone_nag_secs;
#endif // HAS_TFT
#endif // HAS_I2S

#ifdef NANO_G2_ULTRA
    moduleConfig.external_notification.enabled = true;
    moduleConfig.external_notification.alert_message = true;
    moduleConfig.external_notification.output_ms = 100;
    moduleConfig.external_notification.active = true;
#endif // NANO_G2_ULTRA

#ifdef T_LORA_PAGER
    moduleConfig.canned_message.updown1_enabled = true;
    moduleConfig.canned_message.inputbroker_pin_a = ROTARY_A;
    moduleConfig.canned_message.inputbroker_pin_b = ROTARY_B;
    moduleConfig.canned_message.inputbroker_pin_press = ROTARY_PRESS;
    moduleConfig.canned_message.inputbroker_event_cw = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(28);
    moduleConfig.canned_message.inputbroker_event_ccw = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(29);
    moduleConfig.canned_message.inputbroker_event_press = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
#endif

    moduleConfig.has_canned_message = true;

#if USERPREFS_MQTT_ENABLED && !MESHTASTIC_EXCLUDE_MQTT
    moduleConfig.mqtt.enabled = true;
#endif

#ifdef USERPREFS_MQTT_ADDRESS
    strncpy(moduleConfig.mqtt.address, USERPREFS_MQTT_ADDRESS, sizeof(moduleConfig.mqtt.address));
#else
    strncpy(moduleConfig.mqtt.address, default_mqtt_address, sizeof(moduleConfig.mqtt.address));
#endif

#ifdef USERPREFS_MQTT_USERNAME
    strncpy(moduleConfig.mqtt.username, USERPREFS_MQTT_USERNAME, sizeof(moduleConfig.mqtt.username));
#else
    strncpy(moduleConfig.mqtt.username, default_mqtt_username, sizeof(moduleConfig.mqtt.username));
#endif

#ifdef USERPREFS_MQTT_PASSWORD
    strncpy(moduleConfig.mqtt.password, USERPREFS_MQTT_PASSWORD, sizeof(moduleConfig.mqtt.password));
#else
    strncpy(moduleConfig.mqtt.password, default_mqtt_password, sizeof(moduleConfig.mqtt.password));
#endif

#ifdef USERPREFS_MQTT_ROOT_TOPIC
    strncpy(moduleConfig.mqtt.root, USERPREFS_MQTT_ROOT_TOPIC, sizeof(moduleConfig.mqtt.root));
#else
    strncpy(moduleConfig.mqtt.root, default_mqtt_root, sizeof(moduleConfig.mqtt.root));
#endif

#ifdef USERPREFS_MQTT_ENCRYPTION_ENABLED
    moduleConfig.mqtt.encryption_enabled = USERPREFS_MQTT_ENCRYPTION_ENABLED;
#else
    moduleConfig.mqtt.encryption_enabled = default_mqtt_encryption_enabled;
#endif

#ifdef USERPREFS_MQTT_TLS_ENABLED
    moduleConfig.mqtt.tls_enabled = USERPREFS_MQTT_TLS_ENABLED;
#else
    moduleConfig.mqtt.tls_enabled = default_mqtt_tls_enabled;
#endif

    moduleConfig.has_neighbor_info = true;
    moduleConfig.neighbor_info.enabled = false;

    installTrafficManagementDefaults(moduleConfig);

    moduleConfig.has_detection_sensor = true;
    moduleConfig.detection_sensor.enabled = false;
    moduleConfig.detection_sensor.detection_trigger_type = meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
    moduleConfig.detection_sensor.minimum_broadcast_secs = 45;

    moduleConfig.has_ambient_lighting = true;
    moduleConfig.ambient_lighting.current = 10;
    // Default to a color based on our node number
    moduleConfig.ambient_lighting.red = (myNodeInfo.my_node_num & 0xFF0000) >> 16;
    moduleConfig.ambient_lighting.green = (myNodeInfo.my_node_num & 0x00FF00) >> 8;
    moduleConfig.ambient_lighting.blue = myNodeInfo.my_node_num & 0x0000FF;

#if !MESHTASTIC_EXCLUDE_BEACON
    moduleConfig.has_mesh_beacon = true;
    // Default flags: listen on, broadcast off, legacy split on.
    moduleConfig.mesh_beacon.flags = meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED |
                                     meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LEGACY_SPLIT;
// Set or clear a single beacon flag bit from a USERPREFS boolean.
#define BEACON_APPLY_FLAG(enabled, flag)                                                                                         \
    do {                                                                                                                         \
        if (enabled)                                                                                                             \
            moduleConfig.mesh_beacon.flags |= (uint32_t)(flag);                                                                  \
        else                                                                                                                     \
            moduleConfig.mesh_beacon.flags &= ~(uint32_t)(flag);                                                                 \
    } while (0)
#ifdef USERPREFS_MESH_BEACON_LISTEN_ENABLED
    BEACON_APPLY_FLAG(USERPREFS_MESH_BEACON_LISTEN_ENABLED, meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED);
#endif
#ifdef USERPREFS_MESH_BEACON_BROADCAST_ENABLED
    BEACON_APPLY_FLAG(USERPREFS_MESH_BEACON_BROADCAST_ENABLED,
                      meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_BROADCAST_ENABLED);
#endif
#ifdef USERPREFS_MESH_BEACON_MESSAGE
    strncpy(moduleConfig.mesh_beacon.broadcast_message, USERPREFS_MESH_BEACON_MESSAGE,
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.broadcast_message[sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1] = '\0';
#endif
#ifdef USERPREFS_MESH_BEACON_INTERVAL_SECS
    moduleConfig.mesh_beacon.broadcast_interval_secs =
        (USERPREFS_MESH_BEACON_INTERVAL_SECS != 0 &&
         USERPREFS_MESH_BEACON_INTERVAL_SECS < default_mesh_beacon_min_broadcast_interval_secs)
            ? default_mesh_beacon_min_broadcast_interval_secs
            : USERPREFS_MESH_BEACON_INTERVAL_SECS;
#endif
#ifdef USERPREFS_MESH_BEACON_OFFER_PRESET
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = USERPREFS_MESH_BEACON_OFFER_PRESET;
#endif
#ifdef USERPREFS_MESH_BEACON_OFFER_REGION
    moduleConfig.mesh_beacon.broadcast_offer_region = USERPREFS_MESH_BEACON_OFFER_REGION;
#endif
#ifdef USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME
    moduleConfig.mesh_beacon.has_broadcast_offer_channel = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_offer_channel.name, USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME,
            sizeof(moduleConfig.mesh_beacon.broadcast_offer_channel.name) - 1);
    moduleConfig.mesh_beacon.broadcast_offer_channel.name[sizeof(moduleConfig.mesh_beacon.broadcast_offer_channel.name) - 1] =
        '\0';
#endif
#ifdef USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK
    moduleConfig.mesh_beacon.has_broadcast_offer_channel = true;
    static const uint8_t beaconOfferPsk[] = USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK;
    static_assert(sizeof(beaconOfferPsk) <= sizeof(moduleConfig.mesh_beacon.broadcast_offer_channel.psk.bytes),
                  "USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK exceeds the 32-byte channel PSK buffer");
    memcpy(moduleConfig.mesh_beacon.broadcast_offer_channel.psk.bytes, beaconOfferPsk, sizeof(beaconOfferPsk));
    moduleConfig.mesh_beacon.broadcast_offer_channel.psk.size = sizeof(beaconOfferPsk);
#endif
#ifdef USERPREFS_MESH_BEACON_ON_PRESET
    moduleConfig.mesh_beacon.has_broadcast_on_preset = true;
    moduleConfig.mesh_beacon.broadcast_on_preset = USERPREFS_MESH_BEACON_ON_PRESET;
#endif
#ifdef USERPREFS_MESH_BEACON_ON_REGION
    moduleConfig.mesh_beacon.broadcast_on_region = USERPREFS_MESH_BEACON_ON_REGION;
#endif
#ifdef USERPREFS_MESH_BEACON_ON_CHANNEL_NAME
    moduleConfig.mesh_beacon.has_broadcast_on_channel = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_on_channel.name, USERPREFS_MESH_BEACON_ON_CHANNEL_NAME,
            sizeof(moduleConfig.mesh_beacon.broadcast_on_channel.name) - 1);
    moduleConfig.mesh_beacon.broadcast_on_channel.name[sizeof(moduleConfig.mesh_beacon.broadcast_on_channel.name) - 1] = '\0';
#endif
#ifdef USERPREFS_MESH_BEACON_ON_CHANNEL_PSK
    moduleConfig.mesh_beacon.has_broadcast_on_channel = true;
    static const uint8_t beaconOnPsk[] = USERPREFS_MESH_BEACON_ON_CHANNEL_PSK;
    static_assert(sizeof(beaconOnPsk) <= sizeof(moduleConfig.mesh_beacon.broadcast_on_channel.psk.bytes),
                  "USERPREFS_MESH_BEACON_ON_CHANNEL_PSK exceeds the 32-byte channel PSK buffer");
    memcpy(moduleConfig.mesh_beacon.broadcast_on_channel.psk.bytes, beaconOnPsk, sizeof(beaconOnPsk));
    moduleConfig.mesh_beacon.broadcast_on_channel.psk.size = sizeof(beaconOnPsk);
#endif
#ifdef USERPREFS_MESH_BEACON_ON_CHANNEL_NUM
    moduleConfig.mesh_beacon.has_broadcast_on_channel = true;
    moduleConfig.mesh_beacon.broadcast_on_channel.channel_num = USERPREFS_MESH_BEACON_ON_CHANNEL_NUM;
#endif
#ifdef USERPREFS_MESH_BEACON_LEGACY_SPLIT
    BEACON_APPLY_FLAG(USERPREFS_MESH_BEACON_LEGACY_SPLIT, meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LEGACY_SPLIT);
#endif
#undef BEACON_APPLY_FLAG
// Per-preset broadcast targets (up to 4). Each TARGET_<N>_* key bumps broadcast_targets_count as needed.
#define BEACON_TARGET_PRESET(N, VAL)                                                                                             \
    do {                                                                                                                         \
        if (moduleConfig.mesh_beacon.broadcast_targets_count < (N) + 1)                                                          \
            moduleConfig.mesh_beacon.broadcast_targets_count = (N) + 1;                                                          \
        moduleConfig.mesh_beacon.broadcast_targets[(N)].has_preset = true;                                                       \
        moduleConfig.mesh_beacon.broadcast_targets[(N)].preset = (VAL);                                                          \
    } while (0)
#define BEACON_TARGET_REGION(N, VAL)                                                                                             \
    do {                                                                                                                         \
        if (moduleConfig.mesh_beacon.broadcast_targets_count < (N) + 1)                                                          \
            moduleConfig.mesh_beacon.broadcast_targets_count = (N) + 1;                                                          \
        moduleConfig.mesh_beacon.broadcast_targets[(N)].region = (VAL);                                                          \
    } while (0)
// Target channel is referenced by index into the device's channel table (0..MAX_NUM_CHANNELS-1).
#define BEACON_TARGET_CH_INDEX(N, VAL)                                                                                           \
    do {                                                                                                                         \
        if (moduleConfig.mesh_beacon.broadcast_targets_count < (N) + 1)                                                          \
            moduleConfig.mesh_beacon.broadcast_targets_count = (N) + 1;                                                          \
        moduleConfig.mesh_beacon.broadcast_targets[(N)].has_channel_index = true;                                                \
        moduleConfig.mesh_beacon.broadcast_targets[(N)].channel_index = (VAL);                                                   \
    } while (0)
#ifdef USERPREFS_MESH_BEACON_TARGET_0_PRESET
    BEACON_TARGET_PRESET(0, USERPREFS_MESH_BEACON_TARGET_0_PRESET);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_0_REGION
    BEACON_TARGET_REGION(0, USERPREFS_MESH_BEACON_TARGET_0_REGION);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_0_CHANNEL_INDEX
    BEACON_TARGET_CH_INDEX(0, USERPREFS_MESH_BEACON_TARGET_0_CHANNEL_INDEX);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_1_PRESET
    BEACON_TARGET_PRESET(1, USERPREFS_MESH_BEACON_TARGET_1_PRESET);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_1_REGION
    BEACON_TARGET_REGION(1, USERPREFS_MESH_BEACON_TARGET_1_REGION);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_1_CHANNEL_INDEX
    BEACON_TARGET_CH_INDEX(1, USERPREFS_MESH_BEACON_TARGET_1_CHANNEL_INDEX);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_2_PRESET
    BEACON_TARGET_PRESET(2, USERPREFS_MESH_BEACON_TARGET_2_PRESET);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_2_REGION
    BEACON_TARGET_REGION(2, USERPREFS_MESH_BEACON_TARGET_2_REGION);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_2_CHANNEL_INDEX
    BEACON_TARGET_CH_INDEX(2, USERPREFS_MESH_BEACON_TARGET_2_CHANNEL_INDEX);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_3_PRESET
    BEACON_TARGET_PRESET(3, USERPREFS_MESH_BEACON_TARGET_3_PRESET);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_3_REGION
    BEACON_TARGET_REGION(3, USERPREFS_MESH_BEACON_TARGET_3_REGION);
#endif
#ifdef USERPREFS_MESH_BEACON_TARGET_3_CHANNEL_INDEX
    BEACON_TARGET_CH_INDEX(3, USERPREFS_MESH_BEACON_TARGET_3_CHANNEL_INDEX);
#endif
#undef BEACON_TARGET_PRESET
#undef BEACON_TARGET_REGION
#undef BEACON_TARGET_CH_INDEX
#endif // !MESHTASTIC_EXCLUDE_BEACON

    initModuleConfigIntervals();
}

void NodeDB::installRoleDefaults(meshtastic_Config_DeviceConfig_Role role)
{
    if (role == meshtastic_Config_DeviceConfig_Role_ROUTER) {
        initConfigIntervals();
        initModuleConfigIntervals();
        moduleConfig.telemetry.device_update_interval = default_telemetry_broadcast_interval_secs;
        config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY;
        owner.has_is_unmessagable = true;
        owner.is_unmessagable = true;
    } else if (role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
        moduleConfig.telemetry.device_update_interval = ONE_DAY;
        owner.has_is_unmessagable = true;
        owner.is_unmessagable = true;
    } else if (role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
        owner.has_is_unmessagable = true;
        owner.is_unmessagable = true;
        moduleConfig.telemetry.device_update_interval = default_telemetry_broadcast_interval_secs;
        moduleConfig.telemetry.environment_measurement_enabled = true;
        moduleConfig.telemetry.environment_update_interval = 300;
    } else if (role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND) {
        config.position.position_broadcast_smart_enabled = false;
        config.position.position_broadcast_secs = 300; // Every 5 minutes
    } else if (role == meshtastic_Config_DeviceConfig_Role_TAK) {
        config.device.node_info_broadcast_secs = ONE_DAY;
        config.position.position_broadcast_smart_enabled = false;
        config.position.position_broadcast_secs = ONE_DAY;
        // Remove Altitude MSL from flags since CoTs use HAE (height above ellipsoid)
        config.position.position_flags =
            (meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
             meshtastic_Config_PositionConfig_PositionFlags_HEADING | meshtastic_Config_PositionConfig_PositionFlags_DOP);
        moduleConfig.telemetry.device_update_interval = ONE_DAY;
    } else if (role == meshtastic_Config_DeviceConfig_Role_TRACKER) {
        owner.has_is_unmessagable = true;
        owner.is_unmessagable = true;
        moduleConfig.telemetry.device_update_interval = default_telemetry_broadcast_interval_secs;
    } else if (role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) {
        owner.has_is_unmessagable = true;
        owner.is_unmessagable = true;
        config.device.node_info_broadcast_secs = ONE_DAY;
        config.position.position_broadcast_smart_enabled = true;
        config.position.position_broadcast_secs = 3 * 60; // Every 3 minutes
        config.position.broadcast_smart_minimum_distance = 20;
        config.position.broadcast_smart_minimum_interval_secs = 15;
        // Remove Altitude MSL from flags since CoTs use HAE (height above ellipsoid)
        config.position.position_flags =
            (meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
             meshtastic_Config_PositionConfig_PositionFlags_HEADING | meshtastic_Config_PositionConfig_PositionFlags_DOP);
        moduleConfig.telemetry.device_update_interval = ONE_DAY;
    } else if (role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY;
        config.device.node_info_broadcast_secs = MAX_INTERVAL;
        config.position.position_broadcast_smart_enabled = false;
        config.position.position_broadcast_secs = MAX_INTERVAL;
        moduleConfig.neighbor_info.update_interval = MAX_INTERVAL;
        moduleConfig.telemetry.device_update_interval = MAX_INTERVAL;
        moduleConfig.telemetry.environment_update_interval = MAX_INTERVAL;
        moduleConfig.telemetry.air_quality_interval = MAX_INTERVAL;
        moduleConfig.telemetry.health_update_interval = MAX_INTERVAL;
    }
}

void NodeDB::initModuleConfigIntervals()
{
    // Zero out telemetry intervals so that they coalesce to defaults in Default.h
#ifdef USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL
    moduleConfig.telemetry.device_update_interval = USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL;
#else
    moduleConfig.telemetry.device_update_interval = MAX_INTERVAL;
#endif

#ifdef USERPREFS_CONFIG_ENV_TELEM_UPDATE_INTERVAL
    moduleConfig.telemetry.environment_update_interval = USERPREFS_CONFIG_ENV_TELEM_UPDATE_INTERVAL;
#else
    moduleConfig.telemetry.environment_update_interval = 0;
#endif

#ifdef USERPREFS_CONFIG_ENVIRONMENT_MEASUREMENT_ENABLED
    moduleConfig.telemetry.environment_measurement_enabled = USERPREFS_CONFIG_ENVIRONMENT_MEASUREMENT_ENABLED;
#endif
    moduleConfig.telemetry.air_quality_interval = 0;
    moduleConfig.telemetry.power_update_interval = 0;
    moduleConfig.telemetry.health_update_interval = 0;
    moduleConfig.neighbor_info.update_interval = 0;
    moduleConfig.paxcounter.paxcounter_update_interval = 0;
}

void NodeDB::installDefaultChannels()
{
    LOG_INFO("Install default ChannelFile");
    memset(&channelFile, 0, sizeof(meshtastic_ChannelFile));
    channelFile.version = DEVICESTATE_CUR_VER;
}

void NodeDB::resetNodes(bool keepFavorites)
{
    if (!config.position.fixed_position)
        clearLocalPosition();
    NodeNum ourNum = getNodeNum();
    numMeshNodes = 1;
    if (keepFavorites) {
        LOG_INFO("Clearing node database - preserving favorites");
        for (size_t i = 0; i < meshNodes->size(); i++) {
            meshtastic_NodeInfoLite &node = meshNodes->at(i);
            if (i > 0 && !nodeInfoLiteIsFavorite(&node)) {
                eraseNodeSatellites(node.num);
                node = meshtastic_NodeInfoLite();
            } else {
                numMeshNodes += 1;
            }
        };
    } else {
        LOG_INFO("Clearing node database - removing favorites");
        for (size_t i = 1; i < meshNodes->size(); i++) {
            const NodeNum gone = meshNodes->at(i).num;
            if (gone)
                eraseNodeSatellites(gone);
        }
        std::fill(nodeDatabase.nodes.begin() + 1, nodeDatabase.nodes.end(), meshtastic_NodeInfoLite());
    }
    (void)ourNum;
#if WARM_NODE_COUNT > 0
    warmStore.clear(); // warm entries are never favorites; a DB reset clears them too
#endif

    devicestate.has_rx_waypoint = false;
    saveNodeDatabaseToDisk();
    saveDeviceStateToDisk();
    if (neighborInfoModule && moduleConfig.neighbor_info.enabled)
        neighborInfoModule->resetNeighbors();
}

void NodeDB::removeNodeByNum(NodeNum nodeNum)
{
    int newPos = 0, removed = 0;
    for (int i = 0; i < numMeshNodes; i++) {
        if (meshNodes->at(i).num != nodeNum)
            meshNodes->at(newPos++) = meshNodes->at(i);
        else
            removed++;
    }
    numMeshNodes -= removed;
    std::fill(nodeDatabase.nodes.begin() + numMeshNodes, nodeDatabase.nodes.begin() + numMeshNodes + 1,
              meshtastic_NodeInfoLite());
    if (removed)
        eraseNodeSatellites(nodeNum);
#if WARM_NODE_COUNT > 0
    // Explicit user removal: don't let the warm tier resurrect the node
    warmStore.remove(nodeNum);
#endif

    LOG_DEBUG("NodeDB::removeNodeByNum purged %d entries. Save changes", removed);
    saveNodeDatabaseToDisk();
}

void NodeDB::clearLocalPosition()
{
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    concurrency::LockGuard guard(&satelliteMutex);
    nodePositions.erase(getNodeNum());
#endif
    setLocalPosition(meshtastic_Position_init_default);
    localPositionUpdatedSinceBoot = false;
}

bool NodeDB::copyNodePosition(NodeNum n, meshtastic_PositionLite &out) const
{
#if MESHTASTIC_EXCLUDE_POSITIONDB
    (void)n;
    (void)out;
    return false;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return copySatelliteEntry(nodePositions, n, out);
#endif
}

bool NodeDB::copyNodeTelemetry(NodeNum n, meshtastic_DeviceMetrics &out) const
{
#if MESHTASTIC_EXCLUDE_TELEMETRYDB
    (void)n;
    (void)out;
    return false;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return copySatelliteEntry(nodeTelemetry, n, out);
#endif
}

bool NodeDB::copyNodeEnvironment(NodeNum n, meshtastic_EnvironmentMetrics &out) const
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    (void)n;
    (void)out;
    return false;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return copySatelliteEntry(nodeEnvironment, n, out);
#endif
}

bool NodeDB::copyNodeStatus(NodeNum n, meshtastic_StatusMessage &out) const
{
#if MESHTASTIC_EXCLUDE_STATUSDB
    (void)n;
    (void)out;
    return false;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return copySatelliteEntry(nodeStatus, n, out);
#endif
}

std::vector<NodeNum> NodeDB::snapshotPositionNodeNums(NodeNum exclude) const
{
#if MESHTASTIC_EXCLUDE_POSITIONDB
    (void)exclude;
    return {};
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return snapshotSatelliteNodeNums(nodePositions, exclude);
#endif
}

std::vector<NodeNum> NodeDB::snapshotTelemetryNodeNums(NodeNum exclude) const
{
#if MESHTASTIC_EXCLUDE_TELEMETRYDB
    (void)exclude;
    return {};
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return snapshotSatelliteNodeNums(nodeTelemetry, exclude);
#endif
}

std::vector<NodeNum> NodeDB::snapshotEnvironmentNodeNums(NodeNum exclude) const
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    (void)exclude;
    return {};
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return snapshotSatelliteNodeNums(nodeEnvironment, exclude);
#endif
}

std::vector<NodeNum> NodeDB::snapshotStatusNodeNums(NodeNum exclude) const
{
#if MESHTASTIC_EXCLUDE_STATUSDB
    (void)exclude;
    return {};
#else
    concurrency::LockGuard guard(&satelliteMutex);
    return snapshotSatelliteNodeNums(nodeStatus, exclude);
#endif
}

void NodeDB::setNodeStatus(NodeNum n, const meshtastic_StatusMessage &status)
{
#if MESHTASTIC_EXCLUDE_STATUSDB
    (void)n;
    (void)status;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    evictSatelliteOverCap(*this, nodeStatus, n);
    nodeStatus[n] = status;
#endif
}

void NodeDB::touchNodePositionTime(NodeNum n, uint32_t time)
{
#if MESHTASTIC_EXCLUDE_POSITIONDB
    (void)n;
    (void)time;
#else
    concurrency::LockGuard guard(&satelliteMutex);
    evictSatelliteOverCap(*this, nodePositions, n);
    nodePositions[n].time = time;
#endif
}

void NodeDB::eraseNodeSatellites(NodeNum n)
{
    concurrency::LockGuard guard(&satelliteMutex);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    nodePositions.erase(n);
#endif

#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    nodeTelemetry.erase(n);
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    nodeEnvironment.erase(n);
#endif

#if !MESHTASTIC_EXCLUDE_STATUSDB
    nodeStatus.erase(n);
#endif
}

bool NodeDB::enforceSatelliteCaps()
{
    concurrency::LockGuard guard(&satelliteMutex);
    bool trimmedAny = false;
    auto trim = [this, &trimmedAny](auto &map, const char *name) {
        const size_t before = map.size();
        while (map.size() > MAX_SATELLITE_NODES) {
            if (!evictStalestSatellite(*this, map))
                break;
        }
        if (map.size() != before) {
            trimmedAny = true;
            LOG_MIGRATION("Trimmed %s satellites %u -> %u (cap %d)", name, (unsigned)before, (unsigned)map.size(),
                          MAX_SATELLITE_NODES);
        }
    };
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    trim(nodePositions, "position");
#endif

#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    trim(nodeTelemetry, "telemetry");
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    trim(nodeEnvironment, "environment");
#endif

#if !MESHTASTIC_EXCLUDE_STATUSDB
    trim(nodeStatus, "status");
#endif

    (void)trim; // all four maps may be compiled out

    // Approximate satellite heap usage: each std::map entry is one rb-tree node,
    // value_type plus ~44 B of node overhead (parent/left/right pointers, color,
    // allocator rounding on 32-bit targets - an estimate, not exact bookkeeping).
    size_t satBytes = 0;
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    satBytes += nodePositions.size() * (sizeof(decltype(nodePositions)::value_type) + 44);
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    satBytes += nodeTelemetry.size() * (sizeof(decltype(nodeTelemetry)::value_type) + 44);
#endif
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    satBytes += nodeEnvironment.size() * (sizeof(decltype(nodeEnvironment)::value_type) + 44);
#endif
#if !MESHTASTIC_EXCLUDE_STATUSDB
    satBytes += nodeStatus.size() * (sizeof(decltype(nodeStatus)::value_type) + 44);
#endif
    memaudit::set("satmaps", satBytes);

    return trimmedAny;
}

#if WARM_NODE_COUNT > 0
// Classify an evicted node's hop-protected category for the warm tier. Favorite/ignored/
// verified are local flags (rarely reach warm - they're eviction-protected - but classify
// them if they do); otherwise tracker/sensor/tak_tracker are role-protected.
static uint8_t warmProtectedCategory(const meshtastic_NodeInfoLite &n)
{
    if (n.bitfield & (NODEINFO_BITFIELD_IS_FAVORITE_MASK | NODEINFO_BITFIELD_IS_IGNORED_MASK |
                      NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK))
        return static_cast<uint8_t>(WarmProtected::Flag);
    if (IS_ONE_OF(n.role, meshtastic_Config_DeviceConfig_Role_TRACKER, meshtastic_Config_DeviceConfig_Role_SENSOR,
                  meshtastic_Config_DeviceConfig_Role_TAK_TRACKER))
        return static_cast<uint8_t>(WarmProtected::Role);
    return static_cast<uint8_t>(WarmProtected::None);
}

// The warm tier packs the device role into a 4-bit field (WARM_ROLE_MASK). Fail the build
// loudly if a new role outgrows it, rather than silently truncating role on eviction.
static_assert(_meshtastic_Config_DeviceConfig_Role_MAX <= WARM_ROLE_MASK,
              "device role no longer fits the 4-bit warm metadata field");
#endif // WARM_NODE_COUNT > 0

void NodeDB::cleanupMeshDB()
{
    int newPos = 0, removed = 0;
    for (int i = 0; i < numMeshNodes; i++) {
        meshtastic_NodeInfoLite &n = meshNodes->at(i);
        // Keep ignored (blocked) nodes even without user info: a block set by
        // bare node ID has no NodeInfo and would otherwise be purged here,
        // silently dropping the block.
        if (nodeInfoLiteHasUser(&n) || nodeInfoLiteIsIgnored(&n)) {
            if (n.public_key.size > 0) {
                if (memfll(n.public_key.bytes, 0, n.public_key.size)) {
                    n.public_key.size = 0;
                }
            }
            if (newPos != i)
                meshNodes->at(newPos++) = n;
            else
                newPos++;
        } else {
            // No user info - drop this node and its satellites
            const NodeNum gone = n.num;
            if (gone) {
#if WARM_NODE_COUNT > 0
                // Keep any key we learned (e.g. via a DM before the NodeInfo
                // exchange completed) rather than losing it with the purge.
                if (n.public_key.size == 32)
                    warmStore.absorb(gone, n.last_heard, n.public_key.bytes, n.role, warmProtectedCategory(n));
#endif

                eraseNodeSatellites(gone);
            }
            removed++;
        }
    }
    numMeshNodes -= removed;
    std::fill(nodeDatabase.nodes.begin() + numMeshNodes, nodeDatabase.nodes.begin() + numMeshNodes + removed,
              meshtastic_NodeInfoLite());
    LOG_DEBUG("cleanupMeshDB purged %d entries", removed);
}

void NodeDB::installDefaultDeviceState()
{
    LOG_INFO("Install default DeviceState");
    // memset(&devicestate, 0, sizeof(meshtastic_DeviceState));

    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_owner = true;
    devicestate.version = DEVICESTATE_CUR_VER;
    devicestate.receive_queue_count = 0; // Not yet implemented FIXME
    devicestate.has_rx_waypoint = false;

    generatePacketId(); // FIXME - ugly way to init current_packet_id;

    // Set default owner name
    pickNewNodeNum(); // based on macaddr now
#ifdef USERPREFS_CONFIG_OWNER_LONG_NAME
    snprintf(owner.long_name, sizeof(owner.long_name), (const char *)USERPREFS_CONFIG_OWNER_LONG_NAME);
#else
    snprintf(owner.long_name, sizeof(owner.long_name), "Meshtastic %04x", getNodeNum() & 0x0ffff);
#endif

    clampLongName(owner.long_name); // vendor userprefs may exceed the local cap

#ifdef USERPREFS_CONFIG_OWNER_SHORT_NAME
    snprintf(owner.short_name, sizeof(owner.short_name), (const char *)USERPREFS_CONFIG_OWNER_SHORT_NAME);
#else
    snprintf(owner.short_name, sizeof(owner.short_name), "%04x", getNodeNum() & 0x0ffff);
#endif

    snprintf(owner.id, sizeof(owner.id), "!%08x", getNodeNum()); // Default node ID now based on nodenum
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));
    owner.has_is_unmessagable = true;
    owner.is_unmessagable = false;
}

// We reserve a few nodenums for future use
#define NUM_RESERVED 4

/**
 * get our starting (provisional) nodenum from flash.
 */
void NodeDB::pickNewNodeNum()
{
    NodeNum nodeNum = myNodeInfo.my_node_num;
    getMacAddr(ourMacAddr); // Make sure ourMacAddr is set
    if (nodeNum == 0) {
        // Pick an initial nodenum based on the macaddr
        nodeNum = (ourMacAddr[2] << 24) | (ourMacAddr[3] << 16) | (ourMacAddr[4] << 8) | ourMacAddr[5];
    }

    // Identity check via public key (or "empty slot?" when no keys yet);
    // macaddr no longer lives on the slim header.
    auto isOurOwnEntry = [&](const meshtastic_NodeInfoLite *n) -> bool {
        if (!n)
            return false;
        if (owner.public_key.size == 32 && n->public_key.size == 32)
            return memcmp(n->public_key.bytes, owner.public_key.bytes, 32) == 0;
        return !nodeInfoLiteHasUser(n);
    };

    meshtastic_NodeInfoLite *found;
    while (((found = getMeshNode(nodeNum)) && !isOurOwnEntry(found)) ||
           (nodeNum == NODENUM_BROADCAST || nodeNum < NUM_RESERVED)) {
        NodeNum candidate = random(NUM_RESERVED, LONG_MAX); // try a new random choice
        if (found)
            LOG_WARN("NOTE! Our desired nodenum 0x%08x is invalid or in use, picking 0x%08x", nodeNum, candidate);
        nodeNum = candidate;
    }
    LOG_DEBUG("Use nodenum 0x%08x ", nodeNum);

    myNodeInfo.my_node_num = nodeNum;
}

/** Load a protobuf from a file, return LoadFileResult */
LoadFileResult NodeDB::loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields,
                                 void *dest_struct)
{
    LoadFileResult state = LoadFileResult::OTHER_FAILURE;

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // check if the file is encrypted and decrypt before protobuf decode
    if (EncryptedStorage::isEncrypted(filename)) {
        // ZeroizingArrayPtr wipes the decrypted plaintext (which contains config
        // secrets - channel PSKs, security private_key, etc.) before delete[],
        // so it isn't recoverable from the heap after this function returns.
        auto decBuf = meshtastic_security::make_zeroizing_array(protoSize);
        if (!decBuf) {
            LOG_ERROR("OOM decrypting %s", filename);
            return LoadFileResult::OTHER_FAILURE;
        }
        size_t decLen = 0;
        if (EncryptedStorage::readAndDecrypt(filename, decBuf.get(), protoSize, decLen)) {
            LOG_INFO("Load encrypted %s", filename);
            pb_istream_t stream = pb_istream_from_buffer(decBuf.get(), decLen);
            if (fields != &meshtastic_NodeDatabase_msg)
                memset(dest_struct, 0, objSize);
            if (!pb_decode(&stream, fields, dest_struct)) {
                LOG_ERROR("Error: can't decode protobuf %s", PB_GET_ERROR(&stream));
                state = LoadFileResult::DECODE_FAILED;
                storageCorruptThisLoad = true;
            } else {
                LOG_INFO("Loaded encrypted %s successfully", filename);
                state = LoadFileResult::LOAD_SUCCESS;
            }
        } else {
            LOG_ERROR("Decrypt failed for %s, treating as corrupt", filename);
            state = LoadFileResult::DECODE_FAILED;
            storageCorruptThisLoad = true;
        }
        return state;
    }
#endif

#ifdef FSCom
    concurrency::LockGuard g(spiLock);

    auto f = FSCom.open(filename, FILE_O_READ);

    if (f) {
        LOG_INFO("Load %s", filename);
        pb_istream_t stream = {&readcb, &f, protoSize};
        if (fields != &meshtastic_NodeDatabase_msg &&
            fields != &meshtastic_NodeDatabase_Legacy_msg) // both NodeDatabase descriptors contain std::vector members
            memset(dest_struct, 0, objSize);
        if (!pb_decode(&stream, fields, dest_struct)) {
            LOG_ERROR("Error: can't decode protobuf %s", PB_GET_ERROR(&stream));
            state = LoadFileResult::DECODE_FAILED;
        } else {
            LOG_INFO("Loaded %s successfully", filename);
            state = LoadFileResult::LOAD_SUCCESS;
        }
        f.close();
    } else {
        LOG_ERROR("Could not open / read %s", filename);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
    state = LoadFileResult::NO_FILESYSTEM;
#endif
    return state;
}

#if WARM_NODE_COUNT > 0
void NodeDB::demoteOldestHotNodesToWarm()
{
    const int keep = MAX_NUM_NODES;
    if (numMeshNodes <= keep)
        return;

    // Protected nodes (favorite/ignored/verified) outrank recency and are demoted
    // only when the store is full of them; within a class, most-recently-heard
    // wins. Index 0 is self and stays put (sort from +1), as in runtime eviction.
    std::sort(meshNodes->begin() + 1, meshNodes->begin() + numMeshNodes,
              [](const meshtastic_NodeInfoLite &a, const meshtastic_NodeInfoLite &b) {
                  const bool ka = nodeInfoLiteIsProtected(&a);
                  const bool kb = nodeInfoLiteIsProtected(&b);
                  if (ka != kb)
                      return ka;
                  return a.last_heard > b.last_heard;
              });

    int demoted = 0;
    for (int i = keep; i < numMeshNodes; i++) {
        const meshtastic_NodeInfoLite &n = (*meshNodes)[i];
        if (n.num == 0)
            continue;
        // Keep the public key if we have one (40 B warm record); keyless nodes
        // still get a placeholder so re-admission restores last_heard.
        warmStore.absorb(n.num, n.last_heard, n.public_key.size > 0 ? n.public_key.bytes : nullptr, n.role,
                         warmProtectedCategory(n));
        // Demotion drops the node from the header table, so drop its satellites
        // too (the eviction chokepoint) - they'd otherwise orphan until the next
        // enforceSatelliteCaps pass.
        eraseNodeSatellites(n.num);
        demoted++;
    }
    numMeshNodes = keep; // the resize() in loadFromDisk reclaims the demoted tail
    LOG_MIGRATION("NodeDB migration: demoted %d node(s) over %d into the warm tier (keepers preferred)", demoted, keep);
}
#endif

void NodeDB::nodeDBSelfCare()
{
    if (!meshNodes)
        return;

    const NodeNum self = getNodeNum();
    const bool nodesOverCap = numMeshNodes > MAX_NUM_NODES;

    // Confirm self is present and its key matches what we just (re)derived. A
    // non-empty DB that doesn't contain us means a foreign/over-cap or corrupt
    // nodes.proto was loaded; an empty DB is just a fresh device (no warning).
    meshtastic_NodeInfoLite *selfNode = getMeshNode(self);
    if (!selfNode && numMeshNodes > 0) {
        LOG_WARN("NodeDB self-care: self 0x%08x absent from DB, re-adding", (unsigned)self);
    } else if (selfNode && owner.public_key.size == 32 && selfNode->public_key.size == 32 &&
               memcmp(selfNode->public_key.bytes, owner.public_key.bytes, 32) != 0) {
        LOG_WARN("NodeDB self-care: self 0x%08x key mismatch, refreshing", (unsigned)self);
    }

    // Maintenance that must never touch self. Pin self to index 0 first so
    // the positional demote/eviction scans (which skip index 0) provably exclude
    // us, wherever the loaded file happened to place our row.
    if (selfNode && numMeshNodes > 0 && selfNode != &meshNodes->at(0)) {
        std::swap(meshNodes->at(0), *selfNode);
    }

#if WARM_NODE_COUNT > 0
    if (nodesOverCap)
        demoteOldestHotNodesToWarm(); // demotes oldest NON-self overflow; index 0 (us) left in place
#endif

    if (numMeshNodes > MAX_NUM_NODES) {
        LOG_WARN("NodeDB self-care: %d over cap %d, truncating", numMeshNodes, MAX_NUM_NODES);
        numMeshNodes = MAX_NUM_NODES;
    }
    // Normalise the backing store to the hot cap so getOrCreateMeshNode always
    // has spare slots to append into (it indexes meshNodes->at(numMeshNodes++)).
    meshNodes->resize(MAX_NUM_NODES);
    memaudit::set("nodedb", MAX_NUM_NODES * sizeof(meshtastic_NodeInfoLite));

    const bool satsTrimmed = enforceSatelliteCaps();

    // Ensure self exists, sits at index 0, and carries current owner info - after
    // any demotion has freed a slot. Covers the foreign/fixture case where the
    // loaded file did not contain us at all.
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(self);
    if (info) {
        TypeConversions::CopyUserToNodeInfoLite(info, owner);
        if (info != &meshNodes->at(0))
            std::swap(meshNodes->at(0), *info);
    }

    // One-shot rewrite: only when we healed something, and never while storage
    // is locked - a locked boot loads placeholder defaults that must not be written
    // over the encrypted store; reloadFromDisk() re-runs self-care once unlocked.
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    const bool storageLocked = EncryptedStorage::isLockdownActive() && !EncryptedStorage::isUnlocked();
#else
    const bool storageLocked = false;
#endif

    if ((nodesOverCap || satsTrimmed) && !storageLocked) {
        LOG_MIGRATION("NodeDB self-care: healed store (nodes-over-cap:%s sats-trimmed:%s); rewriting nodes.proto once",
                      nodesOverCap ? "yes" : "no", satsTrimmed ? "yes" : "no");
        saveNodeDatabaseToDisk();
#if WARM_NODE_COUNT > 0
        warmStore.saveIfDirty();
#endif
    }
}

void NodeDB::loadFromDisk()
{
    // Mark the current device state as completely unusable, so that if we fail reading the entire file from
    // disk we will still factoryReset to restore things.
    devicestate.version = 0;
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // Reset the per-load decrypt-failure tracker. Set by loadProto on any
    // encrypted file that fails to decrypt or proto-decode; consumed by
    // reloadFromDisk to surface storage corruption to the operator instead
    // of silently falling back to defaults.
    storageCorruptThisLoad = false;
#endif

    migrationSavePending = false;
    configDecodeFailed = false;

    meshtastic_Config_SecurityConfig backupSecurity = meshtastic_Config_SecurityConfig_init_zero;

#ifdef ARCH_ESP32
    spiLock->lock();
    // If the legacy deviceState exists, start over with a factory reset
    if (FSCom.exists("/static/static"))
        rmDir("/static/static"); // Remove bad static web files bundle from initial 2.5.13 release
    spiLock->unlock();
#endif

#ifdef FSCom
#if defined(FACTORY_INSTALL) && !defined(ARCH_PORTDUINO)
    spiLock->lock();
    if (!FSCom.exists("/prefs/" xstr(BUILD_EPOCH))) {
        LOG_WARN("Factory Install Reset!");
        rmDir("/prefs");
        FSCom.mkdir("/prefs");
        File f2 = FSCom.open("/prefs/" xstr(BUILD_EPOCH), FILE_O_WRITE);
        if (f2) {
            f2.flush();
            f2.close();
        }
    }
    spiLock->unlock();
#endif // FACTORY_INSTALL, not PORTDUINO
    spiLock->lock();
    if (FSCom.exists(legacyPrefFileName)) {
        spiLock->unlock();
        LOG_WARN("Legacy prefs version found, factory resetting");
        if (loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(meshtastic_LocalConfig), &meshtastic_LocalConfig_msg,
                      &config) == LoadFileResult::LOAD_SUCCESS &&
            config.has_security && config.security.private_key.size > 0) {
            LOG_DEBUG("Saving backup of security config and keys");
            backupSecurity = config.security;
        }
        spiLock->lock();
        rmDir("/prefs");
        spiLock->unlock();
    } else {
        spiLock->unlock();
    }

#endif // FSCom

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // Only take the locked-boot defaults path when lockdown is ACTIVE (the
    // device is provisioned) AND storage is still locked. A lockdown-capable
    // build that has never been provisioned - or that was disabled - falls
    // through to the normal plaintext load below and behaves like stock.
    if (EncryptedStorage::isLockdownActive() && !EncryptedStorage::isUnlocked()) {
        // Encrypted storage is locked. Install defaults and wait for the
        // passphrase over BLE/serial; PhoneAPI::handleLockdownAuthInline
        // calls reloadFromDisk() once the storage is unlocked.
        LOG_WARN("NodeDB: Encrypted storage locked, using default config until unlocked");
        installDefaultNodeDatabase();
        installDefaultDeviceState();
        installDefaultConfig();
        installDefaultModuleConfig();
        installDefaultChannels();

        // Hold the radio silent until the operator unlocks. installDefaultConfig
        // would otherwise honour USERPREFS_CONFIG_LORA_REGION (the common shape
        // for managed deployments) and the LongFast default channel synthesised
        // by installDefaultChannels, so the device would beacon nodeinfo /
        // telemetry on the public default PSK before any unlock - and process
        // incoming default-channel packets the same way. Forcing region=UNSET
        // gates both TX and RX in RadioLibInterface (see the region==UNSET
        // checks in startSend and readData); tx_enabled=false is belt-and-
        // suspenders for any code path that does not consult region directly.
        // reloadFromDisk() restores the persisted lora config when the
        // operator unlocks.
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        config.lora.tx_enabled = false;
        return;
    }
#endif

    // Arm the direct-into-map decode so satellite entries skip the temp vectors.
    {
        concurrency::LockGuard guard(&satelliteMutex);
        armNodeDatabaseDecodeTargets();
    }
    struct Disarm {
        NodeDB &self;
        ~Disarm() { self.disarmNodeDatabaseDecodeTargets(); }
    } disarm{*this};

    // Avoid push_back's power-of-2 capacity growth wasting RAM at small N.
    nodeDatabase.nodes.reserve(MAX_NUM_NODES);

    auto state = loadProto(nodeDatabaseFileName, getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                           &meshtastic_NodeDatabase_msg, &nodeDatabase);
    if (nodeDatabase.version < DEVICESTATE_MIN_VER) {
        LOG_WARN("NodeDatabase %d is old, discard", nodeDatabase.version);
        installDefaultNodeDatabase();
    } else if (nodeDatabase.version < DEVICESTATE_CUR_VER) {
        if (migrateLegacyNodeDatabase())
            migrationSavePending = true;
        else
            installDefaultNodeDatabase();
    } else {
        meshNodes = &nodeDatabase.nodes;
        numMeshNodes = nodeDatabase.nodes.size();
        // Counts computed outside LOG_INFO() so cppcheck doesn't choke on #if in macro args.
        const unsigned posCount =
#if !MESHTASTIC_EXCLUDE_POSITIONDB
            (unsigned)nodePositions.size();
#else
            0u;
#endif

        const unsigned telCount =
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
            (unsigned)nodeTelemetry.size();
#else
            0u;
#endif

        const unsigned envCount =
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
            (unsigned)nodeEnvironment.size();
#else
            0u;
#endif

        const unsigned statusCount =
#if !MESHTASTIC_EXCLUDE_STATUSDB
            (unsigned)nodeStatus.size();
#else
            0u;
#endif

        LOG_INFO("Loaded saved nodedatabase v%d: %d nodes, %u pos, %u tel, %u env, %u status", nodeDatabase.version,
                 nodeDatabase.nodes.size(), posCount, telCount, envCount, statusCount);
    }

    // Left UNTRIMMED on purpose: trim/demote/satellite-cap/self-pin/rewrite all
    // run in nodeDBSelfCare() once getNodeNum() is valid (still 0 here on a cold
    // boot, so we could only assume index 0 == self - the very bug being fixed).
#if WARM_NODE_COUNT > 0
    // Load the warm tier so its on-disk snapshot is available before the node DB
    // is exercised (and before nodeDBSelfCare() demotes any overflow into it).
    warmStore.load();
#endif

    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    state = loadProto(deviceStateFileName, meshtastic_DeviceState_size, sizeof(meshtastic_DeviceState),
                      &meshtastic_DeviceState_msg, &devicestate);

    // See https://github.com/meshtastic/firmware/issues/4184#issuecomment-2269390786
    // It is very important to try and use the saved prefs even if we fail to read meshtastic_DeviceState.  Because most of our
    // critical config may still be valid (in the other files - loaded next).
    // Also, if we did fail on reading we probably failed on the enormous (and non critical) nodeDB.  So DO NOT install default
    // device state.
    // if (state != LoadFileResult::LOAD_SUCCESS) {
    //    installDefaultDeviceState(); // Our in RAM copy might now be corrupt
    //} else {
    if ((state != LoadFileResult::LOAD_SUCCESS) || (devicestate.version < DEVICESTATE_MIN_VER)) {
        LOG_WARN("Devicestate %d is old or invalid, discard", devicestate.version);
        installDefaultDeviceState();

        // Attempt recovery of owner fields from our own NodeDB entry if available.
        meshtastic_NodeInfoLite *us = getMeshNode(getNodeNum());
        if (nodeInfoLiteHasUser(us)) {
            LOG_WARN("Restoring owner fields (long_name/short_name/is_licensed/is_unmessagable) from NodeDB for our node 0x%08x",
                     us->num);
            // owner.long_name (40) is wider than the lite source (25); bound by the source
            memcpy(owner.long_name, us->long_name, sizeof(us->long_name));
            owner.long_name[sizeof(us->long_name) - 1] = '\0';
            memcpy(owner.short_name, us->short_name, sizeof(owner.short_name));
            owner.short_name[sizeof(owner.short_name) - 1] = '\0';
            owner.is_licensed = nodeInfoLiteIsLicensed(us);
            owner.has_is_unmessagable = nodeInfoLiteHasIsUnmessagable(us);
            owner.is_unmessagable = nodeInfoLiteIsUnmessagable(us);

            // Save the recovered owner to device state on disk
            saveToDisk(SEGMENT_DEVICESTATE);
        }
    } else {
        LOG_INFO("Loaded saved devicestate version %d", devicestate.version);
    }

    // Devicestate saved by firmware that allowed 39-byte names gets clamped on
    // first load; from here on owner never carries more than the local cap.
    clampLongName(owner.long_name);

    state = loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(meshtastic_LocalConfig), &meshtastic_LocalConfig_msg,
                      &config);
    if (state == LoadFileResult::DECODE_FAILED) {
        // Config file present but undecodable this boot (corruption / torn write / transient decrypt fail).
        // loadProto() already zeroed `config`, so the keypair is gone from RAM; minting a new one would change
        // our NodeNum (== crc32(public_key)) and orphan us on the mesh. configDecodeFailed freezes identity and
        // skips persisting (see ctor), so a transient failure self-heals on the next clean boot. A genuinely
        // absent config returns OTHER_FAILURE, so this never fires on first boot. Boot degraded + radio-silent.
        LOG_ERROR("Config decode failed - freezing identity, booting degraded (radio silent until restored)");
        configDecodeFailed = true;
        installDefaultConfig(true);
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        config.lora.tx_enabled = false;
    } else if (state != LoadFileResult::LOAD_SUCCESS) {
        // No decodable config to work with: the file is absent (first boot) or could not be opened (OTHER_FAILURE
        // / NO_FILESYSTEM). Unlike DECODE_FAILED there are no usable contents to protect, so install defaults.
        installDefaultConfig();
    } else if (config.version < DEVICESTATE_MIN_VER) {
        LOG_WARN("config %d is old, discard", config.version);
        installDefaultConfig(true);
    } else {
        LOG_INFO("Loaded saved config version %d", config.version);
    }

    // Coerce LoRa config fields derived from presets while bootstrapping.
    // Some clients/UI components display bandwidth/spread_factor directly from config even in preset mode.
    if (config.has_lora && config.lora.use_preset) {
        RadioInterface::clampConfigLora(config.lora);
    }

#if defined(USERPREFS_LORA_TX_DISABLED) && USERPREFS_LORA_TX_DISABLED
    config.lora.tx_enabled = false;
#endif

    // Always-apply LoRa overrides: applied after loading saved config so they
    // take effect even when NVS already has a valid config (e.g. region-locked
    // dev boards with no BLE/serial to set the region at runtime).
#ifdef USERPREFS_CONFIG_LORA_REGION
    // Skip on a degraded boot to keep the radio silent (identity is already protected by the keygen gate).
    if (!configDecodeFailed)
        config.lora.region = USERPREFS_CONFIG_LORA_REGION;
#endif

#ifdef USERPREFS_LORACONFIG_USE_PRESET
    config.lora.use_preset = USERPREFS_LORACONFIG_USE_PRESET;
#endif

#ifdef USERPREFS_LORACONFIG_BANDWIDTH
    config.lora.bandwidth = USERPREFS_LORACONFIG_BANDWIDTH;
#endif

#ifdef USERPREFS_LORACONFIG_SPREAD_FACTOR
    config.lora.spread_factor = USERPREFS_LORACONFIG_SPREAD_FACTOR;
#endif

#ifdef USERPREFS_LORACONFIG_CODING_RATE
    config.lora.coding_rate = USERPREFS_LORACONFIG_CODING_RATE;
#endif

#ifdef USERPREFS_LORACONFIG_OVERRIDE_FREQUENCY
    config.lora.override_frequency = USERPREFS_LORACONFIG_OVERRIDE_FREQUENCY;
#endif

    if (backupSecurity.private_key.size > 0) {
        LOG_DEBUG("Restoring backup of security config");
        config.security = backupSecurity;
        saveToDisk(SEGMENT_CONFIG);
    }

    // Make sure we load hard coded admin keys even when the configuration file has none.
    // Initialize admin_key_count to zero
    byte numAdminKeys = 0;
#if defined(USERPREFS_USE_ADMIN_KEY_0) || defined(USERPREFS_USE_ADMIN_KEY_1) || defined(USERPREFS_USE_ADMIN_KEY_2)
    uint16_t sum = 0;
#endif

#ifdef USERPREFS_USE_ADMIN_KEY_0

    for (uint8_t b = 0; b < 32; b++) {
        sum += config.security.admin_key[0].bytes[b];
    }
    if (sum == 0) {
        numAdminKeys += 1;
        LOG_INFO("Admin 0 key zero. Loading hard coded key from user preferences.");
        memcpy(config.security.admin_key[0].bytes, userprefs_admin_key_0, 32);
        config.security.admin_key[0].size = 32;
    }
#endif

#ifdef USERPREFS_USE_ADMIN_KEY_1
    sum = 0;
    for (uint8_t b = 0; b < 32; b++) {
        sum += config.security.admin_key[1].bytes[b];
    }
    if (sum == 0) {
        numAdminKeys += 1;
        LOG_INFO("Admin 1 key zero. Loading hard coded key from user preferences.");
        memcpy(config.security.admin_key[1].bytes, userprefs_admin_key_1, 32);
        config.security.admin_key[1].size = 32;
    }
#endif

#ifdef USERPREFS_USE_ADMIN_KEY_2
    sum = 0;
    for (uint8_t b = 0; b < 32; b++) {
        sum += config.security.admin_key[2].bytes[b];
    }
    if (sum == 0) {
        numAdminKeys += 1;
        LOG_INFO("Admin 2 key zero. Loading hard coded key from user preferences.");
        memcpy(config.security.admin_key[2].bytes, userprefs_admin_key_2, 32);
        config.security.admin_key[2].size = 32;
    }
#endif

    if (numAdminKeys > 0) {
        LOG_INFO("Saving %d hard coded admin keys.", numAdminKeys);
        config.security.admin_key_count = numAdminKeys;
        saveToDisk(SEGMENT_CONFIG);
    }

    state = loadProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, sizeof(meshtastic_LocalModuleConfig),
                      &meshtastic_LocalModuleConfig_msg, &moduleConfig);
    if (state != LoadFileResult::LOAD_SUCCESS) {
        installDefaultModuleConfig(); // Our in RAM copy might now be corrupt
    } else {
        if (moduleConfig.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("moduleConfig %d is old, discard", moduleConfig.version);
            installDefaultModuleConfig();
        } else {
            LOG_INFO("Loaded saved moduleConfig version %d", moduleConfig.version);
        }
    }

    // Always-on traffic management: a device that has NEVER configured TMM
    // (has_traffic_management false - AdminModule always sets the has_ flag on
    // write, even when disabling) gets the fork defaults. Explicitly configured
    // devices keep their exact settings.
    if (!moduleConfig.has_traffic_management) {
        LOG_INFO("Traffic management never configured, installing always-on defaults");
        installTrafficManagementDefaults(moduleConfig);
        saveToDisk(SEGMENT_MODULECONFIG);
    }

    state = loadProto(channelFileName, meshtastic_ChannelFile_size, sizeof(meshtastic_ChannelFile), &meshtastic_ChannelFile_msg,
                      &channelFile);
    if (state != LoadFileResult::LOAD_SUCCESS) {
        installDefaultChannels(); // Our in RAM copy might now be corrupt
    } else {
        if (channelFile.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("channelFile %d is old, discard", channelFile.version);
            installDefaultChannels();
        } else {
            LOG_INFO("Loaded saved channelFile version %d", channelFile.version);
        }
    }

    state = loadProto(uiconfigFileName, meshtastic_DeviceUIConfig_size, sizeof(meshtastic_DeviceUIConfig),
                      &meshtastic_DeviceUIConfig_msg, &uiconfig);
    if (state == LoadFileResult::LOAD_SUCCESS) {
        LOG_INFO("Loaded UIConfig");
    }

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // Ensure all config segments are persisted to encrypted storage.
    // installDefaultConfig/installDefaultModuleConfig only set in-memory structs
    // without saving to disk, so we force a save here to ensure encrypted files exist.
    //
    // Only when lockdown is ACTIVE. A capable-but-off device must leave its
    // files as plaintext - encryptAndWrite would fail anyway (no DEK), but
    // skipping the whole block avoids the wasted attempts and error logs.
    if (EncryptedStorage::isLockdownActive()) {
        const char *filesToCheck[] = {configFileName, moduleConfigFileName, channelFileName, deviceStateFileName,
                                      nodeDatabaseFileName};
        const int segments[] = {SEGMENT_CONFIG, SEGMENT_MODULECONFIG, SEGMENT_CHANNELS, SEGMENT_DEVICESTATE,
                                SEGMENT_NODEDATABASE};
        int toSave = 0;
        for (int i = 0; i < 5; i++) {
            if (!EncryptedStorage::isEncrypted(filesToCheck[i])) {
                toSave |= segments[i];
            }
        }
        if (toSave) {
            LOG_INFO("Lockdown: Saving unencrypted segments to encrypted storage (mask=0x%x)", toSave);
            saveToDisk(toSave);
        }

        // Migrate any remaining plaintext proto files (from standard firmware upgrade)
        for (const char *fn : filesToCheck) {
            if (!EncryptedStorage::isEncrypted(fn)) {
                LOG_INFO("Migrating %s to encrypted storage", fn);
                EncryptedStorage::migrateFile(fn);
            }
        }
    }
#endif

    // 2.4.X - configuration migration to update new default intervals
    if (moduleConfig.version < 23) {
        LOG_DEBUG("ModuleConfig version %d is stale, upgrading to new default intervals", moduleConfig.version);
        moduleConfig.version = DEVICESTATE_CUR_VER;
        if (moduleConfig.telemetry.device_update_interval == 900)
            moduleConfig.telemetry.device_update_interval = 0;
        if (moduleConfig.telemetry.environment_update_interval == 900)
            moduleConfig.telemetry.environment_update_interval = 0;
        if (moduleConfig.telemetry.air_quality_interval == 900)
            moduleConfig.telemetry.air_quality_interval = 0;
        if (moduleConfig.telemetry.power_update_interval == 900)
            moduleConfig.telemetry.power_update_interval = 0;
        if (moduleConfig.neighbor_info.update_interval == 900)
            moduleConfig.neighbor_info.update_interval = 0;
        if (moduleConfig.paxcounter.paxcounter_update_interval == 900)
            moduleConfig.paxcounter.paxcounter_update_interval = 0;

        saveToDisk(SEGMENT_MODULECONFIG);
    }
#if ARCH_PORTDUINO
    // set any config overrides
    if (portduino_config.has_configDisplayMode) {
        config.display.displaymode = (_meshtastic_Config_DisplayConfig_DisplayMode)portduino_config.configDisplayMode;
    }
    if (portduino_config.has_statusMessage) {
        moduleConfig.has_statusmessage = true;
        strncpy(moduleConfig.statusmessage.node_status, portduino_config.statusMessage.c_str(),
                sizeof(moduleConfig.statusmessage.node_status));
        moduleConfig.statusmessage.node_status[sizeof(moduleConfig.statusmessage.node_status) - 1] = '\0';
    }
    if (portduino_config.enable_UDP) {
        config.network.enabled_protocols = meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST;
    }

#endif
}

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
// Serializes reloadFromDisk against itself. Other readers of config /
// channelFile / nodeDatabase don't take this lock today, so this only
// prevents reload-vs-reload races (e.g. fast successive unlocks). It is
// not a full data-race fix for those structs - that would require
// thread-shared locking discipline across the whole codebase, beyond
// the audit's M7 scope. The radio standby+reconfigure below keeps the
// radio out of the window where SX12xx registers are mid-swap.
static concurrency::Lock g_reloadFromDiskMutex;

/**
 * Re-run loadFromDisk() after encrypted storage is unlocked at runtime.
 * Holds the radio in standby across the file IO + proto decode so the
 * SX12xx is not mid-RX/TX when config.lora is overwritten, then calls
 * reconfigure() to push the now-real settings to the chip.
 *
 * Returns true iff every encrypted file decrypted and decoded cleanly.
 * On false the caller MUST treat storage as corrupt - see header.
 */
bool NodeDB::reloadFromDisk()
{
    concurrency::LockGuard guard(&g_reloadFromDiskMutex);
    LOG_INFO("NodeDB: Reloading config from encrypted storage after unlock");

    RadioInterface *rIface = router ? router->getRadioIface() : nullptr;

    // Park the radio while config.lora / channelFile swap. Without this,
    // a concurrent send or receive can read half-old / half-new state
    // (channel keys, region, modem preset) and the SX12xx ends up in
    // an inconsistent register set that only a reboot recovers from.
    if (rIface)
        rIface->sleep();

    loadFromDisk();

    if (storageCorruptThisLoad) {
        LOG_ERROR("NodeDB: storage decrypt/decode failed during reload - surfacing as corrupt");
        // Leave the radio sleeping. Caller will lock storage and emit
        // a LOCKED(storage_corrupt) status; we must not reconfigure
        // the chip with the locked-default placeholder values still
        // sitting in config.lora.
        return false;
    }

    // loadFromDisk() leaves the store untrimmed; run self-care now (getNodeNum()
    // is valid at runtime) to trim/demote non-self overflow, pin self to index 0
    // and normalise the backing store before the node DB is exercised again.
    nodeDBSelfCare();

    // Preserve constructor ordering: persist any migration only after self-care.
    if (migrationSavePending) {
        saveNodeDatabaseToDisk();
        migrationSavePending = false;
    }

    // Push the now-real config to the radio.
    if (rIface) {
        channels.onConfigChanged();
        rIface->reconfigure();
    }
    return true;
}

bool NodeDB::disableLockdownToPlaintext()
{
    concurrency::LockGuard guard(&g_reloadFromDiskMutex);
    if (!EncryptedStorage::isUnlocked()) {
        LOG_ERROR("NodeDB: disable requested but storage not unlocked");
        return false;
    }
    LOG_INFO("NodeDB: reverting encrypted prefs to plaintext for lockdown disable");

    // Decrypt each encrypted pref back to plaintext IN PLACE. Mirror of the
    // plaintext->encrypted migrate loop above. Order does not matter here;
    // EncryptedStorage::removeLockdownArtifacts() (which deletes the DEK,
    // the commit point) only runs after every file is confirmed plaintext.
    const char *filesToCheck[] = {configFileName, moduleConfigFileName, channelFileName, deviceStateFileName,
                                  nodeDatabaseFileName};
    for (const char *fn : filesToCheck) {
        if (!EncryptedStorage::migrateFileToPlaintext(fn)) {
            LOG_ERROR("NodeDB: failed to revert %s to plaintext; aborting disable (device stays in lockdown)", fn);
            return false;
        }
    }

    // All files are plaintext now - remove the lockdown artifacts. Deleting
    // /prefs/.dek is the atomic commit: after it, isLockdownActive() is false.
    EncryptedStorage::removeLockdownArtifacts();
    return true;
}
#endif

/** Save a protobuf from a file, return true for success */
bool NodeDB::saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct,
                       bool fullAtomic)
{

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway. Device should be sleeping at this point anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveProto() on unsafe device power level.");
        return false;
    }

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // Encrypt all files except uiconfig (no secrets) and the DEK file (self-encrypted).
    // Only when lockdown is ACTIVE (provisioned). A lockdown-capable but DISABLED
    // device has no DEK, so encryptAndWrite would fail and config would never
    // persist - it must save plaintext exactly like stock firmware. Once enabled,
    // the reloadFromDisk migrate pass re-saves these plaintext files encrypted.
    if (EncryptedStorage::isLockdownActive() && strcmp(filename, uiconfigFileName) != 0) {
        // ZeroizingArrayPtr wipes the unencrypted protobuf encoding (which contains
        // config secrets - channel PSKs, security private_key, etc.) before delete[],
        // so plaintext copies aren't left in heap memory after encryption completes.
        auto pbBuf = meshtastic_security::make_zeroizing_array(protoSize);
        if (!pbBuf) {
            LOG_ERROR("OOM encoding %s for encryption", filename);
            return false;
        }

        pb_ostream_t stream = pb_ostream_from_buffer(pbBuf.get(), protoSize);
        if (!pb_encode(&stream, fields, dest_struct)) {
            LOG_ERROR("Error: can't encode protobuf %s", PB_GET_ERROR(&stream));
            return false;
        }

        size_t encodedSize = stream.bytes_written;
        bool ok = EncryptedStorage::encryptAndWrite(filename, pbBuf.get(), encodedSize, fullAtomic);

        if (!ok) {
            LOG_ERROR("EncryptedStorage: Failed to encrypt and write %s", filename);
        }
        return ok;
    }
#endif

    bool okay = false;
#ifdef FSCom
    auto f = SafeFile(filename, fullAtomic);

    LOG_INFO("Save %s", filename);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&f), protoSize};

    if (!pb_encode(&stream, fields, dest_struct)) {
        LOG_ERROR("Error: can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }

    bool writeSucceeded = f.close();

    if (!okay || !writeSucceeded) {
        LOG_ERROR("Can't write prefs!");
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
    return okay;
}

bool NodeDB::saveChannelsToDisk()
{

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveChannelsToDisk() on unsafe device power level.");
        return false;
    }

#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    return saveProto(channelFileName, meshtastic_ChannelFile_size, &meshtastic_ChannelFile_msg, &channelFile);
}

bool NodeDB::saveDeviceStateToDisk()
{

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway. Device should be sleeping at this point anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveDeviceStateToDisk() on unsafe device power level.");
        return false;
    }

#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif
    // Note: if MAX_NUM_NODES=100 and meshtastic_NodeInfoLite_size=166, so will be approximately 17KB
    // Because so huge we _must_ not use fullAtomic, because the filesystem is probably too small to hold two copies of this
    return saveProto(deviceStateFileName, meshtastic_DeviceState_size, &meshtastic_DeviceState_msg, &devicestate, true);
}

bool NodeDB::saveNodeDatabaseToDisk()
{
    // Don't persist the node DB until this device has a PKI keypair
    // TODO: revisit when https://github.com/meshtastic/firmware/pull/10478 lands
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    if (owner.public_key.size != 32 && !owner.is_licensed) {
        LOG_DEBUG("Skip NodeDB without key");
        return true;
    }
#endif

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway. Device should be sleeping at this point anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveNodeDatabaseToDisk() on unsafe device power level.");
        return false;
    }

    // Defer (don't fail) while xmodem holds the prefs file handle. Returning false
    // would propagate through saveToDisk() and trigger fsFormat() mid-transfer.
#ifdef FSCom
    if (xModem.isBusy()) {
        LOG_DEBUG("Deferring NodeDB save: xmodem transfer in progress");
        return true;
    }
#endif

#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    // Project the maps into the on-disk vectors just before encoding; cleared
    // again on the way out so we don't carry duplicate state.
    concurrency::LockGuard guard(&satelliteMutex);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    nodeDatabase.positions.clear();
    nodeDatabase.positions.reserve(nodePositions.size());
    for (const auto &kv : nodePositions) {
        meshtastic_NodePositionEntry entry = meshtastic_NodePositionEntry_init_default;
        entry.num = kv.first;
        entry.has_position = true;
        entry.position = kv.second;
        nodeDatabase.positions.push_back(entry);
    }
#else
    nodeDatabase.positions.clear();
#endif

#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    nodeDatabase.telemetry.clear();
    nodeDatabase.telemetry.reserve(nodeTelemetry.size());
    for (const auto &kv : nodeTelemetry) {
        meshtastic_NodeTelemetryEntry entry = meshtastic_NodeTelemetryEntry_init_default;
        entry.num = kv.first;
        entry.has_device_metrics = true;
        entry.device_metrics = kv.second;
        nodeDatabase.telemetry.push_back(entry);
    }
#else
    nodeDatabase.telemetry.clear();
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    nodeDatabase.environment.clear();
    nodeDatabase.environment.reserve(nodeEnvironment.size());
    for (const auto &kv : nodeEnvironment) {
        meshtastic_NodeEnvironmentEntry entry = meshtastic_NodeEnvironmentEntry_init_default;
        entry.num = kv.first;
        entry.has_environment_metrics = true;
        entry.environment_metrics = kv.second;
        nodeDatabase.environment.push_back(entry);
    }
#else
    nodeDatabase.environment.clear();
#endif

#if !MESHTASTIC_EXCLUDE_STATUSDB
    nodeDatabase.status.clear();
    nodeDatabase.status.reserve(nodeStatus.size());
    for (const auto &kv : nodeStatus) {
        meshtastic_NodeStatusEntry entry = meshtastic_NodeStatusEntry_init_default;
        entry.num = kv.first;
        entry.has_status = true;
        entry.status = kv.second;
        nodeDatabase.status.push_back(entry);
    }
#else
    nodeDatabase.status.clear();
#endif

    size_t nodeDatabaseSize;
    pb_get_encoded_size(&nodeDatabaseSize, meshtastic_NodeDatabase_fields, &nodeDatabase);
    bool ok = saveProto(nodeDatabaseFileName, nodeDatabaseSize, &meshtastic_NodeDatabase_msg, &nodeDatabase, false);

    nodeDatabase.positions.clear();
    nodeDatabase.positions.shrink_to_fit();
    nodeDatabase.telemetry.clear();
    nodeDatabase.telemetry.shrink_to_fit();
    nodeDatabase.environment.clear();
    nodeDatabase.environment.shrink_to_fit();
    nodeDatabase.status.clear();
    nodeDatabase.status.shrink_to_fit();
#if WARM_NODE_COUNT > 0
#ifdef ARCH_RP2040
    // nodes.proto + warm.dat are written back-to-back without the loop running between them;
    // reset the 8s HW watchdog so the second write gets a full budget (issue #10746).
    watchdog_update();
#endif
    // Same cadence as the node DB; failure is logged but must not propagate -
    // a false return from here would trigger saveToDisk()'s fsFormat() path.
    warmStore.saveIfDirty();
#endif
    return ok;
}

bool NodeDB::saveToDiskNoRetry(int saveWhat)
{

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway. Device should be sleeping at this point anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveToDiskNoRetry() on unsafe device power level.");
        return false;
    }

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // When lockdown is ACTIVE but storage is still locked, encryptAndWrite()
    // returns false for every file. That would cause saveToDisk()'s nRF52 retry
    // path to call FSCom.format(), wiping all encrypted proto files from flash.
    // Return true here - "nothing to save, not an error."
    //
    // Gate on isLockdownActive(): a lockdown-capable but DISABLED device (never
    // provisioned) also has isUnlocked()==false, but it must persist plaintext
    // normally - skipping here would silently drop every config write (e.g. the
    // LoRa region) until the device is provisioned.
    if (EncryptedStorage::isLockdownActive() && !EncryptedStorage::isUnlocked()) {
        LOG_WARN("NodeDB: saveToDisk skipped - encrypted storage locked");
        return true;
    }
#endif

    bool success = true;
#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    if (saveWhat & SEGMENT_CONFIG) {
        config.has_device = true;
        config.has_display = true;
        config.has_lora = true;
        config.has_position = true;
        config.has_power = true;
        config.has_network = true;
        config.has_bluetooth = true;
        config.has_security = true;

        success &= saveProto(configFileName, meshtastic_LocalConfig_size, &meshtastic_LocalConfig_msg, &config);
    }

    if (saveWhat & SEGMENT_MODULECONFIG) {
        moduleConfig.has_canned_message = true;
        moduleConfig.has_external_notification = true;
        moduleConfig.has_mqtt = true;
        moduleConfig.has_range_test = true;
        moduleConfig.has_serial = true;
        moduleConfig.has_store_forward = true;
        moduleConfig.has_telemetry = true;
        moduleConfig.has_neighbor_info = true;
        moduleConfig.has_detection_sensor = true;
        moduleConfig.has_ambient_lighting = true;
        moduleConfig.has_audio = true;
        moduleConfig.has_paxcounter = true;
        moduleConfig.has_statusmessage = true;
#if !MESHTASTIC_EXCLUDE_BEACON
        moduleConfig.has_mesh_beacon = true;
#endif

        success &=
            saveProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, &meshtastic_LocalModuleConfig_msg, &moduleConfig);
    }

    if (saveWhat & SEGMENT_CHANNELS) {
        success &= saveChannelsToDisk();
    }

    if (saveWhat & SEGMENT_DEVICESTATE) {
        success &= saveDeviceStateToDisk();
    }

    if (saveWhat & SEGMENT_NODEDATABASE) {
        success &= saveNodeDatabaseToDisk();
    }

    return success;
}

bool NodeDB::saveToDisk(int saveWhat)
{
    LOG_DEBUG("Save to disk %d", saveWhat);

    // do not try to save anything if power level is not safe. In many cases flash will be lock-protected
    // and all writes will fail anyway. Device should be sleeping at this point anyway.
    if (!powerHAL_isPowerLevelSafe()) {
        LOG_ERROR("Error: trying to saveToDisk() on unsafe device power level.");
        return false;
    }

    bool success = saveToDiskNoRetry(saveWhat);

    if (!success) {
        LOG_ERROR("Failed to save to disk, retrying");
        spiLock->lock();
        fsFormat();
        spiLock->unlock();

        success = saveToDiskNoRetry(saveWhat);

        RECORD_CRITICALERROR(success ? meshtastic_CriticalErrorCode_FLASH_CORRUPTION_RECOVERABLE
                                     : meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
    }

    return success;
}

const meshtastic_NodeInfoLite *NodeDB::readNextMeshNode(uint32_t &readIndex)
{
    if (readIndex < numMeshNodes)
        return &meshNodes->at(readIndex++);
    else
        return NULL;
}

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const meshtastic_NodeInfoLite *n)
{
    uint32_t now = getTime();

    int delta = (int)(now - n->last_heard);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}

uint32_t sinceReceived(const meshtastic_MeshPacket *p)
{
    uint32_t now = getTime();

    int delta = (int)(now - p->rx_time);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}

HopStartStatus classifyHopStart(const meshtastic_MeshPacket &p)
{
    // Guard against invalid values.
    if (p.hop_start < p.hop_limit)
        return HopStartStatus::INVALID;

    if (p.hop_start == 0) {
        // hop_start == hop_limit == 0: intentional zero-hop broadcast (e.g. beacon). Valid by definition -
        // the packet was never meant to travel any hops, so no hop_start ambiguity applies.
        if (p.hop_limit == 0)
            return HopStartStatus::VALID;
        // Firmware prior to 2.3.0 (585805c) lacked a hop_start field. Firmware version 2.5.0 (bf34329) introduced a
        // bitfield that is always present. Use the presence of the bitfield to determine if the origin's firmware
        // version is guaranteed to have hop_start populated. Note that this can only be done for decoded packets as
        // the bitfield is encrypted under the channel encryption key.
        if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag && p.decoded.has_bitfield)
            return HopStartStatus::VALID;
        return HopStartStatus::MISSING_OR_UNKNOWN;
    }

    return HopStartStatus::VALID;
}

int8_t getHopsAway(const meshtastic_MeshPacket &p, int8_t defaultIfUnknown)
{
    // Firmware prior to 2.3.0 (585805c) lacked a hop_start field. Firmware version 2.5.0 (bf34329) introduced a
    // bitfield that is always present. Use the presence of the bitfield to determine if the origin's firmware
    // version is guaranteed to have hop_start populated. Note that this can only be done for decoded packets as
    // the bitfield is encrypted under the channel encryption key. For encrypted packets, this returns
    // defaultIfUnknown when hop_start is 0.
    if (p.hop_start == 0 && !(p.which_payload_variant == meshtastic_MeshPacket_decoded_tag && p.decoded.has_bitfield))
        return defaultIfUnknown; // Cannot reliably determine the number of hops.

    // Guard against invalid values.
    if (p.hop_start < p.hop_limit)
        return defaultIfUnknown;

    return p.hop_start - p.hop_limit;
}

#define NUM_ONLINE_SECS (60 * 60 * 2) // 2 hrs to consider someone offline

size_t NodeDB::getNumOnlineMeshNodes(bool localOnly)
{
    size_t numseen = 0;

    // FIXME this implementation is kinda expensive
    for (int i = 0; i < numMeshNodes; i++) {
        if (localOnly && nodeInfoLiteViaMqtt(&meshNodes->at(i)))
            continue;
        if (sinceLastSeen(&meshNodes->at(i)) < NUM_ONLINE_SECS)
            numseen++;
    }

    return numseen;
}

#include "MeshModule.h"
#include "Throttle.h"

static constexpr uint32_t HOPSTART_DROP_LOG_INTERVAL_MS = 15000;

void logHopStartDrop(const meshtastic_MeshPacket &p, const char *context)
{
    static uint32_t lastLogMs = 0;
    if (Throttle::isWithinTimespanMs(lastLogMs, HOPSTART_DROP_LOG_INTERVAL_MS)) {
        return;
    }
    lastLogMs = millis();
    const bool decoded = (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag);
    const bool hasBitfield = decoded && p.decoded.has_bitfield;
    LOG_DEBUG(
        "Drop packet (%s): hop_start invalid/missing (from=0x%08x id=%u hop_start=%u hop_limit=%u decoded=%d has_bitfield=%d)",
        context ? context : "unknown", p.from, p.id, p.hop_start, p.hop_limit, decoded, hasBitfield);
}

/** Update position info for this node based on received position data
 */
void NodeDB::updatePosition(uint32_t nodeId, const meshtastic_Position &p, RxSource src)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    if (!info) {
        return;
    }

#if MESHTASTIC_EXCLUDE_POSITIONDB
    // Build flag opted out: header still tracks last_heard via updateFrom; we
    // simply don't cache the position payload anywhere on this device.
    if (src == RX_SRC_LOCAL) {
        LOG_INFO("updatePosition LOCAL (PositionDB excluded) time=%u lat=%d lon=%d alt=%d", p.time, p.latitude_i, p.longitude_i,
                 p.altitude);
        setLocalPosition(p);
    }
    (void)nodeId;
    updateGUIforNode = info;
    notifyObservers(true);
#else
    {
        concurrency::LockGuard guard(&satelliteMutex);
        evictSatelliteOverCap(*this, nodePositions, nodeId);
        meshtastic_PositionLite &slot = nodePositions[nodeId]; // creates default-zero entry if missing

        if (src == RX_SRC_LOCAL) {
            // Local packet, fully authoritative
            LOG_INFO("updatePosition LOCAL pos@%x time=%u lat=%d lon=%d alt=%d", p.timestamp, p.time, p.latitude_i, p.longitude_i,
                     p.altitude);

            setLocalPosition(p);
            slot = TypeConversions::ConvertToPositionLite(p);
        } else if ((p.time > 0) && !p.latitude_i && !p.longitude_i && !p.timestamp && !p.location_source) {
            // FIXME SPECIAL TIME SETTING PACKET FROM EUD TO RADIO
            // (stop-gap fix for issue #900)
            LOG_DEBUG("updatePosition SPECIAL time setting time=%u", p.time);
            slot.time = p.time;
        } else {
            // Be careful to only update fields that have been set by the REMOTE sender
            // A lot of position reports don't have time populated.  In that case, be careful to not blow away the time we
            // recorded based on the packet rxTime
            //
            // FIXME perhaps handle RX_SRC_USER separately?
            LOG_INFO("updatePosition REMOTE node=0x%08x time=%u lat=%d lon=%d", nodeId, p.time, p.latitude_i, p.longitude_i);

            // First, back up fields that we want to protect from overwrite
            uint32_t tmp_time = slot.time;

            // Next, update atomically
            slot = TypeConversions::ConvertToPositionLite(p);

            // Last, restore any fields that may have been overwritten
            if (!slot.time)
                slot.time = tmp_time;
        }
    }
    updateGUIforNode = info;
    notifyObservers(true); // Force an update whether or not our node counts have changed
#endif
}

/** Update telemetry info for this node based on received metrics. Stores
 *  device_metrics and environment_metrics into their respective satellite
 *  maps; other variants (air_quality, power, local_stats, health) are
 *  intentionally not retained per-node.
 */
void NodeDB::updateTelemetry(uint32_t nodeId, const meshtastic_Telemetry &t, RxSource src)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    if (!info)
        return;

    if (t.which_variant == meshtastic_Telemetry_device_metrics_tag) {
        if (src == RX_SRC_LOCAL) {
            LOG_DEBUG("updateTelemetry LOCAL device");
        } else {
            LOG_DEBUG("updateTelemetry REMOTE device node=0x%08x", nodeId);
        }
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
        concurrency::LockGuard guard(&satelliteMutex);
        evictSatelliteOverCap(*this, nodeTelemetry, nodeId);
        nodeTelemetry[nodeId] = t.variant.device_metrics;
#endif

    } else if (t.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
        if (src == RX_SRC_LOCAL) {
            LOG_DEBUG("updateTelemetry LOCAL env");
        } else {
            LOG_DEBUG("updateTelemetry REMOTE env node=0x%08x", nodeId);
        }
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
        concurrency::LockGuard guard(&satelliteMutex);
        evictSatelliteOverCap(*this, nodeEnvironment, nodeId);
        nodeEnvironment[nodeId] = t.variant.environment_metrics;
#endif

    } else {
        return; // air_quality / power / local_stats / health: not stored per-node
    }
    updateGUIforNode = info;
    notifyObservers(true);
}

/**
 * Update the node database with a new contact
 */
void NodeDB::addFromContact(meshtastic_SharedContact contact)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(contact.node_num);
    if (!info || !contact.has_user) {
        return;
    }
    // If the local node has this node marked as manually verified
    // and the client does not, do not allow the client to update the
    // saved public key.
    if (nodeInfoLiteIsKeyManuallyVerified(info) && !contact.manually_verified) {
        if (contact.user.public_key.size != info->public_key.size ||
            memcmp(contact.user.public_key.bytes, info->public_key.bytes, info->public_key.size) != 0) {
            return;
        }
    }
    info->num = contact.node_num;
    TypeConversions::CopyUserToNodeInfoLite(info, contact.user);
    if (contact.should_ignore) {
        // Block the contact and drop its rich satellite data, but keep the
        // public key copied above - an ignored peer keeps a usable identity
        // (a verifiable target) rather than a bare node number.
        if (!setProtectedFlag(info, NODEINFO_BITFIELD_IS_IGNORED_MASK, true))
            LOG_WARN(PROTECTED_CAP_WARN_FMT, "ignore", contact.node_num, MAX_NUM_NODES - 2);
        nodeInfoLiteSetBit(info, NODEINFO_BITFIELD_IS_FAVORITE_MASK, false);
        eraseNodeSatellites(contact.node_num);
    } else {
        /* Clients are sending add_contact before every text message DM (because clients may hold a larger node database with
         * public keys than the radio holds). However, we don't want to update last_heard just because we sent someone a DM!
         */

        /* "Boring old nodes" are the first to be evicted out of the node database when full. This includes a newly-zeroed
         * nodeinfo because it has: !is_favorite && last_heard==0. To keep this from happening when we addFromContact, we set the
         * new node as a favorite, and we leave last_heard alone (even if it's zero).
         */
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE) {
            // Special case for CLIENT_BASE: is_favorite has special meaning, and we don't want to automatically set it
            // without the user doing so deliberately. We don't normally expect users to use a CLIENT_BASE to send DMs or to add
            // contacts, but we should make sure it doesn't auto-favorite in case they do. Instead, as a workaround, we'll set
            // last_heard to now, so that the add_contact node doesn't immediately get evicted.
            info->last_heard = getTime();
        } else {
            // Normal case: set is_favorite to prevent expiration.
            // last_heard will remain as-is (or remain 0 if this entry wasn't in the nodeDB).
            // If the protected cap refuses the favorite, fall back to stamping last_heard so the
            // contact still isn't the first eviction victim.
            if (!setProtectedFlag(info, NODEINFO_BITFIELD_IS_FAVORITE_MASK, true))
                info->last_heard = getTime();
        }

        // As the clients will begin sending the contact with DMs, we want to strictly check if the node is manually verified
        if (contact.manually_verified) {
            if (!setProtectedFlag(info, NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true))
                LOG_WARN(PROTECTED_CAP_WARN_FMT, "verify", contact.node_num, MAX_NUM_NODES - 2);
        }
        // Mark the node's key as manually verified to indicate trustworthiness.
        updateGUIforNode = info;
        sortMeshDB();
        notifyObservers(true); // Force an update whether or not our node counts have changed
    }
    saveNodeDatabaseToDisk();
}

/** Update user info and channel for this node based on received user data
 */
bool NodeDB::updateUser(uint32_t nodeId, meshtastic_User &p, uint8_t channelIndex)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    if (!info) {
        return false;
    }

#if !(MESHTASTIC_EXCLUDE_PKI)
    if (p.public_key.size == 32 && nodeId != nodeDB->getNodeNum()) {
        printBytes("Incoming Pubkey: ", p.public_key.bytes, 32);

        // Alert the user if a remote node is advertising public key that matches our own
        if (owner.public_key.size == 32 && memcmp(p.public_key.bytes, owner.public_key.bytes, 32) == 0) {
            if (!duplicateWarned) {
                duplicateWarned = true;
                // Sanitize before embedding long_name in the phone-facing ClientNotification string
                // (defense-in-depth vs PB_VALIDATE_UTF8).
                char safeName[sizeof(p.long_name)];
                strncpy(safeName, p.long_name, sizeof(safeName));
                safeName[sizeof(safeName) - 1] = '\0';
                sanitizeUtf8(safeName, sizeof(safeName));
                char warning[] =
                    "Remote device %s has advertised your public key. This may indicate a compromised key. You may need "
                    "to regenerate your public keys.";
                LOG_WARN(warning, safeName);
                meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
                cn->level = meshtastic_LogRecord_Level_WARNING;
                cn->time = getValidTime(RTCQualityFromNet);
                snprintf(cn->message, sizeof(cn->message), warning, safeName);
                service->sendClientNotification(cn);
            }
            return false;
        }
    }
    if (info->public_key.size == 32) { // if we have a key for this user already, don't overwrite with a new one
        // if the key doesn't match, don't update nodeDB at all.
        if (p.public_key.size != 32 || (memcmp(p.public_key.bytes, info->public_key.bytes, 32) != 0)) {
            LOG_WARN("Public Key mismatch, dropping NodeInfo");
            return false;
        }
        LOG_INFO("Public Key set for node, not updating!");
    } else if (p.public_key.size == 32) {
        LOG_INFO("Update Node Pubkey!");
    }
#endif

    // Always ensure user.id is derived from nodeId, regardless of what was received
    snprintf(p.id, sizeof(p.id), "!%08x", nodeId);

    meshtastic_NodeInfoLite before = *info;
    TypeConversions::CopyUserToNodeInfoLite(info, p);
    bool changed =
        (memcmp(before.long_name, info->long_name, sizeof(info->long_name)) != 0) ||
        (memcmp(before.short_name, info->short_name, sizeof(info->short_name)) != 0) || (before.hw_model != info->hw_model) ||
        (before.role != info->role) || (before.public_key.size != info->public_key.size) ||
        (info->public_key.size > 0 && memcmp(before.public_key.bytes, info->public_key.bytes, info->public_key.size) != 0) ||
        (before.bitfield != info->bitfield) || (info->channel != channelIndex);

    if (info->public_key.size == 32) {
        printBytes("Saved Pubkey: ", info->public_key.bytes, 32);
    }
    if (nodeId != getNodeNum())
        info->channel = channelIndex; // Set channel we need to use to reach this node (but don't set our own channel)
    LOG_DEBUG("Update changed=%d user %s/%s, id=0x%08x, channel=%d", changed, info->long_name, info->short_name, nodeId,
              info->channel);

    if (changed) {
        updateGUIforNode = info;
        notifyObservers(true); // Force an update whether or not our node counts have changed

        // We just changed something about a User,
        // store our DB unless we just did so less than a minute ago

        if (!Throttle::isWithinTimespanMs(lastNodeDbSave, ONE_MINUTE_MS)) {
            saveToDisk(SEGMENT_NODEDATABASE);
            lastNodeDbSave = millis();
        } else {
            LOG_DEBUG("Defer NodeDB saveToDisk for now");
        }
    }

    return changed;
}

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const meshtastic_MeshPacket &mp)
{
    if (mp.from == getNodeNum()) {
        LOG_DEBUG("Ignore update from self");
        return;
    }
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.from) {
        LOG_DEBUG("Update DB node 0x%08x, rx_time=%u", mp.from, mp.rx_time);

        meshtastic_NodeInfoLite *info = getOrCreateMeshNode(getFrom(&mp));
        if (!info) {
            return;
        }

        if (mp.rx_time) // if the packet has a valid timestamp use it to update our last_heard
            info->last_heard = mp.rx_time;

        if (mp.rx_snr)
            info->snr = mp.rx_snr; // keep the most recent SNR we received for this node.

        nodeInfoLiteSetBit(info, NODEINFO_BITFIELD_VIA_MQTT_MASK,
                           mp.via_mqtt); // Store if we received this packet via MQTT

#if HAS_VARIABLE_HOPS
        // Only sample genuine RF-origin packets. The transport check excludes packets received
        // directly from the broker (TRANSPORT_MQTT), but an MQTT-origin packet rebroadcast onto
        // LoRa by a gateway arrives as TRANSPORT_LORA with via_mqtt set - count those would
        // inflate the local mesh-size estimate with non-RF nodes (and they usually carry
        // hop_start==0, landing in the hop-0 bucket that pulls the recommendation lowest), so
        // exclude via_mqtt too.
        if (mp.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA && !mp.via_mqtt &&
            hopScalingModule) {
            uint8_t hopCount = std::max(int8_t(0), getHopsAway(mp));
            hopScalingModule->samplePacketForHistogram(mp.from, hopCount);
        }
#endif

        // If hopStart was set and there wasn't someone messing with the limit in the middle, add hopsAway
        const int8_t hopsAway = getHopsAway(mp);
        if (hopsAway >= 0) {
            info->has_hops_away = true;
            info->hops_away = hopsAway;
        }
        sortMeshDB();
    }
}

int NodeDB::numProtectedNodes() const
{
    int count = 0;
    for (int i = 0; i < numMeshNodes; i++)
        if (nodeInfoLiteIsProtected(&meshNodes->at(i)))
            count++;
    return count;
}

bool NodeDB::setProtectedFlag(meshtastic_NodeInfoLite *node, uint32_t mask, bool on)
{
    if (!node)
        return false;
    if (!on) {
        nodeInfoLiteSetBit(node, mask, false);
        return true;
    }
    // Adding a flag to a node that is already protected doesn't grow the
    // protected set, so it's always allowed. A newly-protected node is refused
    // once the protected set has reached MAX_NUM_NODES-2, leaving two evictable
    // slots so getOrCreateMeshNode can always make room.
    if (nodeInfoLiteIsProtected(node) || numProtectedNodes() < MAX_NUM_NODES - 2) {
        nodeInfoLiteSetBit(node, mask, true);
        return true;
    }
    return false;
}

bool NodeDB::set_favorite(bool is_favorite, uint32_t nodeId)
{
    meshtastic_NodeInfoLite *lite = getMeshNode(nodeId);
    if (!lite)
        return false;
    if (nodeInfoLiteIsFavorite(lite) == is_favorite)
        return true; // already in the requested state
    if (setProtectedFlag(lite, NODEINFO_BITFIELD_IS_FAVORITE_MASK, is_favorite)) {
        sortMeshDB();
        saveNodeDatabaseToDisk();
        return true;
    }
    LOG_WARN(PROTECTED_CAP_WARN_FMT, "favorite", nodeId, MAX_NUM_NODES - 2);
    return false;
}

bool NodeDB::isFavorite(uint32_t nodeId)
{
    // returns true if nodeId is_favorite; false if not or not found

    // NODENUM_BROADCAST will never be in the DB
    if (nodeId == NODENUM_BROADCAST)
        return false;

    const meshtastic_NodeInfoLite *lite = getMeshNode(nodeId);

    if (lite) {
        return nodeInfoLiteIsFavorite(lite);
    }
    return false;
}

bool NodeDB::isFromOrToFavoritedNode(const meshtastic_MeshPacket &p)
{
    // This method is logically equivalent to:
    //   return isFavorite(p.from) || isFavorite(p.to);
    // but is more efficient by:
    //   1. doing only one pass through the database, instead of two
    //   2. exiting early when a favorite is found, or if both from and to have been seen

    if (p.to == NODENUM_BROADCAST)
        return isFavorite(p.from); // we never store NODENUM_BROADCAST in the DB, so we only need to check p.from

    meshtastic_NodeInfoLite *lite = NULL;

    bool seenFrom = false;
    bool seenTo = false;

    for (int i = 0; i < numMeshNodes; i++) {
        lite = &meshNodes->at(i);

        if (lite->num == p.from) {
            if (nodeInfoLiteIsFavorite(lite))
                return true;

            seenFrom = true;
        }

        if (lite->num == p.to) {
            if (nodeInfoLiteIsFavorite(lite))
                return true;

            seenTo = true;
        }

        if (seenFrom && seenTo)
            return false; // we've seen both, and neither is a favorite, so we can stop searching early

        // Note: if we knew that sortMeshDB was always called after any change to is_favorite, we could exit early after searching
        // all favorited nodes first.
    }

    return false;
}

void NodeDB::pause_sort(bool paused)
{
    sortingIsPaused = paused;
}

void NodeDB::sortMeshDB()
{
    if (!sortingIsPaused && (lastSort == 0 || !Throttle::isWithinTimespanMs(lastSort, 1000 * 5))) {
        lastSort = millis();
        bool changed = true;
        while (changed) { // dumb reverse bubble sort, but probably not bad for what we're doing
            changed = false;
            for (int i = numMeshNodes - 1; i > 0; i--) { // lowest case this should examine is i == 1
                if (meshNodes->at(i - 1).num == getNodeNum()) {
                    // noop
                } else if (meshNodes->at(i).num ==
                           getNodeNum()) { // in the oddball case our own node num is not at location 0, put it there
                    // TODO: Look for at(i-1) also matching own node num, and throw the DB in the trash
                    std::swap(meshNodes->at(i), meshNodes->at(i - 1));
                    changed = true;
                } else if (nodeInfoLiteIsFavorite(&meshNodes->at(i)) && !nodeInfoLiteIsFavorite(&meshNodes->at(i - 1))) {
                    std::swap(meshNodes->at(i), meshNodes->at(i - 1));
                    changed = true;
                } else if (!nodeInfoLiteIsFavorite(&meshNodes->at(i)) && nodeInfoLiteIsFavorite(&meshNodes->at(i - 1))) {
                    // noop
                } else if (meshNodes->at(i).last_heard > meshNodes->at(i - 1).last_heard) {
                    std::swap(meshNodes->at(i), meshNodes->at(i - 1));
                    changed = true;
                }
            }
        }
        LOG_INFO("Sort took %u milliseconds", millis() - lastSort);
    }
}

uint8_t NodeDB::getMeshNodeChannel(NodeNum n)
{
    const meshtastic_NodeInfoLite *info = getMeshNode(n);
    if (!info) {
        return 0; // defaults to PRIMARY
    }
    return info->channel;
}

std::string NodeDB::getNodeId() const
{
    char nodeId[16];
    snprintf(nodeId, sizeof(nodeId), "!%08x", myNodeInfo.my_node_num);
    return std::string(nodeId);
}

/// Find a node in our DB, return null for missing
/// NOTE: This function might be called from an ISR
meshtastic_NodeInfoLite *NodeDB::getMeshNode(NodeNum n)
{
    for (int i = 0; i < numMeshNodes; i++)
        if (meshNodes->at(i).num == n)
            return &meshNodes->at(i);

    return NULL;
}

ResolvedNode NodeDB::resolveLastByte(uint8_t lastByte, bool requireDirectNeighbor)
{
    ResolvedNode result; // defaults to {None, 0}

    // 0 is the NO_RELAY_NODE / NO_NEXT_HOP_PREFERENCE sentinel (also what MQTT-sourced packets carry
    // when hop_start==0). getLastByteOfNodeNum() never yields 0, so nothing can legitimately match.
    if (lastByte == 0)
        return result;

    const NodeNum self = getNodeNum();
    NodeNum firstMatch = 0;
    uint8_t matches = 0;

    for (size_t i = 0; i < numMeshNodes; i++) {
        const meshtastic_NodeInfoLite *node = &meshNodes->at(i);

        // Candidate gate: never resolve to ourselves, the sentinels, or an ignored node.
        if (node->num == self || node->num == 0 || node->num == NODENUM_BROADCAST)
            continue;
        if (nodeInfoLiteIsIgnored(node))
            continue;
        if (getLastByteOfNodeNum(node->num) != lastByte) // cheapest discriminator last
            continue;

        // Relevance gate: is this node a plausible relay for the requested scope?
        bool relevant;
        if (requireDirectNeighbor) {
            relevant = node->has_hops_away && node->hops_away == 0 && sinceLastSeen(node) < NEXTHOP_NEIGHBOR_FRESH_SECS;
        } else {
            const bool directNeighbor = node->has_hops_away && node->hops_away == 0;
            const bool routerRole =
                IS_ONE_OF(node->role, meshtastic_Config_DeviceConfig_Role_ROUTER, meshtastic_Config_DeviceConfig_Role_ROUTER_LATE,
                          meshtastic_Config_DeviceConfig_Role_CLIENT_BASE);
            relevant = directNeighbor || nodeInfoLiteIsFavorite(node) || routerRole;
        }
        if (!relevant)
            continue;

        if (++matches == 1) {
            firstMatch = node->num;
        } else {
            // A second relevant candidate shares this byte: ambiguous. No further scanning can
            // change that, so stop early and report the collision.
            result.status = LastByteResolution::Ambiguous;
            result.num = 0;
            return result;
        }
    }

    if (matches == 1) {
        result.status = LastByteResolution::Unique;
        result.num = firstMatch;
    }
    return result;
}

bool NodeDB::resolveUniqueLastByte(uint8_t lastByte, bool requireDirectNeighbor, NodeNum *outNum)
{
    ResolvedNode r = resolveLastByte(lastByte, requireDirectNeighbor);
    if (r.status == LastByteResolution::Unique) {
        if (outNum)
            *outNum = r.num;
        return true;
    }
    return false;
}

// returns true if the maximum number of nodes is reached or we are running low on memory
bool NodeDB::isFull()
{
    return (numMeshNodes >= MAX_NUM_NODES) || (memGet.getFreeHeap() < MINIMUM_SAFE_FREE_HEAP);
}

uint32_t NodeDB::hotNodeLastHeard(NodeNum n) const
{
    for (int i = 0; i < numMeshNodes; i++)
        if (meshNodes->at(i).num == n)
            return meshNodes->at(i).last_heard;
    return 0;
}

bool NodeDB::copyPublicKey(NodeNum n, meshtastic_NodeInfoLite_public_key_t &out)
{
    const meshtastic_NodeInfoLite *info = getMeshNode(n);
    if (info && info->public_key.size == 32) {
        out = info->public_key;
        return true;
    }
#if WARM_NODE_COUNT > 0
    if (warmStore.copyKey(n, out.bytes)) {
        out.size = 32;
        return true;
    }
#endif
    return false;
}

meshtastic_Config_DeviceConfig_Role NodeDB::getNodeRole(NodeNum n)
{
    const meshtastic_NodeInfoLite *info = getMeshNode(n);
    if (nodeInfoLiteHasUser(info))
        return info->role;
#if WARM_NODE_COUNT > 0
    // Hot-store miss: fall back to the role the warm tier cached at eviction.
    uint8_t role = 0, prot = 0;
    if (warmStore.lookupMeta(n, role, prot))
        return static_cast<meshtastic_Config_DeviceConfig_Role>(role);
#endif
    return meshtastic_Config_DeviceConfig_Role_CLIENT;
}

/// Find a node in our DB, create an empty NodeInfo if missing
meshtastic_NodeInfoLite *NodeDB::getOrCreateMeshNode(NodeNum n)
{
    meshtastic_NodeInfoLite *lite = getMeshNode(n);

    if (!lite) {
        if (isFull()) {
            LOG_INFO("Node database full with %i nodes and %u bytes free. Erasing oldest entry", numMeshNodes,
                     memGet.getFreeHeap());
            // look for oldest node and erase it
            uint32_t oldest = UINT32_MAX;
            uint32_t oldestBoring = UINT32_MAX;
            int oldestIndex = -1;
            int oldestBoringIndex = -1;
            for (int i = 1; i < numMeshNodes; i++) {
                const meshtastic_NodeInfoLite *cand = &meshNodes->at(i);
                const bool isFavoriteNode = nodeInfoLiteIsFavorite(cand);
                const bool isIgnored = nodeInfoLiteIsIgnored(cand);
                const bool isVerified = nodeInfoLiteIsKeyManuallyVerified(cand);
                // Simply the oldest non-favorite, non-ignored, non-verified node
                if (!isFavoriteNode && !isIgnored && !isVerified && cand->last_heard < oldest) {
                    oldest = cand->last_heard;
                    oldestIndex = i;
                }
                // The oldest "boring" node
                if (!isFavoriteNode && !isIgnored && cand->public_key.size == 0 && cand->last_heard < oldestBoring) {
                    oldestBoring = cand->last_heard;
                    oldestBoringIndex = i;
                }
            }
            // if we found a "boring" node, evict it
            if (oldestBoringIndex != -1) {
                oldestIndex = oldestBoringIndex;
            }

            if (oldestIndex != -1) {
                const meshtastic_NodeInfoLite &evicted = meshNodes->at(oldestIndex);
#if WARM_NODE_COUNT > 0
                // Demote to the warm tier so the identity (and crucially the
                // PKI key) outlives the hot-store slot.
                warmStore.absorb(evicted.num, evicted.last_heard, evicted.public_key.size == 32 ? evicted.public_key.bytes : NULL,
                                 evicted.role, warmProtectedCategory(evicted));
#endif
                eraseNodeSatellites(evicted.num);
                // Shove the remaining nodes down the chain
                for (int i = oldestIndex; i < numMeshNodes - 1; i++) {
                    meshNodes->at(i) = meshNodes->at(i + 1);
                }
                (numMeshNodes)--;
            }
        }
        // Don't append past the end of the vector. The protected-node cap
        // (numProtectedNodes() <= MAX_NUM_NODES-2) means the eviction above frees
        // a slot in normal operation; this guards the legacy case of a pre-cap
        // database that is full of protected nodes - refuse rather than overrun.
        if (numMeshNodes >= MAX_NUM_NODES)
            return NULL;
        // Pre-size before append when run before nodeDBSelfCare() (boot keygen); else at() aborts on nRF52.
        if (static_cast<size_t>(numMeshNodes) >= meshNodes->size())
            meshNodes->resize(numMeshNodes + 1);
        // add the node at the end
        lite = &meshNodes->at((numMeshNodes)++);

        // everything is missing except the nodenum
        memset(lite, 0, sizeof(*lite));
        lite->num = n;
#if WARM_NODE_COUNT > 0
        // Re-admission: restore what the warm tier kept for this node
        WarmNodeEntry warm;
        if (warmStore.take(n, warm)) {
            lite->last_heard = warmTimeOf(warm); // mask off the stolen role/protected metadata bits
            // Restore the role the warm tier cached, so re-admission isn't stuck at CLIENT
            // until the next NodeInfo arrives.
            lite->role = static_cast<meshtastic_Config_DeviceConfig_Role>(warmRoleOf(warm));
            if (!memfll(warm.public_key, 0, sizeof(warm.public_key))) {
                lite->public_key.size = 32;
                memcpy(lite->public_key.bytes, warm.public_key, 32);
            }
            LOG_MIGRATION("Rehydrated node 0x%08x from warm tier (key=%d)", n, lite->public_key.size == 32);
        }
#endif
        LOG_INFO("Adding node to database with %i nodes and %u bytes free!", numMeshNodes, memGet.getFreeHeap());
    }

    return lite;
}

/// Sometimes we will have Position objects that only have a time, so check for
/// valid lat/lon
bool NodeDB::hasValidPosition(const meshtastic_NodeInfoLite *n)
{
    if (!n)
        return false;
    if (n->num == getNodeNum()) {
        return localPosition.latitude_i != 0 || localPosition.longitude_i != 0;
    }
    meshtastic_PositionLite pos;
    return copyNodePosition(n->num, pos) && (pos.latitude_i != 0 || pos.longitude_i != 0);
}

/// If we have a node / user and they report is_licensed = true
/// we consider them licensed
UserLicenseStatus NodeDB::getLicenseStatus(uint32_t nodeNum)
{
    const meshtastic_NodeInfoLite *info = getMeshNode(nodeNum);
    if (!nodeInfoLiteHasUser(info))
        return UserLicenseStatus::NotKnown;
    return nodeInfoLiteIsLicensed(info) ? UserLicenseStatus::Licensed : UserLicenseStatus::NotLicensed;
}

#if !defined(MESHTASTIC_EXCLUDE_PKI)
bool NodeDB::checkLowEntropyPublicKey(const meshtastic_Config_SecurityConfig_public_key_t &keyToTest)
{
    if (keyToTest.size == 32) {
        uint8_t keyHash[32] = {0};
        memcpy(keyHash, keyToTest.bytes, keyToTest.size);
        crypto->hash(keyHash, 32);
        for (uint16_t i = 0; i < sizeof(LOW_ENTROPY_HASHES) / sizeof(LOW_ENTROPY_HASHES[0]); i++) {
            if (memcmp(keyHash, LOW_ENTROPY_HASHES[i], sizeof(LOW_ENTROPY_HASHES[0])) == 0) {
                return true;
            }
        }
    }
    return false;
}
#endif

bool NodeDB::generateCryptoKeyPair(const uint8_t *privateKey)
{
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    // Only generate keys for non-licensed users and if the LoRa region is set. The native simulator
    // boots region-UNSET but still needs a keypair so PKI-encrypted DMs work between sim nodes, so
    // allow keygen there regardless of region.
    bool regionBlocksKeygen = config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET;
#if ARCH_PORTDUINO
    if (portduino_config.lora_module == use_simradio)
        regionBlocksKeygen = false;
#endif
    if (owner.is_licensed || regionBlocksKeygen) {
        return false;
    }

    bool keygenSuccess = false;
    // Record whether the stored key is a known compromised/low-entropy key so main.cpp can warn the
    // user. A detected low-entropy key is regenerated below, but the flag stays set so the
    // "Compromised keys were detected and regenerated" notification still fires.
    keyIsLowEntropy = checkLowEntropyPublicKey(config.security.public_key);

    // If a specific private key was provided, use it
    if (privateKey != nullptr) {
        LOG_INFO("Using provided private key for PKI");
        memcpy(config.security.private_key.bytes, privateKey, 32);
        config.security.private_key.size = 32;
        config.security.public_key.size = 32;

        // Generate public key from the provided private key
        if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
            keygenSuccess = true;
        } else {
            LOG_ERROR("Failed to generate public key from provided private key");
            return false;
        }
    }
    // Try to regenerate public key from existing private key if it's valid and not low entropy
    else if (config.security.private_key.size == 32 && !keyIsLowEntropy) {
        config.security.public_key.size = 32;
        LOG_DEBUG("Regenerate PKI public key from existing private key");
        if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
            keygenSuccess = true;
        }
    } else {
        // Generate a new key pair
        LOG_INFO("Generate new PKI keys");
        config.security.public_key.size = 32;
        config.security.private_key.size = 32;
        crypto->generateKeyPair(config.security.public_key.bytes, config.security.private_key.bytes);
        keygenSuccess = true;
    }

    // Update sizes and copy to owner if successful
    if (keygenSuccess) {
        owner.public_key.size = 32;
        memcpy(owner.public_key.bytes, config.security.public_key.bytes, 32);

        // Set the DH private key for crypto operations
        LOG_DEBUG("Set DH private key for crypto operations");
        crypto->setDHPrivateKey(config.security.private_key.bytes);

        // Conditionally create new identity based on parameter
        createNewIdentity();
    }
    return keygenSuccess;
#else
    return false;
#endif
}

bool NodeDB::createNewIdentity()
{
    uint32_t oldNodeNum = getNodeNum();
    uint32_t newNodeNum = crc32Buffer(config.security.public_key.bytes, config.security.public_key.size);

    // If the key hasn't changed, nothing to do
    if (newNodeNum == oldNodeNum)
        return false;

    // Remove the old node entry entirely rather than retiring it in place. Flagging the old identity as
    // ignored (the previous behavior) left a keyless ghost of ourselves that survived cleanup/eviction, was
    // still streamed to clients, and made any DM/admin aimed at it fail forever with PKI_SEND_FAIL_PUBLIC_KEY.
    // removeNodeByNum() drops the lite entry, its satellite stores, and the warm-tier copy.
    if (getMeshNode(oldNodeNum) != NULL) {
        LOG_DEBUG("Old node num %u is now %u, removing stale identity", oldNodeNum, newNodeNum);
        removeNodeByNum(oldNodeNum);
    } else {
        // Lite entry already absent: drop any orphaned satellite-store entries directly.
        eraseNodeSatellites(oldNodeNum);
    }

    myNodeInfo.my_node_num = newNodeNum;

    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(getNodeNum());
    if (!info)
        return false;
    TypeConversions::CopyUserToNodeInfoLite(info, owner);

    return true;
}

bool NodeDB::backupPreferences(meshtastic_AdminMessage_BackupLocation location)
{
    bool success = false;
    lastBackupAttempt = millis();
#ifdef FSCom
    if (location == meshtastic_AdminMessage_BackupLocation_FLASH) {
        meshtastic_BackupPreferences backup = meshtastic_BackupPreferences_init_zero;
        backup.version = DEVICESTATE_CUR_VER;
        backup.timestamp = getValidTime(RTCQuality::RTCQualityDevice, false);
        backup.has_config = true;
        backup.config = config;
        backup.has_module_config = true;
        backup.module_config = moduleConfig;
        backup.has_channels = true;
        backup.channels = channelFile;
        backup.has_owner = true;
        backup.owner = owner;

        size_t backupSize;
        pb_get_encoded_size(&backupSize, meshtastic_BackupPreferences_fields, &backup);

        spiLock->lock();
        FSCom.mkdir("/backups");
        spiLock->unlock();
        success = saveProto(backupFileName, backupSize, &meshtastic_BackupPreferences_msg, &backup);

        if (success) {
            LOG_INFO("Saved backup preferences");
        } else {
            LOG_ERROR("Failed to save backup preferences to file");
        }
    } else if (location == meshtastic_AdminMessage_BackupLocation_SD) {
        // TODO: After more mainline SD card support
    }
#endif
    return success;
}

bool NodeDB::restorePreferences(meshtastic_AdminMessage_BackupLocation location, int restoreWhat)
{
    bool success = false;
#ifdef FSCom
    if (location == meshtastic_AdminMessage_BackupLocation_FLASH) {
        spiLock->lock();
        if (!FSCom.exists(backupFileName)) {
            spiLock->unlock();
            LOG_WARN("Could not restore. No backup file found");
            return false;
        } else {
            spiLock->unlock();
        }
        meshtastic_BackupPreferences backup = meshtastic_BackupPreferences_init_zero;
        success = loadProto(backupFileName, meshtastic_BackupPreferences_size, sizeof(meshtastic_BackupPreferences),
                            &meshtastic_BackupPreferences_msg, &backup);
        if (success) {
            if (restoreWhat & SEGMENT_CONFIG) {
                config = backup.config;
                LOG_DEBUG("Restored config");
            }
            if (restoreWhat & SEGMENT_MODULECONFIG) {
                moduleConfig = backup.module_config;
                LOG_DEBUG("Restored module config");
            }
            if (restoreWhat & SEGMENT_DEVICESTATE) {
                devicestate.owner = backup.owner;
                LOG_DEBUG("Restored device state");
            }
            if (restoreWhat & SEGMENT_CHANNELS) {
                channelFile = backup.channels;
                LOG_DEBUG("Restored channels");
            }

            success = saveToDisk(restoreWhat);
            if (success) {
                LOG_INFO("Restored preferences from backup");
            } else {
                LOG_ERROR("Failed to save restored preferences to flash");
            }
        } else {
            LOG_ERROR("Failed to restore preferences from backup file");
        }
    } else if (location == meshtastic_AdminMessage_BackupLocation_SD) {
        // TODO: After more mainline SD card support
    }
#endif
    return success;
}

/// Record an error that should be reported via analytics
void recordCriticalError(meshtastic_CriticalErrorCode code, uint32_t address, const char *filename)
{
    if (filename) {
        LOG_ERROR("NOTE! Record critical error %d at %s:%lu", code, filename, address);
    } else {
        LOG_ERROR("NOTE! Record critical error %d, address=0x%lx", code, address);
    }

    // Record error to DB
    error_code = code;
    error_address = address;

    // Currently portuino is mostly used for simulation.  Make sure the user notices something really bad happened
#ifdef ARCH_PORTDUINO
    LOG_ERROR("A critical failure occurred");
    // TODO: Determine if other critical errors should also cause an immediate exit
    if (code == meshtastic_CriticalErrorCode_FLASH_CORRUPTION_RECOVERABLE ||
        code == meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE)
        exit(2);
#endif
}
