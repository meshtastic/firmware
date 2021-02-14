# About

This is a work in progress and is not yet available.

The Store Request Plugin is an implementation of a Store and Forward system to enable resilient messaging in the event that a client device is disconnected from the main network.

Because of the increased network traffic for this overhead, it's not adviced to use this if you are duty cycle limited for your airtime usage nor is it adviced to use this for SF12 (Long range but Slow).

# Requirements

Initial Requirements:

* Must be installed on a router node.
* * This is an artificial limitation, but is in place to enforce best practices.
* * Router nodes are intended to be always online. If this plugin misses any messages, the reliability of the stored messages will be reduced
* Esp32 Processor based device with external PSRAM. (tbeam v1.0 and tbeamv1.1, maybe others)

# Implementation timeline

Not necessarily in this order:

UC 1) MVP - automagically forward packets to a client that may have missed packets.

UC 2) Client Interface (Web, Android, Python or iOS when that happens) to request packets be resent

UC 3) router sends a periodic “heartbeat” to let the clients know they’re part of the main mesh

UC 4) support for a mesh to have multiple routers that have the store & forward functionality (for redundancy)

# Things to consider

Not all these cases will be initially implemented. It's just a running stream of thoughts to be considered.

## Main Mesh Network with Router

The store and forward plugin is intended to be enabled on a router that designates your "main" mesh network.

## Store and Forward on Multiple Routers

If multiple routers with the plugin are enabled, they should be able to share their stored database amongst each other. This enable resilliancy from one router going offline.

## Fragmented networks - No router

In this case, the mesh network has been fragmented by two client devices leaving the main network.

If two Meshtastic devices walk away from the main mesh, they will be able to message each other but not message the main network. When they return to the main network, they will receive the messages they have missed from the main mesh network.

## Fragmented network - With routers

In this case, we have two routers separate by a great distance, each serving multiple devices. One of the routers have gone offline. This has now created two physically seaprated mesh networks using the same channel configuration.

Q: How do we rejoin both fragmented networks? Do we care about messages that were unrouted between fagments?

# Router Data Structures

Structure of received messages:

    receivedMessages
      Port_No
      packetID
      to
      from
      rxTimeMsec
      data

Structure of nodes and last time we heard from them. This is a record of any packet type.

    senderRecord
      From
      rxTimeMsec

# General Operation for UC1 - automagically forward packets to a client that may have missed packets

On every handled packet
* Record the sender from and the time we heard from that sender.

On every handled packet

* If the packet is a message, save the messsage into receivedMessages

On every handled packet, if we have not heard from that sender in a period of time greater than timeAway, let's assume that they have been away from the network.

* In this case, we will resend them all the messages they have missed since they were gone

## Expected problems this implementation

* If the client has been away for less than 5 minutes and has received the previously sent message, the client will gracefully ignore it. This is thanks to PacketHistory::wasSeenRecently in PacketHistory.cpp.
* * If the client has been away for more than 5 minutes and we resend packets that they have already received, it's possible they will see duplicate messages. This should be unlikely but is still possible. 

