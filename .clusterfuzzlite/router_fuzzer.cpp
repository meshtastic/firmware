// Fuzzer implementation that sends MeshPackets to Router::enqueueReceivedMessage.
#include <mutex>
#include <pb_decode.h>
#include <string>
#include <thread>

#include "PortduinoGlue.h"
#include "PowerFSM.h"
#include "mesh/MeshTypes.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "mesh/TypeConversions.h"
#include "mesh/mesh-pb-constants.h"

namespace
{
constexpr uint32_t nodeId = 0x12345678;
bool hasBeenConfigured = false;
} // namespace

// Called just prior to starting Meshtastic. Allows for setting config values before startup.
void lateInitVariant()
{
    settingsMap[logoutputlevel] = level_error;
    channelFile.channels[0] = meshtastic_Channel{
        .has_settings = true,
        .settings =
            meshtastic_ChannelSettings{
                .psk = {.size = 1, .bytes = {/*defaultpskIndex=*/1}},
                .name = "LongFast",
                .uplink_enabled = true,
                .has_module_settings = true,
                .module_settings = {.position_precision = 16},
            },
        .role = meshtastic_Channel_Role_PRIMARY,
    };
    config.security.admin_key[0] = {
        .size = 32,
        .bytes = {0xcd, 0xc0, 0xb4, 0x3c, 0x53, 0x24, 0xdf, 0x13, 0xca, 0x5a, 0xa6, 0x0c, 0x0d, 0xec, 0x85, 0x5a,
                  0x4c, 0xf6, 0x1a, 0x96, 0x04, 0x1a, 0x3e, 0xfc, 0xbb, 0x8e, 0x33, 0x71, 0xe5, 0xfc, 0xff, 0x3c},
    };
    config.security.admin_key_count = 1;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    moduleConfig.has_mqtt = true;
    moduleConfig.mqtt = meshtastic_ModuleConfig_MQTTConfig{
        .enabled = true,
        .proxy_to_client_enabled = true,
    };
    moduleConfig.has_store_forward = true;
    moduleConfig.store_forward = meshtastic_ModuleConfig_StoreForwardConfig{
        .enabled = true,
        .history_return_max = 4,
        .history_return_window = 600,
        .is_server = true,
    };
    meshtastic_Position fixedGPS = meshtastic_Position{
        .has_latitude_i = true,
        .latitude_i = static_cast<uint32_t>(1 * 1e7),
        .has_longitude_i = true,
        .longitude_i = static_cast<uint32_t>(3 * 1e7),
        .has_altitude = true,
        .altitude = 64,
        .location_source = meshtastic_Position_LocSource_LOC_MANUAL,
    };
    nodeDB->setLocalPosition(fixedGPS);
    config.has_position = true;
    config.position.fixed_position = true;
    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(nodeDB->getNodeNum());
    info->has_position = true;
    info->position = TypeConversions::ConvertToPositionLite(fixedGPS);
    hasBeenConfigured = true;
}

extern "C" {
int portduino_main(int argc, char **argv); // Renamed "main" function from Meshtastic binary.

// Start Meshtastic in a thread and wait till it has reached the ON state.
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    settingsMap[maxtophone] = 5;

    std::thread t([program = *argv[0]]() {
        char nodeIdStr[12];
        strcpy(nodeIdStr, std::to_string(nodeId).c_str());
        int argc = 5;
        char *argv[] = {program, "-d", "/tmp/meshtastic", "-h", nodeIdStr, nullptr};
        portduino_main(argc, argv);
    });
    t.detach();

    // Wait for startup.
    for (int i = 1; i < 20; ++i) {
        if (powerFSM.getState() == &stateON) {
            assert(hasBeenConfigured);
            assert(router);
            assert(nodeDB);
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 1;
}

// This is the main entrypoint for the fuzzer (the fuzz target). The fuzzer will provide an array of bytes to be
// interpreted by this method. To keep things simple, the bytes are interpreted as a binary serialized MeshPacket
// proto. Any crashes discovered by the fuzzer will be written to a file. Unserialize that file to print the MeshPacket
// that caused the failure.
//
// This guide provides best practices for writing a fuzzer target.
// https://github.com/google/fuzzing/blob/master/docs/good-fuzz-target.md
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t length)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
    pb_istream_t stream = pb_istream_from_buffer(data, length);
    // Ignore any inputs that fail to decode or have fields set that are not transmitted over LoRa.
    if (!pb_decode(&stream, &meshtastic_MeshPacket_msg, &p) || p.rx_time || p.rx_snr || p.priority || p.rx_rssi || p.delayed ||
        p.public_key.size || p.next_hop || p.relay_node || p.tx_after)
        return -1; // Reject: The input will not be added to the corpus.
    if (p.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        meshtastic_Data d;
        stream = pb_istream_from_buffer(p.decoded.payload.bytes, p.decoded.payload.size);
        if (!pb_decode(&stream, &meshtastic_Data_msg, &d))
            return -1; // Reject: The input will not be added to the corpus.
    }

    // Provide default values for a few fields so the fuzzer doesn't need to guess them.
    if (p.from == 0)
        p.from = nodeDB->getNodeNum();
    if (p.to == 0)
        p.to = nodeDB->getNodeNum();
    static uint32_t packetId = 0;
    if (p.id == 0)
        p.id == ++packetId;
    if (p.pki_encrypted && config.security.admin_key_count)
        memcpy(&p.public_key, &config.security.admin_key[0], sizeof(p.public_key));

    // Ideally only one packet, the one generated by the fuzzer, is being processed by the firmware at
    // a time. We acquire a lock here, and the router unlocks it after it has processed all queued packets.
    // Grabbing the lock again, below, should block until the queue has been emptied.
    router->inProgressLock.lock();
    router->enqueueReceivedMessage(packetPool.allocCopy(p));

    const std::lock_guard<std::mutex> lck(router->inProgressLock);
    return 0; // Accept: The input may be added to the corpus.
}
}