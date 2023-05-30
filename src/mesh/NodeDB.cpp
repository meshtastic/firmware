#include "configuration.h"

#include "../detect/ScanI2C.h"
#include "Channels.h"
#include "CryptoEngine.h"
#include "FSCommon.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "error.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include <ErriezCRC32.h>
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef ARCH_ESP32
#include "mesh/http/WiFiAPClient.h"
#include "modules/esp32/StoreForwardModule.h"
#include <Preferences.h>
#include <nvs_flash.h>
#endif

#ifdef ARCH_NRF52
#include <bluefruit.h>
#include <utility/bonding.h>
#endif

NodeDB nodeDB;

// we have plenty of ram so statically alloc this tempbuf (for now)
EXT_RAM_ATTR meshtastic_DeviceState devicestate;
meshtastic_MyNodeInfo &myNodeInfo = devicestate.my_node;
meshtastic_LocalConfig config;
meshtastic_LocalModuleConfig moduleConfig;
meshtastic_ChannelFile channelFile;
meshtastic_OEMStore oemStore;

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

static uint8_t ourMacAddr[6];

NodeDB::NodeDB() : nodes(devicestate.node_db), numNodes(&devicestate.node_db_count) {}

/**
 * Most (but not always) of the time we want to treat packets 'from' the local phone (where from == 0), as if they originated on
 * the local node. If from is zero this function returns our node number instead
 */
NodeNum getFrom(const meshtastic_MeshPacket *p)
{
    return (p->from == 0) ? nodeDB.getNodeNum() : p->from;
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

    // temp hack for quicker testing
    // devicestate.no_save = true;
    if (devicestate.no_save) {
        LOG_DEBUG("***** DEVELOPMENT MODE - DO NOT RELEASE *****\n");

        // Sleep quite frequently to stress test the BLE comms, broadcast position every 6 mins
        config.display.screen_on_secs = 10;
        config.power.wait_bluetooth_secs = 10;
        config.position.position_broadcast_secs = 6 * 60;
        config.power.ls_secs = 60;
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_TW;

        // Enter super deep sleep soon and stay there not very long
        // radioConfig.preferences.mesh_sds_timeout_secs = 10;
        // radioConfig.preferences.sds_secs = 60;
    }

    // Update the global myRegion
    initRegion();

    if (didFactoryReset) {
        LOG_INFO("Rebooting due to factory reset");
        screen->startRebootScreen();
        rebootAtMsec = millis() + (5 * 1000);
    }

    return didFactoryReset;
}

bool NodeDB::factoryReset()
{
    LOG_INFO("Performing factory reset!\n");
    // first, remove the "/prefs" (this removes most prefs)
    rmDir("/prefs");
    // second, install default state (this will deal with the duplicate mac address issue)
    installDefaultDeviceState();
    installDefaultConfig();
    installDefaultModuleConfig();
    installDefaultChannels();
    // third, write everything to disk
    saveToDisk();
#ifdef ARCH_ESP32
    // This will erase what's in NVS including ssl keys, persistant variables and ble pairing
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
    config.has_bluetooth = true;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    config.lora.sx126x_rx_boosted_gain = false;
    config.lora.tx_enabled =
        true; // FIXME: maybe false in the future, and setting region to enable it. (unset region forces it off)
    config.lora.override_duty_cycle = false;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.hop_limit = HOP_RELIABLE;
    config.position.gps_enabled = true;
    config.position.position_broadcast_smart_enabled = true;
    config.position.broadcast_smart_minimum_distance = 100;
    config.position.broadcast_smart_minimum_interval_secs = 30;
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER)
        config.device.node_info_broadcast_secs = 3 * 60 * 60;
    config.device.serial_enabled = true;
    resetRadioConfig();
    strncpy(config.network.ntp_server, "0.pool.ntp.org", 32);
    // FIXME: Default to bluetooth capability of platform as default
    config.bluetooth.enabled = true;
    config.bluetooth.fixed_pin = defaultBLEPin;
#if defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER)
    bool hasScreen = true;
#else
    bool hasScreen = screen_found.port != ScanI2C::I2CPort::NO_I2C;
#endif
    config.bluetooth.mode = hasScreen ? meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN
                                      : meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    // for backward compat, default position flags are ALT+MSL
    config.position.position_flags =
        (meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL);

    initConfigIntervals();
}

