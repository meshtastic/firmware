# Device API

The Device API is design to have only a simple stream of ToRadio and FromRadio packets and all polymorphism comes from the flexible set of Google Protocol Buffers which are sent over the wire. We use protocol buffers extensively both for the bluetooth API and for packets inside the mesh or when providing packets to other applications on the phone.

## Streaming version

This protocol is **almost** identical when it is deployed over BLE, Serial/USB or TCP (our three currently supported transports for connecting to phone/PC). Most of this document is in terms of the original BLE version, but this section describes the small changes when this API is exposed over a Streaming (non datagram) transport. The streaming version has the following changes:

- We assume the stream is reliable (though the protocol will resynchronize if bytes are lost or corrupted). i.e. we do not include CRCs or error correction codes.
- Packets always have a four byte header (described below) prefixed before each packet. This header provides framing characters and length.
- The stream going towards the radio is only a series of ToRadio packets (with the extra 4 byte headers)
- The stream going towards the PC is a stream of FromRadio packets (with the 4 byte headers), or if the receiver state machine does not see valid header bytes it can (optionally) print those bytes as the debug console from the radio. This allows the device to emit regular serial debugging messages (which can be understood by a terminal program) but also switch to a more structured set of protobufs once it sees that the PC client has sent a protobuf towards it.

The 4 byte header is constructed to both provide framing and to not look line 'normal' 7 bit ASCII.

- Byte 0: START1 (0x94)
- Byte 1: START2 (0xc3)
- Byte 2: MSB of protobuf length
- Byte 3: LSB of protobuf length

The receiver will validate length and if >512 it will assume the packet is corrupted and return to looking for START1. While looking for START1 any other characters are printed as "debug output". For small example implementation of this reader see the meshtastic-python implementation.

## MeshBluetoothService (the BLE API)

This is the main bluetooth service for the device and provides the API your app should use to get information about the mesh, send packets or provision the radio.

