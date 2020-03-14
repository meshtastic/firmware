# Bluetooth API

The Bluetooth API is design to have only a few characteristics and most polymorphism comes from the flexible set of Google Protocol Buffers which are sent over the wire.  We use protocol buffers extensively both for the bluetooth API and for packets inside the mesh or when providing packets to other applications on the phone.

## A note on MTU sizes

This device will work with any MTU size, but it is highly recommended that you call your phone's "setMTU function to increase MTU to 512 bytes" as soon as you connect to a service.  This will dramatically improve performance when reading/writing packets.

## MeshBluetoothService 

This is the main bluetooth service for the device and provides the API your app should use to get information about the mesh, send packets or provision the radio.  

For a reference implementation of a client that uses this service see [RadioInterfaceService](https://github.com/meshtastic/Meshtastic-Android/blob/master/app/src/main/java/com/geeksville/mesh/service/RadioInterfaceService.kt).  Typical flow when 
a phone connects to the device should be the following:

* SetMTU size to 512
* Read a RadioConfig from "radio" - used to get the channel and radio settings
* Read (and write if incorrect) a User from "user" - to get the username for this node
* Read a MyNodeInfo from "mynode" to get information about this local device
* Write an empty record to "nodeinfo" to restart the nodeinfo reading state machine
* Read from "nodeinfo" until it returns empty to build the phone's copy of the current NodeDB for the mesh
* Read from "fromradio" until it returns empty to get any messages that arrived for this node while the phone was away
* Subscribe to notify on "fromnum" to get notified whenever the device has a new received packet
* Read that new packet from "fromradio"
* Whenever the phone has a packet to send write to "toradio"

For definitions (and documentation) on FromRadio, ToRadio, MyNodeInfo, NodeInfo and User  protocol buffers see [mesh.proto](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/mesh.proto)

UUID for the service: 6ba1b218-15a8-461f-9fa8-5dcae273eafd

Each characteristic is listed as follows:

UUID
Properties
Description (including human readable name)

8ba2bcc2-ee02-4a55-a531-c525c5e454d5
read
fromradio - contains a newly received FromRadio packet destined towards the phone (up to MAXPACKET bytes per packet).
After reading the esp32 will put the next packet in this mailbox.  If the FIFO is empty it will put an empty packet in this
mailbox.

f75c76d2-129e-4dad-a1dd-7866124401e7
write
toradio - write ToRadio protobufs to this characteristic to send them (up to MAXPACKET len)

ed9da18c-a800-4f66-a670-aa7547e34453
read,notify,write
fromnum - the current packet # in the message waiting inside fromradio, if the phone sees this notify it should read messages
until it catches up with this number.

The phone can write to this register to go backwards up to FIXME packets, to handle the rare case of a fromradio packet was dropped after the esp32 callback was called, but before it arrives at the phone.  If the phone writes to this register the esp32 will discard older packets and put the next packet >= fromnum in fromradio.
When the esp32 advances fromnum, it will delay doing the notify by 100ms, in the hopes that the notify will never actally need to be sent if the phone is already pulling from fromradio.

Note: that if the phone ever sees this number decrease, it means the esp32 has rebooted.

ea9f3f82-8dc4-4733-9452-1f6da28892a2
read
mynode - read this to access a MyNodeInfo protobuf

d31e02e0-c8ab-4d3f-9cc9-0b8466bdabe8
read, write
nodeinfo - read this to get a series of NodeInfos (ending with a null empty record), write to this to restart the read statemachine that returns all the node infos

b56786c8-839a-44a1-b98e-a1724c4a0262
read,write
radio - read/write this to access a RadioConfig protobuf

6ff1d8b6-e2de-41e3-8c0b-8fa384f64eb6
read,write
owner - read/write this to access a User protobuf

Re: queue management
Not all messages are kept in the fromradio queue (filtered based on SubPacket):
* only the most recent Position and User messages for a particular node are kept
* all Data SubPackets are kept
* No WantNodeNum / DenyNodeNum messages are kept
A variable keepAllPackets, if set to true will suppress this behavior and instead keep everything for forwarding to the phone (for debugging)


## Other bluetooth services

This document focuses on the core mesh service, but it is worth noting that the following other Bluetooth services are also
provided by the device.

### BluetoothSoftwareUpdate

The software update service.  For a sample function that performs a software update using this API see [startUpdate](https://github.com/meshtastic/Meshtastic-Android/blob/master/app/src/main/java/com/geeksville/mesh/service/SoftwareUpdateService.kt).

SoftwareUpdateService UUID cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30

Characteristics

| UUID                                 | properties       | description|
|--------------------------------------|------------------|------------|
| e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e | write,read       | total image size, 32 bit, write this first, then read read back to see if it was acceptable (0 mean not accepted) |
| e272ebac-d463-4b98-bc84-5cc1a39ee517 | write            | data, variable sized, recommended 512 bytes, write one for each block of file |
| 4826129c-c22a-43a3-b066-ce8f0d5bacc6 | write            | crc32, write last - writing this will complete the OTA operation, now you can read result |
| 5e134862-7411-4424-ac4a-210937432c77 | read,notify      | result code, readable but will notify when the OTA operation completes |
| GATT_UUID_SW_VERSION_STR/0x2a28 | read | We also implement these standard GATT entries because SW update probably needs them: |
| GATT_UUID_MANU_NAME/0x2a29 | read | |
| GATT_UUID_HW_VERSION_STR/0x2a27 | read | |

### DeviceInformationService

Implements the standard BLE contract for this service (has software version, hardware model, serial number, etc...)

### BatteryLevelService

Implements the standard BLE contract service, provides battery level in a way that most client devices should automatically understand (i.e. it should show in the bluetooth devices screen automatically)