void NodeDB::initConfigIntervals()
{
    config.position.gps_update_interval = default_gps_update_interval;
    config.position.gps_attempt_time = default_gps_attempt_time;
    config.position.position_broadcast_secs = default_broadcast_interval_secs;

    config.power.ls_secs = default_ls_secs;
    config.power.mesh_sds_timeout_secs = default_mesh_sds_timeout_secs;
    config.power.min_wake_secs = default_min_wake_secs;
    config.power.sds_secs = default_sds_secs;
    config.power.wait_bluetooth_secs = default_wait_bluetooth_secs;

    config.display.screen_on_secs = default_screen_on_secs;
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
    moduleConfig.has_canned_message = true;

    strncpy(moduleConfig.mqtt.address, default_mqtt_address, sizeof(moduleConfig.mqtt.address));
    strncpy(moduleConfig.mqtt.username, default_mqtt_username, sizeof(moduleConfig.mqtt.username));
    strncpy(moduleConfig.mqtt.password, default_mqtt_password, sizeof(moduleConfig.mqtt.password));

    initModuleConfigIntervals();
}

void NodeDB::installRoleDefaults(meshtastic_Config_DeviceConfig_Role role)
{
    if (role == meshtastic_Config_DeviceConfig_Role_ROUTER) {
        initConfigIntervals();
        initModuleConfigIntervals();
    } else if (role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        config.display.screen_on_secs = 1;
    } else if (role == meshtastic_Config_DeviceConfig_Role_TRACKER) {
        config.position.gps_update_interval = 30;
    } else if (role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
        moduleConfig.telemetry.environment_measurement_enabled = true;
        moduleConfig.telemetry.environment_update_interval = 300;
    }
}

void NodeDB::initModuleConfigIntervals()
{
    moduleConfig.telemetry.device_update_interval = default_broadcast_interval_secs;
    moduleConfig.telemetry.environment_update_interval = default_broadcast_interval_secs;
    moduleConfig.telemetry.air_quality_interval = default_broadcast_interval_secs;
}

void NodeDB::installDefaultChannels()
{
    LOG_INFO("Installing default ChannelFile\n");
    memset(&channelFile, 0, sizeof(meshtastic_ChannelFile));
    channelFile.version = DEVICESTATE_CUR_VER;
}

void NodeDB::resetNodes()
{
    devicestate.node_db_count = 0;
    memset(devicestate.node_db, 0, sizeof(devicestate.node_db));
    saveDeviceStateToDisk();
}

void NodeDB::installDefaultDeviceState()
{
    LOG_INFO("Installing default DeviceState\n");
    memset(&devicestate, 0, sizeof(meshtastic_DeviceState));

    *numNodes = 0;

    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_owner = true;
    devicestate.node_db_count = 0;
    devicestate.version = DEVICESTATE_CUR_VER;
    devicestate.receive_queue_count = 0; // Not yet implemented FIXME

    // default to no GPS, until one has been found by probing
    myNodeInfo.has_gps = false;
    myNodeInfo.message_timeout_msec = FLOOD_EXPIRE_TIME;
    generatePacketId(); // FIXME - ugly way to init current_packet_id;

    // Init our blank owner info to reasonable defaults
    getMacAddr(ourMacAddr);

    // Set default owner name
    pickNewNodeNum(); // based on macaddr now
    snprintf(owner.long_name, sizeof(owner.long_name), "Meshtastic %02x%02x", ourMacAddr[4], ourMacAddr[5]);
    snprintf(owner.short_name, sizeof(owner.short_name), "%02x%02x", ourMacAddr[4], ourMacAddr[5]);

    snprintf(owner.id, sizeof(owner.id), "!%08x", getNodeNum()); // Default node ID now based on nodenum
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));
}

