# MQTT / remote attributes / on-device app API

This is a mini-doc/RFC sketching out a development plan to satisfy a number of 1.1 goals.

## Short term goals

- We want a clean API for novice developers to write mini "apps" that run **on the device** with the existing messaging/location "apps".
- We want the ability to have a gateway web service, so that if any node in the mesh can connect to the internet (via its connected phone app or directly) then that node will provide bidirectional messaging between nodes and the internet.
- We want an easy way for novice developers to remotely read and control GPIOs (because this is an often requested use case), without those developers having to write any device code.
- We want a way to gateway text messaging between our current private meshes and the broader internet (when that mesh is able to connect to the internet)
- We want a way to remotely set any device/channel parameter on a node. This is particularly important for administering physically inaccessible router nodes. Ideally this mechanism would also be used for administering the local node (so one common mechanism for both cases).
- This work should be independent of our current (semi-custom) LoRa transport, so that in the future we can swap out that transport if we wish (to QMesh or Reticulum?)
- Our networks are (usually) very slow and low bandwidth, so the messaging must be very airtime efficient.

## Long term goals

- Store and forward messaging should be supported, so apps can send messages that might be delivered to their destination in **hours** or **days** if a node/mesh was partitioned.

## Security

Mini-apps API can bind to particular channels. They will only see messages sent on that channel.

During the 1.1 timeframe only one channel is supported per node, but eventually we will do things like "remote admin operations / channel settings etc..." are on the "Control" channel and only especially trusted users should have the keys to access that channel. This means that during 1.1 you should assume that **any** user you grant access to your mesh (if technically knowledgeable enough) could change network settings. So you should still think of your meshes as private tools for friends. FIXME - how would this work with remote mqtt?

## Efficient MQTT

A gateway-device will contact the MQTT broker. For each operation it will use the meshtastic node ID as the MQTT "client ID".

### Topics

A sample [topic](https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/) heirarchy for a complete functioning mesh:

| Topic                                   | Description                                         |
| --------------------------------------- | --------------------------------------------------- |
| MESHID/NODEID/id/upd                    | A node ID update broadcast                          |
| MESHID/NODEID/pos/upd                   | A position update broadcast                         |
| MESHID/NODEID/pos/req                   | A position update request                           |
| MESHID/USERID/msg/text/DESTCLASS/DESTID | A text message from USERID to DESTCLASS/DESTID      |
| MESHID/NODEID/msg/bin/DESTCLASS/DESTID  | A binary message from NODEID to DESTCLASS/DESTCLASS |
| MESHID/NODEID/gpio/set/GPIONUM          | Set a GPIO output                                   |
| MESHID/NODEID/gpio/get/GPIONUM          | Try to read a GPIO                                  |
| MESHID/NODEID/gpio/upd/GPIONUM          | Contains the read GPIO value                        |
| MESHID/NODEID/attr/ATTRNAME/req         | Request a current attribute value                   |
| MESHID/NODEID/attr/ATTRNAME/set         | Set an attribute value                              |
| MESHID/NODEID/app/APPNUM/#              | An topic from an unregistered/unknown app           |

Gateway nodes (via code running in the phone) will contain two tables to whitelist particular traffic to either be delivered toward the internet, or down toward the mesh. Users that are developing custom apps will be able to customize these filters/subscriptions.

#### Default ToInternet filters

These filters are used to whitelist particular traffic - only traffic that matches a filter will be forwarded to the internet MQTT broker.

| Pattern          | Description                                                                |
| ---------------- | -------------------------------------------------------------------------- |
| +/+/id/#         | Only if set for 'no privacy'                                               |
| +/+/pos/upd      | Only if set for 'no privacy' - useful for showing all nodes on a world map |
| +/+/msg/text/W/+ | For internet messaging feature                                             |

#### Default FromInternet subscriptions

The gateway node will always subscribe to certain topics on the broker so that it can forward those topics into the mesh.

| Pattern               | Description                                                                     |
| --------------------- | ------------------------------------------------------------------------------- |
| MESHID/+/msg/text/W/+ | To receive text messages from the internet (where the sender knows our mesh ID) |
| +/+/msg/text/W/USERID | For each named user on the local mesh, to receive messages bound for that user  |

The provided example MQTT broker from Geeksville will also have filters:

#### MESHID

#### NODEID

#### USERID

#### DESTCLASS

Is used to filter whole classes of destination IDs (DESTID). Can be...

- L - Local, for this mesh only.
- W - World, for this mesh and the broader internet

#### DESTID

Can be...

- an internet username: kevinh@geeksville.com
- ^ALL for anyone
- An app ID (to allow apps out in the web to receive arbitrary binary data from nodes or simply other apps using meshtastic as a transport). They would connect to the MQTT broker and subscribe to their topic

### Named attribute API

### Name to ID mapping

MQTT topic strings are very long and potentially expensive over the slow LORA network. Also, we don't want to burden each (dumb) node in the mesh with having to regex match against them. For that reason, well known topics map to (small) "topic IDs". For portions of the topic that correspond to a wildcard, those strings are provided as "topic arguments". This means that only the phone app needs to consider full ... FIXME.

## Work items

### Cleanup/refactoring of existing codebase

- Refactor the position features into a position "mini-app". Use only the new public on-device API to implement this app.
- Refactor the on device texting features into a messaging "mini-app". (Similar to the position mini-app)

### New 'no-code-IOT' mini-app

Add a new 'remote GPIO/serial port/SPI/I2C access' mini-app. This new standard app would use the MQTT messaging layer to let users (devs that don't need to write device code) do basic (potentially dangerous) operations remotely.

#### Supported operations in the initial release

Initially supported features for no-code-IOT.

- Set any GPIO
- Read any GPIO

#### Supported operations eventually

General ideas for no-code IOT.

- Subscribe for notification of GPIO input status change (i.e. when pin goes low, send my app a message)
- Write/read N bytes over I2C/SPI bus Y (as one atomic I2C/SPI transaction)
- Send N bytes out serial port Z
- Subscribe for notification for when regex X matches the bytes that were received on serial port Z

### Later release features (1.2)

- Allow radios to be on multiple channels at once. Each channel will have its own encryption keys.

```

```
