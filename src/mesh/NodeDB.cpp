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
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "TypeConversions.h"
#include "error.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "modules/NeighborInfoModule.h"
#include <ErriezCRC32.h>
#include <algorithm>
#include <iostream>
#include <pb_decode.h>
#include <pb_encode.h>
#include <vector>

#ifdef ARCH_ESP32
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif
#include "modules/esp32/StoreForwardModule.h"
#include <Preferences.h>
#include <nvs_flash.h>
#endif

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

#ifdef ARCH_NRF52
#include <bluefruit.h>
#include <utility/bonding.h>
#endif

NodeDB *nodeDB = nullptr;

// we have plenty of ram so statically alloc this tempbuf (for now)
EXT_RAM_ATTR meshtastic_DeviceState devicestate;
meshtastic_MyNodeInfo &myNodeInfo = devicestate.my_node;
meshtastic_LocalConfig config;
meshtastic_LocalModuleConfig moduleConfig;
meshtastic_ChannelFile channelFile;
meshtastic_OEMStore oemStore;

bool meshtastic_DeviceState_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_iter_t *field)
{
    if (ostream) {
        std::vector<meshtastic_NodeInfoLite> const *vec = (std::vector<meshtastic_NodeInfoLite> *)field->pData;
        for (auto item : *vec) {
            if (!pb_encode_tag_for_field(ostream, field))
                return false;
            pb_encode_submessage(ostream, meshtastic_NodeInfoLite_fields, &item);
        }
    }
    if (istream) {
        meshtastic_NodeInfoLite node; // this gets good data
        std::vector<meshtastic_NodeInfoLite> *vec = (std::vector<meshtastic_NodeInfoLite> *)field->pData;

        if (istream->bytes_left && pb_decode(istream, meshtastic_NodeInfoLite_fields, &node))
            vec->push_back(node);
    }
    return true;
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
meshtastic_Position localPosition = meshtastic_Position_init_default;
meshtastic_CriticalErrorCode error_code =
    meshtastic_CriticalErrorCode_NONE; // For the error code, only show values from this boot (discard value from flash)
uint32_t error_address = 0;

static uint8_t ourMacAddr[6];

NodeDB::NodeDB()
{
    LOG_INFO("Initializing NodeDB\n");
    loadFromDisk();
    cleanupMeshDB();

    uint32_t devicestateCRC = crc32Buffer(&devicestate, sizeof(devicestate));
    uint32_t configCRC = crc32Buffer(&config, sizeof(config));
    uint32_t channelFileCRC = crc32Buffer(&channelFile, sizeof(channelFile));

    int saveWhat = 0;

    // likewise - we always want the app requirements to come from the running appload
    myNodeInfo.min_app_version = 30200; // format is Mmmss (where M is 1+the numeric major number. i.e. 30200 means 2.2.00
    // Note! We do this after loading saved settings, so that if somehow an invalid nodenum was stored in preferences we won't
    // keep using that nodenum forever. Crummy guess at our nodenum (but we will check against the nodedb to avoid conflicts)
    pickNewNodeNum();

    // Set our board type so we can share it with others
    owner.hw_model = HW_VENDOR;
    // Ensure user (nodeinfo) role is set to whatever we're configured to
    owner.role = config.device.role;

    // Include our owner in the node db under our nodenum
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(getNodeNum());
    info->user = owner;
    info->has_user = true;

#ifdef ARCH_ESP32
    Preferences preferences;
    preferences.begin("meshtastic", false);
    myNodeInfo.reboot_count = preferences.getUInt("rebootCounter", 0);
    preferences.end();
    LOG_DEBUG("Number of Device Reboots: %d\n", myNodeInfo.reboot_count);
#endif

    resetRadioConfig(); // If bogus settings got saved, then fix them
    // nodeDB->LOG_DEBUG("region=%d, NODENUM=0x%x, dbsize=%d\n", config.lora.region, myNodeInfo.my_node_num, numMeshNodes);

    if (devicestateCRC != crc32Buffer(&devicestate, sizeof(devicestate)))
        saveWhat |= SEGMENT_DEVICESTATE;
    if (configCRC != crc32Buffer(&config, sizeof(config)))
        saveWhat |= SEGMENT_CONFIG;
    if (channelFileCRC != crc32Buffer(&channelFile, sizeof(channelFile)))
        saveWhat |= SEGMENT_CHANNELS;

    if (config.position.gps_enabled) {
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        config.position.gps_enabled = 0;
    }
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

bool NodeDB::resetRadioConfig(bool factory_reset)
{
    bool didFactoryReset = false;

    radioGeneration++;

    if (factory_reset) {
        didFactoryReset = factoryReset();
    }

    if (channelFile.channels_count != MAX_NUM_CHANNELS) {
        LOG_INFO("Setting default channel and radio preferences!\n");

        channels.initDefaults();
    }

    channels.onConfigChanged();

    // Update the global myRegion
    initRegion();

    if (didFactoryReset) {
        LOG_INFO("Rebooting due to factory reset");
        screen->startAlert("Rebooting...");
        rebootAtMsec = millis() + (5 * 1000);
    }

    return didFactoryReset;
}

bool NodeDB::factoryReset()
{
    LOG_INFO("Performing factory reset!\n");
    // first, remove the "/prefs" (this removes most prefs)
    rmDir("/prefs");
    if (FSCom.exists("/static/rangetest.csv") && !FSCom.remove("/static/rangetest.csv")) {
        LOG_ERROR("Could not remove rangetest.csv file\n");
    }
    // second, install default state (this will deal with the duplicate mac address issue)
    installDefaultDeviceState();
    installDefaultConfig();
    installDefaultModuleConfig();
    installDefaultChannels();
    // third, write everything to disk
    saveToDisk();
#ifdef ARCH_ESP32
    // This will erase what's in NVS including ssl keys, persistent variables and ble pairing
    nvs_flash_erase();
#endif
#ifdef ARCH_NRF52
    Bluefruit.begin();
    LOG_INFO("Clearing bluetooth bonds!\n");
    bond_print_list(BLE_GAP_ROLE_PERIPH);
    bond_print_list(BLE_GAP_ROLE_CENTRAL);
    Bluefruit.Periph.clearBonds();
    Bluefruit.Central.clearBonds();
#endif
    return true;
}

void NodeDB::installDefaultConfig()
{
    LOG_INFO("Installing default LocalConfig\n");
    memset(&config, 0, sizeof(meshtastic_LocalConfig));
    config.version = DEVICESTATE_CUR_VER;
    config.has_device = true;
    config.has_display = true;
    config.has_lora = true;
    config.has_position = true;
    config.has_power = true;
    config.has_network = true;
    config.has_bluetooth = (HAS_BLUETOOTH ? true : false);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;

    config.lora.sx126x_rx_boosted_gain = true;
    config.lora.tx_enabled =
        true; // FIXME: maybe false in the future, and setting region to enable it. (unset region forces it off)
    config.lora.override_duty_cycle = false;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.hop_limit = HOP_RELIABLE;
    config.lora.ignore_mqtt = false;
#ifdef PIN_GPS_EN
    config.position.gps_en_gpio = PIN_GPS_EN;
#endif
#ifdef GPS_POWER_TOGGLE
    config.device.disable_triple_click = false;
#else
    config.device.disable_triple_click = true;
#endif
#if !HAS_GPS || defined(T_DECK)
    config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
#elif !defined(GPS_RX_PIN)
    if (config.position.rx_gpio == 0)
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
    else
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
#else
    config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
#endif
    config.position.position_broadcast_smart_enabled = true;
    config.position.broadcast_smart_minimum_distance = 100;
    config.position.broadcast_smart_minimum_interval_secs = 30;
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER)
        config.device.node_info_broadcast_secs = default_node_info_broadcast_secs;
    config.device.serial_enabled = true;
    resetRadioConfig();
    strncpy(config.network.ntp_server, "meshtastic.pool.ntp.org", 32);
    // FIXME: Default to bluetooth capability of platform as default
    config.bluetooth.enabled = true;
    config.bluetooth.fixed_pin = defaultBLEPin;
#if defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7789_CS) || defined(HX8357_CS) ||            \
    defined(USE_ST7789)
    bool hasScreen = true;
#elif ARCH_PORTDUINO
    bool hasScreen = false;
    if (settingsMap[displayPanel])
        hasScreen = true;
    else
        hasScreen = screen_found.port != ScanI2C::I2CPort::NO_I2C;
#else
    bool hasScreen = screen_found.port != ScanI2C::I2CPort::NO_I2C;
#endif
    config.bluetooth.mode = hasScreen ? meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN
                                      : meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    // for backward compat, default position flags are ALT+MSL
    config.position.position_flags =
        (meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL |
         meshtastic_Config_PositionConfig_PositionFlags_SPEED | meshtastic_Config_PositionConfig_PositionFlags_HEADING |
         meshtastic_Config_PositionConfig_PositionFlags_DOP | meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW);

#ifdef RADIOMASTER_900_BANDIT_NANO
    config.display.flip_screen = true;
#endif
#ifdef T_WATCH_S3
    config.display.screen_on_secs = 30;
    config.display.wake_on_tap_or_motion = true;
#endif

    initConfigIntervals();
}

void NodeDB::initConfigIntervals()
{
    config.position.gps_update_interval = default_gps_update_interval;
    config.position.position_broadcast_secs = default_broadcast_interval_secs;

    config.power.ls_secs = default_ls_secs;
    config.power.min_wake_secs = default_min_wake_secs;
    config.power.sds_secs = default_sds_secs;
    config.power.wait_bluetooth_secs = default_wait_bluetooth_secs;

    config.display.screen_on_secs = default_screen_on_secs;

#if defined(T_WATCH_S3) || defined(T_DECK)
    config.power.is_power_saving = true;
    config.display.screen_on_secs = 30;
    config.power.wait_bluetooth_secs = 30;
#endif
}

void NodeDB::installDefaultModuleConfig()
{
    LOG_INFO("Installing default ModuleConfig\n");
    memset(&moduleConfig, 0, sizeof(meshtastic_ModuleConfig));

    moduleConfig.version = DEVICESTATE_CUR_VER;
    moduleConfig.has_mqtt = true;
    moduleConfig.has_range_test = true;
    moduleConfig.has_serial = true;
    moduleConfig.has_store_forward = true;
    moduleConfig.has_telemetry = true;
    moduleConfig.has_external_notification = true;
#if defined(RAK4630) || defined(RAK11310)
    // Default to RAK led pin 2 (blue)
    moduleConfig.external_notification.enabled = true;
    moduleConfig.external_notification.output = PIN_LED2;
    moduleConfig.external_notification.active = true;
    moduleConfig.external_notification.alert_message = true;
    moduleConfig.external_notification.output_ms = 1000;
    moduleConfig.external_notification.nag_timeout = 60;
#endif
#ifdef HAS_I2S
    // Don't worry about the other settings for T-Watch, we'll also use the DRV2056 behavior for notifications
    moduleConfig.external_notification.enabled = true;
    moduleConfig.external_notification.use_i2s_as_buzzer = true;
    moduleConfig.external_notification.alert_message_buzzer = true;
    moduleConfig.external_notification.nag_timeout = 60;
#endif
#ifdef NANO_G2_ULTRA
    moduleConfig.external_notification.enabled = true;
    moduleConfig.external_notification.alert_message = true;
    moduleConfig.external_notification.output_ms = 100;
    moduleConfig.external_notification.active = true;
#endif
    moduleConfig.has_canned_message = true;

    strncpy(moduleConfig.mqtt.address, default_mqtt_address, sizeof(moduleConfig.mqtt.address));
    strncpy(moduleConfig.mqtt.username, default_mqtt_username, sizeof(moduleConfig.mqtt.username));
    strncpy(moduleConfig.mqtt.password, default_mqtt_password, sizeof(moduleConfig.mqtt.password));
    strncpy(moduleConfig.mqtt.root, default_mqtt_root, sizeof(moduleConfig.mqtt.root));
    moduleConfig.mqtt.encryption_enabled = true;

    moduleConfig.has_neighbor_info = true;
    moduleConfig.neighbor_info.enabled = false;

    moduleConfig.has_detection_sensor = true;
    moduleConfig.detection_sensor.enabled = false;
    moduleConfig.detection_sensor.detection_triggered_high = true;
    moduleConfig.detection_sensor.minimum_broadcast_secs = 45;

    moduleConfig.has_ambient_lighting = true;
    moduleConfig.ambient_lighting.current = 10;
    // Default to a color based on our node number
    moduleConfig.ambient_lighting.red = (myNodeInfo.my_node_num & 0xFF0000) >> 16;
    moduleConfig.ambient_lighting.green = (myNodeInfo.my_node_num & 0x00FF00) >> 8;
    moduleConfig.ambient_lighting.blue = myNodeInfo.my_node_num & 0x0000FF;

    initModuleConfigIntervals();
}

void NodeDB::installRoleDefaults(meshtastic_Config_DeviceConfig_Role role)
{
    if (role == meshtastic_Config_DeviceConfig_Role_ROUTER) {
        initConfigIntervals();
        initModuleConfigIntervals();
    } else if (role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        config.display.screen_on_secs = 1;
    } else if (role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
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
    } else if (role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) {
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
        config.device.node_info_broadcast_secs = UINT32_MAX;
        config.position.position_broadcast_smart_enabled = false;
        config.position.position_broadcast_secs = UINT32_MAX;
        moduleConfig.neighbor_info.update_interval = UINT32_MAX;
        moduleConfig.telemetry.device_update_interval = UINT32_MAX;
        moduleConfig.telemetry.environment_update_interval = UINT32_MAX;
        moduleConfig.telemetry.air_quality_interval = UINT32_MAX;
    }
}

void NodeDB::initModuleConfigIntervals()
{
    // Zero out telemetry intervals so that they coalesce to defaults in Default.h
    moduleConfig.telemetry.device_update_interval = 0;
    moduleConfig.telemetry.environment_update_interval = 0;
    moduleConfig.telemetry.air_quality_interval = 0;
    moduleConfig.telemetry.power_update_interval = 0;
    moduleConfig.neighbor_info.update_interval = 0;
    moduleConfig.paxcounter.paxcounter_update_interval = 0;
    moduleConfig.neighbor_info.update_interval = 0;
}

void NodeDB::installDefaultChannels()
{
    LOG_INFO("Installing default ChannelFile\n");
    memset(&channelFile, 0, sizeof(meshtastic_ChannelFile));
    channelFile.version = DEVICESTATE_CUR_VER;
}

void NodeDB::resetNodes()
{
    clearLocalPosition();
    numMeshNodes = 1;
    std::fill(devicestate.node_db_lite.begin() + 1, devicestate.node_db_lite.end(), meshtastic_NodeInfoLite());
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
    std::fill(devicestate.node_db_lite.begin() + numMeshNodes, devicestate.node_db_lite.begin() + numMeshNodes + 1,
              meshtastic_NodeInfoLite());
    LOG_DEBUG("NodeDB::removeNodeByNum purged %d entries. Saving changes...\n", removed);
    saveDeviceStateToDisk();
}

void NodeDB::clearLocalPosition()
{
    meshtastic_NodeInfoLite *node = getMeshNode(nodeDB->getNodeNum());
    node->position.latitude_i = 0;
    node->position.longitude_i = 0;
    node->position.altitude = 0;
    node->position.time = 0;
    setLocalPosition(meshtastic_Position_init_default);
}

void NodeDB::cleanupMeshDB()
{
    int newPos = 0, removed = 0;
    for (int i = 0; i < numMeshNodes; i++) {
        if (meshNodes->at(i).has_user)
            meshNodes->at(newPos++) = meshNodes->at(i);
        else
            removed++;
    }
    numMeshNodes -= removed;
    std::fill(devicestate.node_db_lite.begin() + numMeshNodes, devicestate.node_db_lite.begin() + numMeshNodes + removed,
              meshtastic_NodeInfoLite());
    LOG_DEBUG("cleanupMeshDB purged %d entries\n", removed);
}

void NodeDB::installDefaultDeviceState()
{
    LOG_INFO("Installing default DeviceState\n");
    // memset(&devicestate, 0, sizeof(meshtastic_DeviceState));

    numMeshNodes = 0;
    meshNodes = &devicestate.node_db_lite;

    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_owner = true;
    // devicestate.node_db_lite_count = 0;
    devicestate.version = DEVICESTATE_CUR_VER;
    devicestate.receive_queue_count = 0; // Not yet implemented FIXME

    generatePacketId(); // FIXME - ugly way to init current_packet_id;

    // Set default owner name
    pickNewNodeNum(); // based on macaddr now
    snprintf(owner.long_name, sizeof(owner.long_name), "Meshtastic %02x%02x", ourMacAddr[4], ourMacAddr[5]);
    snprintf(owner.short_name, sizeof(owner.short_name), "%02x%02x", ourMacAddr[4], ourMacAddr[5]);
    snprintf(owner.id, sizeof(owner.id), "!%08x", getNodeNum()); // Default node ID now based on nodenum
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));
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

    meshtastic_NodeInfoLite *found;
    while ((nodeNum == NODENUM_BROADCAST || nodeNum < NUM_RESERVED) ||
           ((found = getMeshNode(nodeNum)) && memcmp(found->user.macaddr, owner.macaddr, sizeof(owner.macaddr)) != 0)) {
        NodeNum candidate = random(NUM_RESERVED, LONG_MAX); // try a new random choice
        LOG_WARN("NOTE! Our desired nodenum 0x%x is invalid or in use, so trying for 0x%x\n", nodeNum, candidate);
        nodeNum = candidate;
    }
    LOG_DEBUG("Using nodenum 0x%x \n", nodeNum);

    myNodeInfo.my_node_num = nodeNum;
}

