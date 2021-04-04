
# Table of Contents
- [Table of Contents](#table-of-contents)
  - [Abstract](#abstract)
  - [Short term goals](#short-term-goals)
  - [Long term goals](#long-term-goals)
  - [Multiple Channel support / Security](#multiple-channel-support--security)
  - [On device API](#on-device-api)
  - [MQTT transport](#mqtt-transport)
    - [Topics](#topics)
      - [Service Envelope](#service-envelope)
      - [NODEID](#nodeid)
      - [USERID](#userid)
      - [CHANNELID](#channelid)
      - [PORTID](#portid)
    - [Gateway nodes](#gateway-nodes)
    - [MQTTSimInterface](#mqttsiminterface)
  - [Web services](#web-services)
    - [Public MQTT broker service](#public-mqtt-broker-service)
      - [Broker selection](#broker-selection)
    - [Admin service](#admin-service)
    - [Riot.im messaging bridge](#riotim-messaging-bridge)
  - [Deprecated concepts](#deprecated-concepts)
    - [MESHID (deprecated)](#meshid-deprecated)
    - [DESTCLASS (deprecated)](#destclass-deprecated)
    - [DESTID (deprecated)](#destid-deprecated)
  - [Rejected idea: RAW UDP](#rejected-idea-raw-udp)
  - [Development plan](#development-plan)
    - [Work items](#work-items)
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

## On device API

For information on the related on-device API see [here](device-api.md).

## MQTT transport

Any gateway-device will contact the MQTT broker. 

### Topics

* The "/mesh/crypt/CHANNELID/NODEID" [topic](https://www.hivemq.com/blog/mqtt-essentials-part-5-mqtt-topics-best-practices/) will be used for (encrypted) messages sent from/to a mesh.

Gateway nodes will foward any MeshPacket from a local mesh channel with uplink_enabled.  The packet (encapsulated in a ServiceEnvelope) will remain encrypted with the key for the specified channel.  

For any channels in the gateway node with downlink_enabled, the gateway node will forward packets from MQTT to the local mesh.  It will do this by subscribing to mesh/crypt/CHANNELID/# and forwarding relevant packets.

* If the channelid 'well known'/public it could be decrypted by a web service (if the web service was provided with the associated channel key), in which case it will be decrypted by a web service and appear at "mesh/clear/CHANNELID/NODEID/PORTID". Note: This is not in the initial deliverable. 

FIXME, consider how text message global mirroring could scale (or not)
FIXME, possibly don't global mirror text messages - instead rely on matrix/riot? 
FIXME, consider possible attacks by griefers and how they can be prvented

* The "/mesh/stat/NODEID" topic contains a simple string showing connection status of nodes.  We rely on the MQTT feature for automatically publishing special failrue messages to this topic when the device disconnects.

#### Service Envelope

The payload published on mesh/... will always be wrapped in a [ServiceEnvelope protobuf](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/docs/docs.md#.ServiceEnvelope).

ServiceEnvelope will include the message, and full information about arrival time, who forwarded it, source channel, source mesh id, etc... 

#### NODEID

The unique ID for a node. A 8 byte (16 character) hex string that starts with a ! symbol.

#### USERID

A user ID string. This string is either a user ID if known or a nodeid to simply deliver the message to whoever the local user is of a particular device (i.e. person who might see the screen). FIXME, see what riot.im uses and perhaps use that convention? Or use the signal +phone number convention? Or the email addr?

#### CHANNELID

For the time being we simply use the local "channel name" - which is not quite good enough.

FIXME, figure out how channelids work in more detail.  They should generally be globally unique, but this is not a requirement.  If someone accidentially (or maliciously) sends data using a channel ID they do not 'own' they will still lacking a valid AES256 encryption, so it will be ignored by others.

idea to be pondered: When the user clicks to enable uplink/downlink check the name they entered and 'claim' it on the server?

#### PORTID

Portid is used to descriminated between different packet types which are sent over a channel.  As used here it is an integer typically (but not necessarily) chosen from portnums.proto.

### Gateway nodes

Any meshtastic node that has a direct connection to the internet (either via a helper app or installed wifi/4G/satellite hardware) can function as a "Gateway node". 

Gateway nodes (via code running in the phone) will contain two tables to whitelist particular traffic to either be delivered toward the internet, or down toward the mesh. Users that are developing custom apps will be able to customize these filters/subscriptions.

Since multiple gateway nodes might be connected to a single mesh, it is possible that duplicate messages will be published on any particular topic.  Therefore subscribers to these topics should
deduplicate if needed by using the packet ID of each message.


### MQTTSimInterface

This is a bit orthogonal from the main MQTT feature set, but a special simulated LoRa interface called MQTTSimInterface uses the
MQTT messaging infrastructure to send "LoRa" packets between simulated nodes running on Linux.  This allows us to test radio topologies and code without having to use real hardware.

This service uses the standard mesh/crypt/... topic, but it picks a special CHANNEL_ID.  That CHANNEL_ID is typcially of the form "simmesh_xxx".  

FIXME: Figure out how to secure the creation and use of well known CHANNEL_IDs.


## Web services

### Public MQTT broker service

An existing public [MQTT broker](https://mosquitto.org/) will be the default for this service, but clients can use any MQTT broker they choose. 

FIXME - figure out how to avoid impersonation (because we are initially using a public mqtt server with no special security options).  FIXME, include some ideas on this in the ServiceEnvelope documentation.

#### Broker selection

On a previous project I used mosqitto, which I liked, but the admin interface for programmatically managing access was ugly.  [This](https://www.openlogic.com/blog/activemq-vs-rabbitmq) article makes me think RabbitMQ might be best for us.

Initially I will try to avoid using any non MQTT broker/library/API

### Admin service

(This is a WIP draft collection of not complete ideas)

The admin service deals with misc global arbibration/access tasks.  It is actually reached **through** the MQTT broker, though for security we depend on that broker having a few specialized rules about who can post to or see particular topics (see below).

Topics:

* mesh/ta/# - all requests going towards the admin server (only the admin server can see this topic)
* mesh/tn/NODEID/# - all responses/requests going towards a particlar gateway node (only this particular gateway node is allowed to see this topic)
* mesh/to/NODEID/# - unsecured messages sent to a gateway node (any attacker can see this topic) - used only for "request gateway id" responses
* mesh/ta/toadmin - a request to the admin server, payload is a ToAdmin protobuf
* mesh/tn/NODEID/tonode - a request/response to a particular gateway node.  payload is a ToNode protobuf

Operations provided via the ToAdmin/ToNode protocol:

* Register a global channel ID (request a new channel ID).  Optionally include the AES key if you would like the web service to automatically decrypt in the cloud
* Request gateway ID - the response is used to re-sign in to the broker.  

Possibly might need public key encryption for the gateway request?  Since the response is sent to the mesh/to endpoint?  I would really like to use MQTT for all comms so I don't need yet another protocol/port from the device.

Idea 1: A gateway ID/signin can only be assigned once per node ID.  If a user loses their signin info, they'll need to change their node number.  yucky.
Idea 2: Instead gateway signins are assigned at "manufacture" time (and if lost, yes the user would need to "remanufacture" their node).  Possibly a simple web service (which can be accessed via the python install script?) that goes to an https endpoint, gets signin info (and server keeps a copy) and stores it in the device.  Hardware manufacturers could ask for N gateway IDs via the same API and get back a bunch of small files that could be programmed on each device.  Would include node id, etc...  Investigate alternatives like storing a particular private key to allow each device to generate their own signin key and the server would trust it by checking against a public key?

TODO/FIXME: look into mqtt broker options, possibly find one with better API support than mosquitto?

### Riot.im messaging bridge

@Geeksville will run a riot.im bridge that talks to the public MQTT broker and sends/receives into the riot.im network.

There is apparently [already](https://github.com/derEisele/tuple) a riot.im [bridge](https://matrix.org/bridges/) for MQTT. That will possibly need to be customized a bit. But by doing this, we should be able to let random riot.im users send/receive messages to/from any meshtastic device. (FIXME ponder security).  See this [issue](https://github.com/meshtastic/Meshtastic-Android/issues/2#issuecomment-645660990) with discussion with the dev.

## Deprecated concepts

You can ignore these for now...

### MESHID (deprecated)

Earlier drafts of this document included the concept of a MESHID.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

A unique ID for this mesh. There will be some sort of key exchange process so that the mesh ID can not be impersonated by other meshes. 

### DESTCLASS (deprecated)

Earlier drafts of this document included the concept of a DESTCLASS.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

The type of DESTID this message should be delivered to. A short one letter sequence:

| Symbol | Description                                                   |
| ------ | ------------------------------------------------------------- |
| R      | riot.im                                                       |
| L      | local mesh node ID or ^all                                    |
| A      | an application specific message, ID will be an APP ID         |
| S      | SMS gateway, DESTID is a phone number to reach via Twilio.com |
| E      | Emergency message, see bug #fixme for more context            |

### DESTID (deprecated)

Earlier drafts of this document included the concept of a DESTCLASS.  That concept has been removed for now, but might be useful in the future.  The old idea is listed below:

Can be...

- an internet username: kevinh@geeksville.com
- ^ALL for anyone
- An app ID (to allow apps out in the web to receive arbitrary binary data from nodes or simply other apps using meshtastic as a transport). They would connect to the MQTT broker and subscribe to their topic

## Rejected idea: RAW UDP

A number of commenters have requested/proposed using UDP for the transport.  We've considered this option and decided to use MQTT instead for the following reasons:

  - Most UDP uses cases would need to have a server anyways so that nodes can reach each other from anywhere (i.e. if most gateways will be behind some form of NAT which would need to be tunnelled)
  - Raw UDP is dropped **very** agressively by many cellular providers.  MQTT from the gateway to a broker can be done over a TCP connection for this reason.
  - MQTT provides a nice/documented/standard security model to build upon
  - MQTT is fairly wire efficient with multiple broker implementations/providers and numerous client libraries for any language.  The actual implementation of MQTT is quite simple.
  
## Development plan

Given the previous problem/goals statement, here's the initial thoughts on the work items required.  As this idea becomes a bit more fully baked we should add details
on how this will be implemented and guesses at approximate work items.

### Work items

- Change nodeIDs to be base64 instead of eight hex digits.
- DONE Refactor the position features into a position "mini-app". Use only the new public on-device API to implement this app.
- DONE Refactor the on device texting features into a messaging "mini-app". (Similar to the position mini-app)
- Add new multi channel concept
- Send new channels to python client
- Let python client add channels
- Add portion of channelid to the raw lora packet header
- Confirm that we can now forward encrypted packets without decrypting at each node
- Use a channel named "remotehw" to secure the GPIO service.  If that channel is not found, don't even start the service.  Document this as the standard method for securing services.
- Add first cut of the "gateway node" code (i.e. MQTT broker client) to the python API (very little code needed for this component)
- Confirm that texting works to/from the internet
- Confirm that positions are optionally sent to the internet
- Add the first cut of the "gateway node" code to the android app (very little code needed for this component)

### Enhancements in following releases

The initial gateway will be added to the python tool.  But the gateway implementation is designed to be fairly trivial/dumb.  After the initial release the actual gateway code can be ported to also run inside of the android app.  In fact, we could have ESP32 based nodes include a built-in "gateway node" implementation.

Store and forward could be added so that nodes on the mesh could deliver messages (i.e. text messages) on an "as possible" basis.  This would allow things like "hiker sends a message to friend - mesh can not currently reach friend - eventually (days later) mesh can somehow reach friend, message gets delivered"
