// Fuzzer implementation that sends MeshPackets to Router::enqueueReceivedMessage.
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <pb_decode.h>
#include <stdexcept>
#include <string>
#include <thread>

#include "PortduinoGPIO.h"
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
// Set to true when lateInitVariant finishes. Used to ensure lateInitVariant was called during startup.
bool hasBeenConfigured = false;

// These are used to block the Arduino loop() function until a fuzzer input is ready. This is
// an optimization that prevents a sleep from happening before the loop is run. The Arduino loop
// function calls loopCanSleep() before sleeping. loopCanSleep is implemented here in the fuzzer
// and blocks until runLoopOnce() is called to signal for the loop to run.
bool fuzzerRunning = false;  // Set to true once LLVMFuzzerTestOneInput has started running.
bool loopCanRun = true;      // The main Arduino loop() can run when this is true.
bool loopIsWaiting = false;  // The main Arduino loop() is waiting to be signaled to run.
bool loopShouldExit = false; // Indicates that the main Arduino thread should exit by throwing ShouldExitException.
std::mutex loopLock;
std::condition_variable loopCV;
std::thread meshtasticThread;

// This exception is thrown when the portuino main thread should exit.
class ShouldExitException : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// Start the loop for one test case and wait till the loop has completed. This ensures fuzz
// test cases do not overlap with one another. This helps the fuzzer attribute a crash to the
// single, currently running, test case.
void runLoopOnce()
{
    realHardware = true; // Avoids delay(100) within portduino/main.cpp
    std::unique_lock<std::mutex> lck(loopLock);
    fuzzerRunning = true;
    loopCanRun = true;
    loopCV.notify_one();
    loopCV.wait(lck, [] { return !loopCanRun && loopIsWaiting; });
}
} // namespace

// Called in the main Arduino loop function to determine if the loop can delay/sleep before running again.
// We use this as a way to block the loop from sleeping and to start the loop function immediately when a
// fuzzer input is ready.
bool loopCanSleep()
{
    std::unique_lock<std::mutex> lck(loopLock);
    loopIsWaiting = true;
    loopCV.notify_one();
    loopCV.wait(lck, [] { return loopCanRun || loopShouldExit; });
    loopIsWaiting = false;
    if (loopShouldExit)
        throw ShouldExitException("exit");
    if (!fuzzerRunning)
        return true;    // The loop can sleep before the fuzzer starts.
    loopCanRun = false; // Only run the loop once before waiting again.
    return false;
}

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

    meshtasticThread = std::thread([program = *argv[0]]() {
        char nodeIdStr[12];
        strcpy(nodeIdStr, std::to_string(nodeId).c_str());
        int argc = 7;
        char *argv[] = {program, "-d", "/tmp/meshtastic", "-h", nodeIdStr, "-p", "0", nullptr};
        try {
            portduino_main(argc, argv);
        } catch (const ShouldExitException &) {
        }
    });
    std::atexit([] {
        {
            const std::lock_guard<std::mutex> lck(loopLock);
            loopShouldExit = true;
            loopCV.notify_one();
        }
        meshtasticThread.join();
    });

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

    router->enqueueReceivedMessage(packetPool.allocCopy(p));
    runLoopOnce();
    return 0; // Accept: The input may be added to the corpus.
}
}