static const char *prefFileName = "/prefs/db.proto";
static const char *configFileName = "/prefs/config.proto";
static const char *moduleConfigFileName = "/prefs/module.proto";
static const char *channelFileName = "/prefs/channels.proto";
static const char *oemConfigFile = "/oem/oem.proto";

/** Load a protobuf from a file, return LoadFileResult */
LoadFileResult NodeDB::loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields,
                                 void *dest_struct)
{
    LoadFileResult state = LoadFileResult::OTHER_FAILURE;
#ifdef FSCom

    if (!FSCom.exists(filename)) {
        LOG_INFO("File %s not found\n", filename);
        return LoadFileResult::NOT_FOUND;
    }

    auto f = FSCom.open(filename, FILE_O_READ);

    if (f) {
        LOG_INFO("Loading %s\n", filename);
        pb_istream_t stream = {&readcb, &f, protoSize};

        memset(dest_struct, 0, objSize);
        if (!pb_decode(&stream, fields, dest_struct)) {
            LOG_ERROR("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
            state = LoadFileResult::DECODE_FAILED;
        } else {
            LOG_INFO("Loaded %s successfully\n", filename);
            state = LoadFileResult::SUCCESS;
        }
        f.close();
    } else {
        LOG_ERROR("Could not open / read %s\n", filename);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
    state = LoadFileState::NO_FILESYSTEM;
#endif
    return state;
}

void NodeDB::loadFromDisk()
{
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    auto state = loadProto(prefFileName, sizeof(meshtastic_DeviceState) + MAX_NUM_NODES * sizeof(meshtastic_NodeInfo),
                           sizeof(meshtastic_DeviceState), &meshtastic_DeviceState_msg, &devicestate);

    if (state != LoadFileResult::SUCCESS) {
        installDefaultDeviceState(); // Our in RAM copy might now be corrupt
    } else {
        if (devicestate.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("Devicestate %d is old, discarding\n", devicestate.version);
            factoryReset();
        } else {
            LOG_INFO("Loaded saved devicestate version %d, with nodecount: %d\n", devicestate.version,
                     devicestate.node_db_lite.size());
            meshNodes = &devicestate.node_db_lite;
            numMeshNodes = devicestate.node_db_lite.size();
        }
    }
    meshNodes->resize(MAX_NUM_NODES);

    state = loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(meshtastic_LocalConfig), &meshtastic_LocalConfig_msg,
                      &config);
    if (state != LoadFileResult::SUCCESS) {
        installDefaultConfig(); // Our in RAM copy might now be corrupt
    } else {
        if (config.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("config %d is old, discarding\n", config.version);
            installDefaultConfig();
        } else {
            LOG_INFO("Loaded saved config version %d\n", config.version);
        }
    }

    state = loadProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, sizeof(meshtastic_LocalModuleConfig),
                      &meshtastic_LocalModuleConfig_msg, &moduleConfig);
    if (state != LoadFileResult::SUCCESS) {
        installDefaultModuleConfig(); // Our in RAM copy might now be corrupt
    } else {
        if (moduleConfig.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("moduleConfig %d is old, discarding\n", moduleConfig.version);
            installDefaultModuleConfig();
        } else {
            LOG_INFO("Loaded saved moduleConfig version %d\n", moduleConfig.version);
        }
    }

    state = loadProto(channelFileName, meshtastic_ChannelFile_size, sizeof(meshtastic_ChannelFile), &meshtastic_ChannelFile_msg,
                      &channelFile);
    if (state != LoadFileResult::SUCCESS) {
        installDefaultChannels(); // Our in RAM copy might now be corrupt
    } else {
        if (channelFile.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("channelFile %d is old, discarding\n", channelFile.version);
            installDefaultChannels();
        } else {
            LOG_INFO("Loaded saved channelFile version %d\n", channelFile.version);
        }
    }

    state = loadProto(oemConfigFile, meshtastic_OEMStore_size, sizeof(meshtastic_OEMStore), &meshtastic_OEMStore_msg, &oemStore);
    if (state == LoadFileResult::SUCCESS) {
        LOG_INFO("Loaded OEMStore\n");
    }

    // 2.4.X - configuration migration to update new default intervals
    if (moduleConfig.version < 23) {
        LOG_DEBUG("ModuleConfig version %d is stale, upgrading to new default intervals\n", moduleConfig.version);
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
}

/** Save a protobuf from a file, return true for success */
bool NodeDB::saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct)
{
    bool okay = false;
#ifdef FSCom
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    String filenameTmp = filename;
    filenameTmp += ".tmp";
    auto f = FSCom.open(filenameTmp.c_str(), FILE_O_WRITE);
    if (f) {
        LOG_INFO("Saving %s\n", filename);
        pb_ostream_t stream = {&writecb, &f, protoSize};

        if (!pb_encode(&stream, fields, dest_struct)) {
            LOG_ERROR("Error: can't encode protobuf %s\n", PB_GET_ERROR(&stream));
        } else {
            okay = true;
        }
        f.flush();
        f.close();

        // brief window of risk here ;-)
        if (FSCom.exists(filename) && !FSCom.remove(filename)) {
            LOG_WARN("Can't remove old pref file\n");
        }
        if (!renameFile(filenameTmp.c_str(), filename)) {
            LOG_ERROR("Error: can't rename new pref file\n");
        }
    } else {
        LOG_ERROR("Can't write prefs\n");
#ifdef ARCH_NRF52
        static uint8_t failedCounter = 0;
        failedCounter++;
        if (failedCounter >= 2) {
            LOG_ERROR("Failed to save file twice. Rebooting...\n");
            delay(100);
            NVIC_SystemReset();
            // We used to blow away the filesystem here, but that's a bit extreme
            // FSCom.format();
            // // After formatting, the device needs to be restarted
            // nodeDB->resetRadioConfig(true);
        }
#endif
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
    return okay;
}

void NodeDB::saveChannelsToDisk()
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
#endif
    saveProto(channelFileName, meshtastic_ChannelFile_size, &meshtastic_ChannelFile_msg, &channelFile);
}

