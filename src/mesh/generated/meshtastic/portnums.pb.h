/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.8 */

#ifndef PB_MESHTASTIC_MESHTASTIC_PORTNUMS_PB_H_INCLUDED
#define PB_MESHTASTIC_MESHTASTIC_PORTNUMS_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Enum definitions */
/* For any new 'apps' that run on the device or via sister apps on phones/PCs they should pick and use a
 unique 'portnum' for their application.
 If you are making a new app using meshtastic, please send in a pull request to add your 'portnum' to this
 master table.
 PortNums should be assigned in the following range:
 0-63   Core Meshtastic use, do not use for third party apps
 64-127 Registered 3rd party apps, send in a pull request that adds a new entry to portnums.proto to  register your application
 256-511 Use one of these portnums for your private applications that you don't want to register publically
 All other values are reserved.
 Note: This was formerly a Type enum named 'typ' with the same id #
 We have change to this 'portnum' based scheme for specifying app handlers for particular payloads.
 This change is backwards compatible by treating the legacy OPAQUE/CLEAR_TEXT values identically. */
typedef enum _meshtastic_PortNum {
    /* Deprecated: do not use in new code (formerly called OPAQUE)
 A message sent from a device outside of the mesh, in a form the mesh does not understand
 NOTE: This must be 0, because it is documented in IMeshService.aidl to be so
 ENCODING: binary undefined */
    meshtastic_PortNum_UNKNOWN_APP = 0,
    /* A simple UTF-8 text message, which even the little micros in the mesh
 can understand and show on their screen eventually in some circumstances
 even signal might send messages in this form (see below)
 ENCODING: UTF-8 Plaintext (?) */
    meshtastic_PortNum_TEXT_MESSAGE_APP = 1,
    /* Reserved for built-in GPIO/example app.
 See remote_hardware.proto/HardwareMessage for details on the message sent/received to this port number
 ENCODING: Protobuf */
    meshtastic_PortNum_REMOTE_HARDWARE_APP = 2,
    /* The built-in position messaging app.
 Payload is a Position message.
 ENCODING: Protobuf */
    meshtastic_PortNum_POSITION_APP = 3,
    /* The built-in user info app.
 Payload is a User message.
 ENCODING: Protobuf */
    meshtastic_PortNum_NODEINFO_APP = 4,
    /* Protocol control packets for mesh protocol use.
 Payload is a Routing message.
 ENCODING: Protobuf */
    meshtastic_PortNum_ROUTING_APP = 5,
    /* Admin control packets.
 Payload is a AdminMessage message.
 ENCODING: Protobuf */
    meshtastic_PortNum_ADMIN_APP = 6,
    /* Compressed TEXT_MESSAGE payloads.
 ENCODING: UTF-8 Plaintext (?) with Unishox2 Compression
 NOTE: The Device Firmware converts a TEXT_MESSAGE_APP to TEXT_MESSAGE_COMPRESSED_APP if the compressed
 payload is shorter. There's no need for app developers to do this themselves. Also the firmware will decompress
 any incoming TEXT_MESSAGE_COMPRESSED_APP payload and convert to TEXT_MESSAGE_APP. */
    meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP = 7,
    /* Waypoint payloads.
 Payload is a Waypoint message.
 ENCODING: Protobuf */
    meshtastic_PortNum_WAYPOINT_APP = 8,
    /* Audio Payloads.
 Encapsulated codec2 packets. On 2.4 GHZ Bandwidths only for now
 ENCODING: codec2 audio frames
 NOTE: audio frames contain a 3 byte header (0xc0 0xde 0xc2) and a one byte marker for the decompressed bitrate.
 This marker comes from the 'moduleConfig.audio.bitrate' enum minus one. */
    meshtastic_PortNum_AUDIO_APP = 9,
    /* Same as Text Message but originating from Detection Sensor Module.
 NOTE: This portnum traffic is not sent to the public MQTT starting at firmware version 2.2.9 */
    meshtastic_PortNum_DETECTION_SENSOR_APP = 10,
    /* Provides a 'ping' service that replies to any packet it receives.
 Also serves as a small example module.
 ENCODING: ASCII Plaintext */
    meshtastic_PortNum_REPLY_APP = 32,
    /* Used for the python IP tunnel feature
 ENCODING: IP Packet. Handled by the python API, firmware ignores this one and pases on. */
    meshtastic_PortNum_IP_TUNNEL_APP = 33,
    /* Paxcounter lib included in the firmware
 ENCODING: protobuf */
    meshtastic_PortNum_PAXCOUNTER_APP = 34,
    /* Provides a hardware serial interface to send and receive from the Meshtastic network.
 Connect to the RX/TX pins of a device with 38400 8N1. Packets received from the Meshtastic
 network is forwarded to the RX pin while sending a packet to TX will go out to the Mesh network.
 Maximum packet size of 240 bytes.
 Module is disabled by default can be turned on by setting SERIAL_MODULE_ENABLED = 1 in SerialPlugh.cpp.
 ENCODING: binary undefined */
    meshtastic_PortNum_SERIAL_APP = 64,
    /* STORE_FORWARD_APP (Work in Progress)
 Maintained by Jm Casler (MC Hamster) : jm@casler.org
 ENCODING: Protobuf */
    meshtastic_PortNum_STORE_FORWARD_APP = 65,
    /* Optional port for messages for the range test module.
 ENCODING: ASCII Plaintext
 NOTE: This portnum traffic is not sent to the public MQTT starting at firmware version 2.2.9 */
    meshtastic_PortNum_RANGE_TEST_APP = 66,
    /* Provides a format to send and receive telemetry data from the Meshtastic network.
 Maintained by Charles Crossan (crossan007) : crossan007@gmail.com
 ENCODING: Protobuf */
    meshtastic_PortNum_TELEMETRY_APP = 67,
    /* Experimental tools for estimating node position without a GPS
 Maintained by Github user a-f-G-U-C (a Meshtastic contributor)
 Project files at https://github.com/a-f-G-U-C/Meshtastic-ZPS
 ENCODING: arrays of int64 fields */
    meshtastic_PortNum_ZPS_APP = 68,
    /* Used to let multiple instances of Linux native applications communicate
 as if they did using their LoRa chip.
 Maintained by GitHub user GUVWAF.
 Project files at https://github.com/GUVWAF/Meshtasticator
 ENCODING: Protobuf (?) */
    meshtastic_PortNum_SIMULATOR_APP = 69,
    /* Provides a traceroute functionality to show the route a packet towards
 a certain destination would take on the mesh.
 ENCODING: Protobuf */
    meshtastic_PortNum_TRACEROUTE_APP = 70,
    /* Aggregates edge info for the network by sending out a list of each node's neighbors
 ENCODING: Protobuf */
    meshtastic_PortNum_NEIGHBORINFO_APP = 71,
    /* ATAK Plugin
 Portnum for payloads from the official Meshtastic ATAK plugin */
    meshtastic_PortNum_ATAK_PLUGIN = 72,
    /* Provides unencrypted information about a node for consumption by a map via MQTT */
    meshtastic_PortNum_MAP_REPORT_APP = 73,
    /* Private applications should use portnums >= 256.
 To simplify initial development and testing you can use "PRIVATE_APP"
 in your code without needing to rebuild protobuf files (via [regen-protos.sh](https://github.com/meshtastic/firmware/blob/master/bin/regen-protos.sh)) */
    meshtastic_PortNum_PRIVATE_APP = 256,
    /* ATAK Forwarder Module https://github.com/paulmandal/atak-forwarder
 ENCODING: libcotshrink */
    meshtastic_PortNum_ATAK_FORWARDER = 257,
    /* Currently we limit port nums to no higher than this value */
    meshtastic_PortNum_MAX = 511
} meshtastic_PortNum;

#ifdef __cplusplus
extern "C" {
#endif

/* Helper constants for enums */
#define _meshtastic_PortNum_MIN meshtastic_PortNum_UNKNOWN_APP
#define _meshtastic_PortNum_MAX meshtastic_PortNum_MAX
#define _meshtastic_PortNum_ARRAYSIZE ((meshtastic_PortNum)(meshtastic_PortNum_MAX+1))


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