void NodeDB::init()
{
    LOG_INFO("Initializing NodeDB\n");
    loadFromDisk();

    uint32_t devicestateCRC = crc32Buffer(&devicestate, sizeof(devicestate));
    uint32_t configCRC = crc32Buffer(&config, sizeof(config));
    uint32_t channelFileCRC = crc32Buffer(&channelFile, sizeof(channelFile));

    int saveWhat = 0;

    myNodeInfo.max_channels = MAX_NUM_CHANNELS; // tell others the max # of channels we can understand

    myNodeInfo.error_code =
        meshtastic_CriticalErrorCode_NONE; // For the error code, only show values from this boot (discard value from flash)
    myNodeInfo.error_address = 0;

    // likewise - we always want the app requirements to come from the running appload
    myNodeInfo.min_app_version = 20300; // format is Mmmss (where M is 1+the numeric major number. i.e. 20120 means 1.1.20

    // Note! We do this after loading saved settings, so that if somehow an invalid nodenum was stored in preferences we won't
    // keep using that nodenum forever. Crummy guess at our nodenum (but we will check against the nodedb to avoid conflicts)
    pickNewNodeNum();

    // Set our board type so we can share it with others
    owner.hw_model = HW_VENDOR;

    // Include our owner in the node db under our nodenum
    meshtastic_NodeInfo *info = getOrCreateNode(getNodeNum());
    info->user = owner;
    info->has_user = true;

    strncpy(myNodeInfo.firmware_version, optstr(APP_VERSION), sizeof(myNodeInfo.firmware_version));

#ifdef ARCH_ESP32
    Preferences preferences;
    preferences.begin("meshtastic", false);
    myNodeInfo.reboot_count = preferences.getUInt("rebootCounter", 0);
    preferences.end();
    LOG_DEBUG("Number of Device Reboots: %d\n", myNodeInfo.reboot_count);

    /* The ESP32 has a wifi radio. This will need to be modified at some point so
     *    the test isn't so simplistic.
     */
    myNodeInfo.has_wifi = true;
#endif

    resetRadioConfig(); // If bogus settings got saved, then fix them
    LOG_DEBUG("region=%d, NODENUM=0x%x, dbsize=%d\n", config.lora.region, myNodeInfo.my_node_num, *numNodes);

    if (devicestateCRC != crc32Buffer(&devicestate, sizeof(devicestate)))
        saveWhat |= SEGMENT_DEVICESTATE;
    if (configCRC != crc32Buffer(&config, sizeof(config)))
        saveWhat |= SEGMENT_CONFIG;
    if (channelFileCRC != crc32Buffer(&channelFile, sizeof(channelFile)))
        saveWhat |= SEGMENT_CHANNELS;

    if (!devicestate.node_remote_hardware_pins) {
        meshtastic_NodeRemoteHardwarePin empty[12] = {meshtastic_RemoteHardwarePin_init_default};
        memcpy(devicestate.node_remote_hardware_pins, empty, sizeof(empty));
    }

    saveToDisk(saveWhat);
}

// We reserve a few nodenums for future use
#define NUM_RESERVED 4

/**
 * get our starting (provisional) nodenum from flash.
 */
void NodeDB::pickNewNodeNum()
{
    NodeNum r = myNodeInfo.my_node_num;

    // If we don't have a nodenum at app - pick an initial nodenum based on the macaddr
    if (r == 0)
        r = (ourMacAddr[2] << 24) | (ourMacAddr[3] << 16) | (ourMacAddr[4] << 8) | ourMacAddr[5];

    if (r == NODENUM_BROADCAST || r < NUM_RESERVED)
        r = NUM_RESERVED; // don't pick a reserved node number

    meshtastic_NodeInfo *found;
    while ((found = getNode(r)) && memcmp(found->user.macaddr, owner.macaddr, sizeof(owner.macaddr))) {
        NodeNum n = random(NUM_RESERVED, NODENUM_BROADCAST); // try a new random choice
        LOG_DEBUG("NOTE! Our desired nodenum 0x%x is in use, so trying for 0x%x\n", r, n);
        r = n;
    }

    myNodeInfo.my_node_num = r;
}

static const char *prefFileName = "/prefs/db.proto";
static const char *configFileName = "/prefs/config.proto";
static const char *moduleConfigFileName = "/prefs/module.proto";
static const char *channelFileName = "/prefs/channels.proto";
static const char *oemConfigFile = "/oem/oem.proto";