void NodeDB::saveDeviceStateToDisk()
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
#endif
    saveProto(prefFileName, sizeof(devicestate) + numMeshNodes * meshtastic_NodeInfoLite_size, &meshtastic_DeviceState_msg,
              &devicestate);
}

void NodeDB::saveToDisk(int saveWhat)
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
#endif
    if (saveWhat & SEGMENT_DEVICESTATE) {
        saveDeviceStateToDisk();
    }

    if (saveWhat & SEGMENT_CONFIG) {
        config.has_device = true;
        config.has_display = true;
        config.has_lora = true;
        config.has_position = true;
        config.has_power = true;
        config.has_network = true;
        config.has_bluetooth = true;

        saveProto(configFileName, meshtastic_LocalConfig_size, &meshtastic_LocalConfig_msg, &config);
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

        saveProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, &meshtastic_LocalModuleConfig_msg, &moduleConfig);
    }

    if (saveWhat & SEGMENT_CHANNELS) {
        saveChannelsToDisk();
    }
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

#define NUM_ONLINE_SECS (60 * 60 * 2) // 2 hrs to consider someone offline

size_t NodeDB::getNumOnlineMeshNodes(bool localOnly)
{
    size_t numseen = 0;

    // FIXME this implementation is kinda expensive
    for (int i = 0; i < numMeshNodes; i++) {
        if (localOnly && meshNodes->at(i).via_mqtt)
            continue;
        if (sinceLastSeen(&meshNodes->at(i)) < NUM_ONLINE_SECS)
            numseen++;
    }

    return numseen;
}

