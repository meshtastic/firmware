
# Table of Contents
- [Table of Contents](#table-of-contents)
  - [Abstract](#abstract)
  - [Short term goals](#short-term-goals)
  - [Long term goals](#long-term-goals)
  - [Multiple Channel support / Security](#multiple-channel-support--security)
  - [On device API](#on-device-api)
  - [Efficient MQTT](#efficient-mqtt)
    - [Topics](#topics)
      - [Service Envelope](#service-envelope)
      - [NODEID](#nodeid)
      - [USERID](#userid)
      - [CHANNELID](#channelid)
    - [Gateway nodes](#gateway-nodes)
    - [Optional web services](#optional-web-services)
      - [Public MQTT broker service](#public-mqtt-broker-service)
      - [Riot.im messaging bridge](#riotim-messaging-bridge)
    - [Deprecated concepts](#deprecated-concepts)
      - [MESHID (deprecated)](#meshid-deprecated)
      - [DESTCLASS (deprecated)](#destclass-deprecated)
      - [DESTID (deprecated)](#destid-deprecated)
  - [Rejected idea: RAW UDP](#rejected-idea-raw-udp)
  - [Development plan](#development-plan)
    - [Cleanup/refactoring of existing codebase](#cleanuprefactoring-of-existing-codebase)
    - [Enhancements in following releases](#enhancements-in-following-releases)

## Abstract

This is a mini-doc/RFC sketching out a development plan to satisfy a number of 1.1 goals.

- [MQTT](https://opensource.com/article/18/6/mqtt) internet accessible API.  Issue #[369](https://github.com/meshtastic/Meshtastic-device/issues/169)
- An open API to easily run custom mini-apps on the devices
- A text messaging bridge when a node in the mesh can gateway to the internet. Issue #[353](https://github.com/meshtastic/Meshtastic-device/issues/353) and this nicely documented [android issue](https://github.com/meshtastic/Meshtastic-Android/issues/2).
- An easy way to let desktop app developers remotely control GPIOs. Issue #[182](https://github.com/meshtastic/Meshtastic-device/issues/182)
- Remote attribute access (to change settings of distant nodes). Issue #182

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

## Multiple Channel support / Security

Mini-apps API can bind to particular channels. They will only see messages sent on that channel.

During the 1.0 timeframe only one channel was supported per node.  Starting in the 1.1 tree we will do things like "remote admin operations / channel settings etc..." are on the "Control" channel and only especially trusted users should have the keys to access that channel.

FIXME - explain this more, talk about how useful for users and security domains.
- add channels as security
  - have a uplinkPolicy enum (none, up only, down only, updown, stay encrypted)
  - 
## On device API

For information on the related on-device API see [here](device-api.md).

## Efficient MQTT

Any gateway-device will contact the MQTT broker. 

### Topics

A sample [topic](https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/) hierarchy for a complete functioning mesh:

Gateway nodes will foward any MeshPacket from a local mesh channel with uplink_enabled.  The packet (encapsulated in a ServiceEnvelope).  The packets will remain encrypted with the key for the specified channel.
mesh/crypt/CHANNELID/NODEID/PORTID

For any channels in the local node with downlink_enabled, the gateway node will forward packets from MQTT to the local mesh.  It will do this by subscribing to mesh/crypt/CHANNELID/# and forwarding relevant packets.

If the channelid 'well known'/public it could be decrypted by a web service (if the web service was provided with the associated channel key), in which case it will be decrypted by a web service and appear at "mesh/clear/NODEID/PORTID". Note: This is not in the initial deliverable. 


FIXME, discuss how text message global mirroring could scale (or not)
FIXME, possibly don't global mirror text messages - instead rely on matrix/riot? 

#### Service Envelope

The payload published on mesh/... will always be wrapped in a [ServiceEnvelope protobuf](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/docs/docs.md#.ServiceEnvelope).

ServiceEnvelope will include the message, and full information about arrival time, who forwarded it, source channel, source mesh id, etc... 

#### NODEID

The unique ID for a node. A hex string that starts with a ! symbol.

#### USERID

A user ID string. This string is either a user ID if known or a nodeid to simply deliver the message to whoever the local user is of a particular device (i.e. person who might see the screen). FIXME, see what riot.im uses and perhaps use that convention? Or use the signal +phone number convention? Or the email addr?

#### CHANNELID

FIXME, figure out how channelids work

### Gateway nodes

Any meshtastic node that has a direct connection to the internet (either via a helper app or installed wifi/4G/satellite hardware) can function as a "Gateway node". 

Gateway nodes (via code running in the phone) will contain two tables to whitelist particular traffic to either be delivered toward the internet, or down toward the mesh. Users that are developing custom apps will be able to customize these filters/subscriptions.

Since multiple gateway nodes might be connected to a single mesh, it is possible that duplicate messages will be published on any particular topic.  Therefore subscribers to these topics should
deduplicate if needed by using the packet ID of each message.

### Optional web services

#### Public MQTT broker service

An existing public [MQTT broker](https://mosquitto.org/) will be the default for this service, but clients can use any MQTT broker they choose. 

FIXME - figure out how to avoid impersonation (because we are initially using a public mqtt server with no special security options).  FIXME, include some ideas on this in the ServiceEnvelope documentation.

#### Riot.im messaging bridge

@Geeksville will run a riot.im bridge that talks to the public MQTT broker and sends/receives into the riot.im network.

There is apparently [already](https://github.com/derEisele/tuple) a riot.im [bridge](https://matrix.org/bridges/) for MQTT. That will possibly need to be customized a bit. But by doing this, we should be able to let random riot.im users send/receive messages to/from any meshtastic device. (FIXME ponder security).  See this [issue](https://github.com/meshtastic/Meshtastic-Android/issues/2#issuecomment-645660990) with discussion with the dev.

### Deprecated concepts

You can ignore these for now...

#### MESHID (deprecated)

Earlier drafts of this document included the concept of a MESHID.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

A unique ID for this mesh. There will be some sort of key exchange process so that the mesh ID can not be impersonated by other meshes. 

#### DESTCLASS (deprecated)

Earlier drafts of this document included the concept of a DESTCLASS.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

The type of DESTID this message should be delivered to. A short one letter sequence:

| Symbol | Description                                                   |
| ------ | ------------------------------------------------------------- |
| R      | riot.im                                                       |
| L      | local mesh node ID or ^all                                    |
| A      | an application specific message, ID will be an APP ID         |
| S      | SMS gateway, DESTID is a phone number to reach via Twilio.com |
| E      | Emergency message, see bug #fixme for more context            |

#### DESTID (deprecated)

Earlier drafts of this document included the concept of a DESTCLASS.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

Can be...

- an internet username: kevinh@geeksville.com
- ^ALL for anyone
- An app ID (to allow apps out in the web to receive arbitrary binary data from nodes or simply other apps using meshtastic as a transport). They would connect to the MQTT broker and subscribe to their topic

## Rejected idea: RAW UDP
- FIXME explain why not UDP
  - need to have a server anyways so that nodes can reach each other from anywhere
  - raw UDP is dropped **very** agressively by many cellular providers
  - mqtt provides a nice/documented/standard security model to build upon
  
## Development plan

Given the previous problem/goals statement, here's the initial thoughts on the work items required.  As this idea becomes a bit more fully baked we should add details
on how this will be implemented and guesses at approximate work items.

### Cleanup/refactoring of existing codebase

- Change nodeIDs to be base64 instead of eight hex digits.
- DONE Refactor the position features into a position "mini-app". Use only the new public on-device API to implement this app.
- DONE Refactor the on device texting features into a messaging "mini-app". (Similar to the position mini-app)
- Add new multi channel concept
- Add portion of channelid to the raw lora packet header
- Confirm that we can now forward encrypted packets without decrypting at each node
- Use a channel named "remotehw" to secure the GPIO service.  If that channel is not found, don't even start the service.  Document this as the standard method for securing services.
- Add first cut of the "gateway node" code (i.e. MQTT broker client) to the python API (very little code needed for this component)
- Confirm that texting works to/from the internet
- Confirm that positions are optionally sent to the internet
- Add the first cut of the "gateway node" code to the android app (very little code needed for this component)

### Enhancements in following releases

FIXME, figure out rules for store and forward