/** Load a protobuf from a file, return true for success */
bool NodeDB::loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct)
{
    bool okay = false;
#ifdef FSCom
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM

    auto f = FSCom.open(filename, FILE_O_READ);

    if (f) {
        LOG_INFO("Loading %s\n", filename);
        pb_istream_t stream = {&readcb, &f, protoSize};

        // LOG_DEBUG("Preload channel name=%s\n", channelSettings.name);

        memset(dest_struct, 0, objSize);
        if (!pb_decode(&stream, fields, dest_struct)) {
            LOG_ERROR("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
        } else {
            okay = true;
        }

        f.close();
    } else {
        LOG_INFO("No %s preferences found\n", filename);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
    return okay;
}

void NodeDB::loadFromDisk()
{
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    if (!loadProto(prefFileName, meshtastic_DeviceState_size, sizeof(meshtastic_DeviceState), &meshtastic_DeviceState_msg,
                   &devicestate)) {
        installDefaultDeviceState(); // Our in RAM copy might now be corrupt
    } else {
        if (devicestate.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("Devicestate %d is old, discarding\n", devicestate.version);
            factoryReset();
        } else {
            LOG_INFO("Loaded saved devicestate version %d\n", devicestate.version);
        }
    }

    if (!loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(meshtastic_LocalConfig), &meshtastic_LocalConfig_msg,
                   &config)) {
        installDefaultConfig(); // Our in RAM copy might now be corrupt
    } else {
        if (config.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("config %d is old, discarding\n", config.version);
            installDefaultConfig();
        } else {
            LOG_INFO("Loaded saved config version %d\n", config.version);
        }
    }

    if (!loadProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, sizeof(meshtastic_LocalModuleConfig),
                   &meshtastic_LocalModuleConfig_msg, &moduleConfig)) {
        installDefaultModuleConfig(); // Our in RAM copy might now be corrupt
    } else {
        if (moduleConfig.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("moduleConfig %d is old, discarding\n", moduleConfig.version);
            installDefaultModuleConfig();
        } else {
            LOG_INFO("Loaded saved moduleConfig version %d\n", moduleConfig.version);
        }
    }

    if (!loadProto(channelFileName, meshtastic_ChannelFile_size, sizeof(meshtastic_ChannelFile), &meshtastic_ChannelFile_msg,
                   &channelFile)) {
        installDefaultChannels(); // Our in RAM copy might now be corrupt
    } else {
        if (channelFile.version < DEVICESTATE_MIN_VER) {
            LOG_WARN("channelFile %d is old, discarding\n", channelFile.version);
            installDefaultChannels();
        } else {
            LOG_INFO("Loaded saved channelFile version %d\n", channelFile.version);
        }
    }

    if (loadProto(oemConfigFile, meshtastic_OEMStore_size, sizeof(meshtastic_OEMStore), &meshtastic_OEMStore_msg, &oemStore)) {
        LOG_INFO("Loaded OEMStore\n");
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
            FSCom.format();
            // After formatting, the device needs to be restarted
            nodeDB.resetRadioConfig(true);
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
    if (!devicestate.no_save) {
#ifdef FSCom
        FSCom.mkdir("/prefs");
#endif
        saveProto(channelFileName, meshtastic_ChannelFile_size, &meshtastic_ChannelFile_msg, &channelFile);
    }
}

void NodeDB::saveDeviceStateToDisk()
{
    if (!devicestate.no_save) {
#ifdef FSCom
        FSCom.mkdir("/prefs");
#endif
        saveProto(prefFileName, meshtastic_DeviceState_size, &meshtastic_DeviceState_msg, &devicestate);
    }
}

void NodeDB::saveToDisk(int saveWhat)
{
    if (!devicestate.no_save) {
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
            saveProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size, &meshtastic_LocalModuleConfig_msg, &moduleConfig);
        }

        if (saveWhat & SEGMENT_CHANNELS) {
            saveChannelsToDisk();
        }
    } else {
        LOG_DEBUG("***** DEVELOPMENT MODE - DO NOT RELEASE - not saving to flash *****\n");
    }
}

const meshtastic_NodeInfo *NodeDB::readNextInfo(uint32_t &readIndex)
{
    if (readIndex < *numNodes)
        return &nodes[readIndex++];
    else
        return NULL;
}

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const meshtastic_NodeInfo *n)
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

size_t NodeDB::getNumOnlineNodes()
{
    size_t numseen = 0;

    // FIXME this implementation is kinda expensive
    for (int i = 0; i < *numNodes; i++)
        if (sinceLastSeen(&nodes[i]) < NUM_ONLINE_SECS)
            numseen++;

    return numseen;
}

#include "MeshModule.h"

/** Update position info for this node based on received position data
 */