#include "MeshModule.h"
#include "Throttle.h"

/** Update position info for this node based on received position data
 */
void NodeDB::updatePosition(uint32_t nodeId, const meshtastic_Position &p, RxSource src)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    if (!info) {
        return;
    }

    if (src == RX_SRC_LOCAL) {
        // Local packet, fully authoritative
        LOG_INFO("updatePosition LOCAL pos@%x time=%u lat=%d lon=%d alt=%d\n", p.timestamp, p.time, p.latitude_i, p.longitude_i,
                 p.altitude);

        setLocalPosition(p);
        info->position = TypeConversions::ConvertToPositionLite(p);
    } else if ((p.time > 0) && !p.latitude_i && !p.longitude_i && !p.timestamp && !p.location_source) {
        // FIXME SPECIAL TIME SETTING PACKET FROM EUD TO RADIO
        // (stop-gap fix for issue #900)
        LOG_DEBUG("updatePosition SPECIAL time setting time=%u\n", p.time);
        info->position.time = p.time;
    } else {
        // Be careful to only update fields that have been set by the REMOTE sender
        // A lot of position reports don't have time populated.  In that case, be careful to not blow away the time we
        // recorded based on the packet rxTime
        //
        // FIXME perhaps handle RX_SRC_USER separately?
        LOG_INFO("updatePosition REMOTE node=0x%x time=%u lat=%d lon=%d\n", nodeId, p.time, p.latitude_i, p.longitude_i);

        // First, back up fields that we want to protect from overwrite
        uint32_t tmp_time = info->position.time;

        // Next, update atomically
        info->position = TypeConversions::ConvertToPositionLite(p);

        // Last, restore any fields that may have been overwritten
        if (!info->position.time)
            info->position.time = tmp_time;
    }
    info->has_position = true;
    updateGUIforNode = info;
    notifyObservers(true); // Force an update whether or not our node counts have changed
}