For a reference implementation of a client that uses this service see [RadioInterfaceService](https://github.com/meshtastic/Meshtastic-Android/blob/master/app/src/main/java/com/geeksville/mesh/service/RadioInterfaceService.kt).

Typical flow when a phone connects to the device should be the following (if you want to watch this flow from the python app just run "meshtastic --debug --info" - the flow over BLE is identical):

- There are only three relevant endpoints (and they have built in BLE documentation - so use a BLE tool of your choice to watch them): FromRadio, FromNum (sends notifies when new data is available in FromRadio) and ToRadio
- SetMTU size to 512
- Write a ToRadio.startConfig protobuf to the "ToRadio" endpoint" - this tells the radio you are a new connection and you need the entire NodeDB sent down.
- Read repeatedly from the "FromRadio" endpoint. Each time you read you will get back a FromRadio protobuf (see Meshtatastic-protobuf). Keep reading from this endpoint until you get back and empty buffer.
- See below for the expected sequence for your initial download.
- After the initial download, you should subscribe for BLE "notify" on the "FromNum" endpoint. If a notification arrives, that means there are now one or more FromRadio packets waiting inside FromRadio. Read from FromRadio until you get back an empty packet.
- Any time you want to send packets to the radio, you should write a ToRadio packet into ToRadio.

Expected sequence for initial download:

- After your send startConfig, you will receive a series of FromRadio packets. The sequence of these packets will be as follows (but you are best not counting on this, instead just update your model for whatever packet you receive - based on looking at the type)
- Read a RadioConfig from "radio" - used to get the channel and radio settings
- Read a User from "user" - to get the username for this node
- Read a MyNodeInfo from "mynode" to get information about this local device
- Write an empty record to "nodeinfo" to restart the nodeinfo reading state machine
- Read a series of NodeInfo packets to build the phone's copy of the current NodeDB for the mesh
- Read a endConfig packet that indicates that the entire state you need has been sent.
- Read a series of MeshPackets until it returns empty to get any messages that arrived for this node while the phone was away

For definitions (and documentation) on FromRadio, ToRadio, MyNodeInfo, NodeInfo and User protocol buffers see [mesh.proto](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/mesh.proto)

UUID for the service: 6ba1b218-15a8-461f-9fa8-5dcae273eafd

Each characteristic is listed as follows:

UUID
Properties
Description (including human readable name)

8ba2bcc2-ee02-4a55-a531-c525c5e454d5
read
fromradio - contains a newly received FromRadio packet destined towards the phone (up to MAXPACKET bytes per packet).
After reading the esp32 will put the next packet in this mailbox. If the FIFO is empty it will put an empty packet in this
mailbox.

f75c76d2-129e-4dad-a1dd-7866124401e7
write
toradio - write ToRadio protobufs to this characteristic to send them (up to MAXPACKET len)

ed9da18c-a800-4f66-a670-aa7547e34453
read,notify,write
fromnum - the current packet # in the message waiting inside fromradio, if the phone sees this notify it should read messages
until it catches up with this number.

The phone can write to this register to go backwards up to FIXME packets, to handle the rare case of a fromradio packet was dropped after the esp32 callback was called, but before it arrives at the phone. If the phone writes to this register the esp32 will discard older packets and put the next packet >= fromnum in fromradio.
When the esp32 advances fromnum, it will delay doing the notify by 100ms, in the hopes that the notify will never actally need to be sent if the phone is already pulling from fromradio.

Note: that if the phone ever sees this number decrease, it means the esp32 has rebooted.

Re: queue management
Not all messages are kept in the fromradio queue (filtered based on SubPacket):

- only the most recent Position and User messages for a particular node are kept
- all Data SubPackets are kept
- No WantNodeNum / DenyNodeNum messages are kept
  A variable keepAllPackets, if set to true will suppress this behavior and instead keep everything for forwarding to the phone (for debugging)

### A note on MTU sizes

This device will work with any MTU size, but it is highly recommended that you call your phone's "setMTU function to increase MTU to 512 bytes" as soon as you connect to a service. This will dramatically improve performance when reading/writing packets.

### Protobuf API

On connect, you should send a want_config_id protobuf to the device. This will cause the device to send its node DB and radio config via the fromradio endpoint. After sending the full DB, the radio will send a want_config_id to indicate it is done sending the configuration.

### Other bluetooth services

This document focuses on the core device protocol, but it is worth noting that the following other Bluetooth services are also
provided by the device.

#### BluetoothSoftwareUpdate

The software update service. For a sample function that performs a software update using this API see [startUpdate](https://github.com/meshtastic/Meshtastic-Android/blob/master/app/src/main/java/com/geeksville/mesh/service/SoftwareUpdateService.kt).

SoftwareUpdateService UUID cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30

Characteristics

| UUID                                 | properties  | description                                                                                                       |
| ------------------------------------ | ----------- | ----------------------------------------------------------------------------------------------------------------- |
| e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e | write,read  | total image size, 32 bit, write this first, then read read back to see if it was acceptable (0 mean not accepted) |
| e272ebac-d463-4b98-bc84-5cc1a39ee517 | write       | data, variable sized, recommended 512 bytes, write one for each block of file                                     |
| 4826129c-c22a-43a3-b066-ce8f0d5bacc6 | write       | crc32, write last - writing this will complete the OTA operation, now you can read result                         |
| 5e134862-7411-4424-ac4a-210937432c77 | read,notify | result code, readable but will notify when the OTA operation completes                                            |
| GATT_UUID_SW_VERSION_STR/0x2a28      | read        | We also implement these standard GATT entries because SW update probably needs them:                              |
| GATT_UUID_MANU_NAME/0x2a29           | read        |                                                                                                                   |
| GATT_UUID_HW_VERSION_STR/0x2a27      | read        |                                                                                                                   |

#### DeviceInformationService

Implements the standard BLE contract for this service (has software version, hardware model, serial number, etc...)

#### BatteryLevelService

Implements the standard BLE contract service, provides battery level in a way that most client devices should automatically understand (i.e. it should show in the bluetooth devices screen automatically)