void NodeDB::updatePosition(uint32_t nodeId, const meshtastic_Position &p, RxSource src)
{
    meshtastic_NodeInfo *info = getOrCreateNode(nodeId);
    if (!info) {
        return;
    }

    if (src == RX_SRC_LOCAL) {
        // Local packet, fully authoritative
        LOG_INFO("updatePosition LOCAL pos@%x, time=%u, latI=%d, lonI=%d, alt=%d\n", p.timestamp, p.time, p.latitude_i,
                 p.longitude_i, p.altitude);
        info->position = p;
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
        LOG_INFO("updatePosition REMOTE node=0x%x time=%u, latI=%d, lonI=%d\n", nodeId, p.time, p.latitude_i, p.longitude_i);

        // First, back up fields that we want to protect from overwrite
        uint32_t tmp_time = info->position.time;

        // Next, update atomically
        info->position = p;

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
    meshtastic_NodeInfo *info = getOrCreateNode(nodeId);
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

/** Update user info for this node based on received user data
 */
bool NodeDB::updateUser(uint32_t nodeId, const meshtastic_User &p)
{
    meshtastic_NodeInfo *info = getOrCreateNode(nodeId);
    if (!info) {
        return false;
    }

    LOG_DEBUG("old user %s/%s/%s\n", info->user.id, info->user.long_name, info->user.short_name);

    bool changed = memcmp(&info->user, &p,
                          sizeof(info->user)); // Both of these blocks start as filled with zero so I think this is okay

    info->user = p;
    LOG_DEBUG("updating changed=%d user %s/%s/%s\n", changed, info->user.id, info->user.long_name, info->user.short_name);
    info->has_user = true;

    if (changed) {
        updateGUIforNode = info;
        powerFSM.trigger(EVENT_NODEDB_UPDATED);
        notifyObservers(true); // Force an update whether or not our node counts have changed

        // We just changed something important about the user, store our DB
        saveToDisk(SEGMENT_DEVICESTATE);
    }

    return changed;
}

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.from) {
        LOG_DEBUG("Update DB node 0x%x, rx_time=%u, channel=%d\n", mp.from, mp.rx_time, mp.channel);

        meshtastic_NodeInfo *info = getOrCreateNode(getFrom(&mp));
        if (!info) {
            return;
        }

        if (mp.rx_time) // if the packet has a valid timestamp use it to update our last_heard
            info->last_heard = mp.rx_time;

        if (mp.rx_snr)
            info->snr = mp.rx_snr; // keep the most recent SNR we received for this node.

        if (mp.decoded.portnum == meshtastic_PortNum_NODEINFO_APP) {
            info->channel = mp.channel;
        }
    }
}

uint8_t NodeDB::getNodeChannel(NodeNum n)
{
    meshtastic_NodeInfo *info = getNode(n);
    if (!info) {
        return 0; // defaults to PRIMARY
    }
    return info->channel;
}

/// Find a node in our DB, return null for missing
/// NOTE: This function might be called from an ISR
meshtastic_NodeInfo *NodeDB::getNode(NodeNum n)
{
    for (int i = 0; i < *numNodes; i++)
        if (nodes[i].num == n)
            return &nodes[i];

    return NULL;
}

/// Find a node in our DB, create an empty NodeInfo if missing
meshtastic_NodeInfo *NodeDB::getOrCreateNode(NodeNum n)
{
    meshtastic_NodeInfo *info = getNode(n);

    if (!info) {
        if ((*numNodes >= MAX_NUM_NODES) || (memGet.getFreeHeap() < meshtastic_NodeInfo_size * 3)) {
            screen->print("warning: node_db full! erasing oldest entry\n");
            // look for oldest node and erase it
            uint32_t oldest = UINT32_MAX;
            int oldestIndex = -1;
            for (int i = 0; i < *numNodes; i++) {
                if (nodes[i].last_heard < oldest) {
                    oldest = nodes[i].last_heard;
                    oldestIndex = i;
                }
            }
            // Shove the remaining nodes down the chain
            for (int i = oldestIndex; i < *numNodes - 1; i++) {
                nodes[i] = nodes[i + 1];
            }
            (*numNodes)--;
        }
        // add the node at the end
        info = &nodes[(*numNodes)++];

        // everything is missing except the nodenum
        memset(info, 0, sizeof(*info));
        info->num = n;
    }

    return info;
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
    myNodeInfo.error_code = code;
    myNodeInfo.error_address = address;
    myNodeInfo.error_count++;

    // Currently portuino is mostly used for simulation.  Make sue the user notices something really bad happend
#ifdef ARCH_PORTDUINO
    LOG_ERROR("A critical failure occurred, portduino is exiting...");
    exit(2);
#endif
}
