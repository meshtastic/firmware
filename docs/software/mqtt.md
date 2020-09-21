
# 1. Table of Contents
- [1. Table of Contents](#1-table-of-contents)
  - [1.1. Abstract](#11-abstract)
  - [1.2. Short term goals](#12-short-term-goals)
  - [1.3. Long term goals](#13-long-term-goals)
  - [1.4. Security](#14-security)
  - [1.5. On device API](#15-on-device-api)
  - [1.6. Efficient MQTT](#16-efficient-mqtt)
    - [1.6.1. Topics](#161-topics)
      - [1.6.1.1. MESHID](#1611-meshid)
      - [1.6.1.2. NODEID](#1612-nodeid)
      - [1.6.1.3. DESTCLASS](#1613-destclass)
      - [1.6.1.4. DESTID](#1614-destid)
      - [1.6.1.5. USERID](#1615-userid)
    - [1.6.2. Gateway nodes](#162-gateway-nodes)
      - [1.6.2.1. Default ToInternet filters](#1621-default-tointernet-filters)
      - [1.6.2.2. Default FromInternet subscriptions](#1622-default-frominternet-subscriptions)
    - [1.6.3. Optional web services](#163-optional-web-services)
      - [1.6.3.1. Public MQTT broker service](#1631-public-mqtt-broker-service)
      - [1.6.3.2. Riot.im messaging bridge](#1632-riotim-messaging-bridge)
    - [1.6.4. Named attribute API](#164-named-attribute-api)
    - [1.6.5. Name to ID mapping](#165-name-to-id-mapping)
  - [1.7. Development plan](#17-development-plan)
    - [1.7.1. Cleanup/refactoring of existing codebase](#171-cleanuprefactoring-of-existing-codebase)
    - [1.7.2. New 'no-code-IOT' mini-app](#172-new-no-code-iot-mini-app)
      - [1.7.2.1. Supported operations in the initial release](#1721-supported-operations-in-the-initial-release)
      - [1.7.2.2. Supported operations eventually](#1722-supported-operations-eventually)
    - [1.7.3. Later release features (1.2)](#173-later-release-features-12)

## 1.1. Abstract

This is a mini-doc/RFC sketching out a development plan to satisfy a number of 1.1 goals.

- [MQTT](https://opensource.com/article/18/6/mqtt) internet accessible API.  Issue #[369](https://github.com/meshtastic/Meshtastic-device/issues/169)
- An open API to easily run custom mini-apps on the devices
- A text messaging bridge when a node in the mesh can gateway to the internet. Issue #[353](https://github.com/meshtastic/Meshtastic-device/issues/353)
- An easy way to let desktop app developers remotely control GPIOs. Issue #[182](https://github.com/meshtastic/Meshtastic-device/issues/182)
- Remote attribute access (to change settings of distant nodes). Issue #182

## 1.2. Short term goals

- We want a clean API for novice developers to write mini "apps" that run **on the device** with the existing messaging/location "apps".
- We want the ability to have a gateway web service, so that if any node in the mesh can connect to the internet (via its connected phone app or directly) then that node will provide bidirectional messaging between nodes and the internet.
- We want an easy way for novice developers to remotely read and control GPIOs (because this is an often requested use case), without those developers having to write any device code.
- We want a way to gateway text messaging between our current private meshes and the broader internet (when that mesh is able to connect to the internet)
- We want a way to remotely set any device/channel parameter on a node. This is particularly important for administering physically inaccessible router nodes. Ideally this mechanism would also be used for administering the local node (so one common mechanism for both cases).
- This work should be independent of our current (semi-custom) LoRa transport, so that in the future we can swap out that transport if we wish (to QMesh or Reticulum?)
- Our networks are (usually) very slow and low bandwidth, so the messaging must be very airtime efficient.

## 1.3. Long term goals

- Store and forward messaging should be supported, so apps can send messages that might be delivered to their destination in **hours** or **days** if a node/mesh was partitioned.

## 1.4. Security

Mini-apps API can bind to particular channels. They will only see messages sent on that channel.

During the 1.1 timeframe only one channel is supported per node, but eventually we will do things like "remote admin operations / channel settings etc..." are on the "Control" channel and only especially trusted users should have the keys to access that channel.

See "Named Attribute API" section for special access control to prevent remote access to device settings.

## 1.5. On device API

FIXME - add an example of the on-device API.  Possibly by showing the new position or texting code.

## 1.6. Efficient MQTT

A gateway-device will contact the MQTT broker. For each operation it will use the meshtastic "MESHID/NODEID" tuple as the MQTT "client ID". MESHIDs will be (TBD somehow) tracked and authenticated out-of-band.

### 1.6.1. Topics

A sample [topic](https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/) hierarchy for a complete functioning mesh:

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

#### 1.6.1.1. MESHID

A unique ID for this mesh. There will be some sort of key exchange process so that the mesh ID can not be impersonated by other meshes.

#### 1.6.1.2. NODEID

The unique ID for a node. A hex string that starts with a ! symbol.

#### 1.6.1.3. DESTCLASS

The type of DESTID this message should be delivered to. A short one letter sequence:

| Symbol | Description                                                   |
| ------ | ------------------------------------------------------------- |
| R      | riot.im                                                       |
| L      | local mesh node ID or ^all                                    |
| A      | an application specific message, ID will be an APP ID         |
| S      | SMS gateway, DESTID is a phone number to reach via Twilio.com |
| E      | Emergency message, see bug #fixme for more context            |

#### 1.6.1.4. DESTID

Can be...

- an internet username: kevinh@geeksville.com
- ^ALL for anyone
- An app ID (to allow apps out in the web to receive arbitrary binary data from nodes or simply other apps using meshtastic as a transport). They would connect to the MQTT broker and subscribe to their topic

#### 1.6.1.5. USERID

A user ID string. This string is either a user ID if known or a nodeid to simply deliver the message to whoever the local user is of a particular device (i.e. person who might see the screen). FIXME, see what riot.im uses and perhaps use that convention? Or use the signal +phone number convention? Or the email addr?

### 1.6.2. Gateway nodes

Any meshtastic node that has a direct connection to the internet (either via a helper app or installed wifi/4G/satellite hardware) can function as a "Gateway node". 

Gateway nodes (via code running in the phone) will contain two tables to whitelist particular traffic to either be delivered toward the internet, or down toward the mesh. Users that are developing custom apps will be able to customize these filters/subscriptions.

#### 1.6.2.1. Default ToInternet filters

These filters are used to whitelist particular traffic - only traffic that matches a filter will be forwarded to the internet MQTT broker.

| Pattern          | Description                                                                                                                |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------- |
| +/+/id/#         | Only if set for 'no privacy'                                                                                               |
| +/+/pos/upd      | Only if set for 'no privacy' - useful for showing all nodes on a world map                                                 |
| +/+/msg/text/W/+ | For internet messaging feature                                                                                             |
| +/+/app/APPNUM/# | Only if "send app APPNUM" has been set in gateway settings - for app developers who want their traffic routed to the world |

#### 1.6.2.2. Default FromInternet subscriptions

The gateway node will always subscribe to certain topics on the broker so that it can forward those topics into the mesh.

| Pattern               | Description                                                                     |
| --------------------- | ------------------------------------------------------------------------------- |
| MESHID/+/msg/text/W/+ | To receive text messages from the internet (where the sender knows our mesh ID) |
| +/+/msg/text/W/USERID | For each named user on the local mesh, to receive messages bound for that user  |

### 1.6.3. Optional web services

#### 1.6.3.1. Public MQTT broker service

@Geeksville will provide a standard [MQTT broker](https://mosquitto.org/) on the web to facilitate use of this service, but clients can use any MQTT broker they choose. Geeksville will initially not charge for the use of this broker, but if it becomes a burden he might ask for donations or require a payment for the use of the server.  

The provided public MQTT broker from geeksville.com will also have filters to ensure:

- only authenticated MESHIDs can publish under that ID
- messages sent/to from the riot.im text message bridge can only be seen by that bridge or the intended destination/source mesh

Is used to filter whole classes of destination IDs (DESTID). Can be...

- L - Local, for this mesh only.
- W - World, for this mesh and the broader internet

#### 1.6.3.2. Riot.im messaging bridge

@Geeksville will run a riot.im bridge that talks to the public MQTT broker and sends/receives into the riot.im network.

There is apparently already a riot.im [bridge](https://matrix.org/bridges/) for MQTT. That will possibly need to be customized a bit. But by doing this, we should be able to let random riot.im users send/receive messages to/from any meshtastic device. (FIXME add link and ponder security)

### 1.6.4. Named attribute API

The current channel and node settings are set/read using a special protobuf exchange between a local client and the meshtastic device.  In version 1.1 that mechanism will be changed so that settings are set/read using MQTT (even when done locally).  This will enable remote node adminstration (even conceivably through the internet).

To provide some basic security a new named attribute name "seckey" can be set.  If set, any attribute operations must include that get with their operation request. Note: This mechanism still assumes that users you grant permission to access your local mesh are not 'adversaries'.  A technically competent user could discover the remote attribute key needed for attribute reading/writing.  In the 1.2ish timeframe we will add the concept of multiple channels and in that case, remote attribute operations will be on their own secured channel that regular 'users' can not see.

### 1.6.5. Name to ID mapping

MQTT topic strings are very long and potentially expensive over the slow LORA network. Also, we don't want to burden each (dumb) node in the mesh with having to regex match against them. For that reason, well known topics map to (small) "topic IDs". For portions of the topic that correspond to a wildcard, those strings are provided as "topic arguments". This means that only the phone app needs to consider full topic strings.  Device nodes will only understand integer topic IDs and their arguments.

FIXME, add more details to this section and figure out how unassigned apps/topics work in this framework.

## 1.7. Development plan

Given the previous problem/goals statement, here's the initial thoughts on the work items required.  As this idea becomes a bit more fully baked we should add details
on how this will be implemented and guesses at approximate work items.

### 1.7.1. Cleanup/refactoring of existing codebase

- Change nodeIDs to be base64 instead of eight hex digits.
- Add the concept of topic IDs and topic arguments to the protobufs and the device code.
- Refactor the position features into a position "mini-app". Use only the new public on-device API to implement this app.
- Refactor the on device texting features into a messaging "mini-app". (Similar to the position mini-app)
- Add first cut of the "gateway node" code (i.e. MQTT broker client) to the python API (very little code needed for this component)
- Confirm that texting works to/from the internet
- Confirm that positions are optionally sent to the internet
- Add the first cut of the "gateway node" code to the android app (very little code needed for this component)

### 1.7.2. New 'no-code-IOT' mini-app

Add a new 'remote GPIO/serial port/SPI/I2C access' mini-app. This new standard app would use the MQTT messaging layer to let users (developers that don't need to write device code) do basic (potentially dangerous) operations remotely.

#### 1.7.2.1. Supported operations in the initial release

Initially supported features for no-code-IOT.

- Set any GPIO
- Read any GPIO

#### 1.7.2.2. Supported operations eventually

General ideas for no-code IOT.

- Subscribe for notification of GPIO input status change (i.e. when pin goes low, send my app a message)
- Write/read N bytes over I2C/SPI bus Y (as one atomic I2C/SPI transaction)
- Send N bytes out serial port Z
- Subscribe for notification for when regex X matches the bytes that were received on serial port Z

### 1.7.3. Later release features (1.2)

- Allow radios to be on multiple channels at once. Each channel will have its own encryption keys.