/** Update telemetry info for this node based on received metrics
 *  We only care about device telemetry here
 */
void NodeDB::updateTelemetry(uint32_t nodeId, const meshtastic_Telemetry &t, RxSource src)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    // Environment metrics should never go to NodeDb but we'll safegaurd anyway
    if (!info || t.which_variant != meshtastic_Telemetry_device_metrics_tag) {
        return;
    }

    if (src == RX_SRC_LOCAL) {
        // Local packet, fully authoritative
        LOG_DEBUG("updateTelemetry LOCAL\n");
    } else {
        LOG_DEBUG("updateTelemetry REMOTE node=0x%x \n", nodeId);
    }
    info->device_metrics = t.variant.device_metrics;
    info->has_device_metrics = true;
    updateGUIforNode = info;
    notifyObservers(true); // Force an update whether or not our node counts have changed
}

/** Update user info and channel for this node based on received user data
 */
bool NodeDB::updateUser(uint32_t nodeId, const meshtastic_User &p, uint8_t channelIndex)
{
    meshtastic_NodeInfoLite *info = getOrCreateMeshNode(nodeId);
    if (!info) {
        return false;
    }

    LOG_DEBUG("old user %s/%s/%s, channel=%d\n", info->user.id, info->user.long_name, info->user.short_name, info->channel);

    // Both of info->user and p start as filled with zero so I think this is okay
    bool changed = memcmp(&info->user, &p, sizeof(info->user)) || (info->channel != channelIndex);

    info->user = p;
    if (nodeId != getNodeNum())
        info->channel = channelIndex; // Set channel we need to use to reach this node (but don't set our own channel)
    LOG_DEBUG("updating changed=%d user %s/%s/%s, channel=%d\n", changed, info->user.id, info->user.long_name,
              info->user.short_name, info->channel);
    info->has_user = true;

    if (changed) {
        updateGUIforNode = info;
        powerFSM.trigger(EVENT_NODEDB_UPDATED);
        notifyObservers(true); // Force an update whether or not our node counts have changed

        // We just changed something about the user, store our DB
        Throttle::execute(
            &lastNodeDbSave, ONE_MINUTE_MS, []() { nodeDB->saveToDisk(SEGMENT_DEVICESTATE); },
            []() { LOG_DEBUG("Deferring NodeDB saveToDisk for now, since we saved less than a minute ago\n"); });
    }

    return changed;
}

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.from) {
        LOG_DEBUG("Update DB node 0x%x, rx_time=%u\n", mp.from, mp.rx_time);

        meshtastic_NodeInfoLite *info = getOrCreateMeshNode(getFrom(&mp));
        if (!info) {
            return;
        }

        if (mp.rx_time) // if the packet has a valid timestamp use it to update our last_heard
            info->last_heard = mp.rx_time;

        if (mp.rx_snr)
            info->snr = mp.rx_snr; // keep the most recent SNR we received for this node.

        info->via_mqtt = mp.via_mqtt; // Store if we received this packet via MQTT

        // If hopStart was set and there wasn't someone messing with the limit in the middle, add hopsAway
        if (mp.hop_start != 0 && mp.hop_limit <= mp.hop_start)
            info->hops_away = mp.hop_start - mp.hop_limit;
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

/// Find a node in our DB, return null for missing
/// NOTE: This function might be called from an ISR
meshtastic_NodeInfoLite *NodeDB::getMeshNode(NodeNum n)
{
    for (int i = 0; i < numMeshNodes; i++)
        if (meshNodes->at(i).num == n)
            return &meshNodes->at(i);

    return NULL;
}

/// Find a node in our DB, create an empty NodeInfo if missing
meshtastic_NodeInfoLite *NodeDB::getOrCreateMeshNode(NodeNum n)
{
    meshtastic_NodeInfoLite *lite = getMeshNode(n);

    if (!lite) {
        if ((numMeshNodes >= MAX_NUM_NODES) || (memGet.getFreeHeap() < meshtastic_NodeInfoLite_size * 3)) {
            if (screen)
                screen->print("Warn: node database full!\nErasing oldest entry\n");
            LOG_WARN("Node database full! Erasing oldest entry\n");
            // look for oldest node and erase it
            uint32_t oldest = UINT32_MAX;
            int oldestIndex = -1;
            for (int i = 1; i < numMeshNodes; i++) {
                if (!meshNodes->at(i).is_favorite && meshNodes->at(i).last_heard < oldest) {
                    oldest = meshNodes->at(i).last_heard;
                    oldestIndex = i;
                }
            }
            // Shove the remaining nodes down the chain
            for (int i = oldestIndex; i < numMeshNodes - 1; i++) {
                meshNodes->at(i) = meshNodes->at(i + 1);
            }
            (numMeshNodes)--;
        }
        // add the node at the end
        lite = &meshNodes->at((numMeshNodes)++);

        // everything is missing except the nodenum
        memset(lite, 0, sizeof(*lite));
        lite->num = n;
    }

    return lite;
}

/// Record an error that should be reported via analytics
void recordCriticalError(meshtastic_CriticalErrorCode code, uint32_t address, const char *filename)
{
    // Print error to screen and serial port
    String lcd = String("Critical error ") + code + "!\n";
    screen->print(lcd.c_str());
    if (filename) {
        LOG_ERROR("NOTE! Recording critical error %d at %s:%lu\n", code, filename, address);
    } else {
        LOG_ERROR("NOTE! Recording critical error %d, address=0x%lx\n", code, address);
    }

    // Record error to DB
    error_code = code;
    error_address = address;

    // Currently portuino is mostly used for simulation.  Make sure the user notices something really bad happened
#ifdef ARCH_PORTDUINO
    LOG_ERROR("A critical failure occurred, portduino is exiting...");
    exit(2);
#endif
}