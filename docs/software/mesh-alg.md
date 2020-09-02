# Mesh broadcast algorithm

## Current algorithm

The routing protocol for Meshtastic is really quite simple (and suboptimal). It is heavily influenced by the mesh routing algorithm used in [Radiohead](https://www.airspayce.com/mikem/arduino/RadioHead/) (which was used in very early versions of this project). It has four conceptual layers.

### A note about protocol buffers

Because we want our devices to work across various vendors and implementations, we use [Protocol Buffers](https://github.com/meshtastic/Meshtastic-protobufs) pervasively. For information on how the protocol buffers are used wrt API clients see [sw-design](sw-design.md), for purposes of this document you mostly only
need to consider the MeshPacket and Subpacket message types.

### Layer 1: Non reliable zero hop messaging

This layer is conventional non-reliable lora packet transmission. The transmitted packet has the following representation on the ether:

- A 32 bit LORA preamble (to allow receiving radios to synchronize clocks and start framing). We use a longer than minimum (8 bit) preamble to maximize the amount of time the LORA receivers can stay asleep, which dramatically lowers power consumption.

After the preamble the 16 byte packet header is transmitted. This header is described directly by the PacketHeader class in the C++ source code. But indirectly it matches the first portion of the "MeshPacket" protobuf definition. But notably: this portion of the packet is sent directly as the following 16 bytes (rather than using the protobuf encoding). We do this to both save airtime and to allow receiving radio hardware the option of filtering packets before even waking the main CPU.

- to (4 bytes): the unique NodeId of the destination (or 0xffffffff for NodeNum_BROADCAST)
- from (4 bytes): the unique NodeId of the sender)
- id (4 bytes): the unique (wrt the sending node only) packet ID number for this packet. We use a large (32 bit) packet ID to ensure there is enough unique state to protect any encrypted payload from attack.
- flags (4 bytes): Only a few bits are are currently used - 3 bits for for the "HopLimit" (see below) and 1 bit for "WantAck"

After the packet header the actual packet is placed onto the the wire. These bytes are merely the encrypted packed protobuf encoding of the SubPacket protobuf. A full description of our encryption is available in [crypto](crypto.md). It is worth noting that only this SubPacket is encrypted, headers are not. Which leaves open the option of eventually allowing nodes to route packets without knowing the keys used to encrypt.

NodeIds are constructed from the bottom four bytes of the macaddr of the bluetooth address. Because the OUI is assigned by the IEEE and we currently only support a few CPU manufacturers, the upper byte is defacto guaranteed unique for each vendor. The bottom 3 bytes are guaranteed unique by that vendor.

To prevent collisions all transmitters will listen before attempting to send. If they hear some other node transmitting, they will reattempt transmission in x milliseconds. This retransmission delay is random between FIXME and FIXME (these two numbers are currently hardwired, but really should be scaled based on expected packet transmission time at current channel settings).

### Layer 2: Reliable zero hop messaging

This layer adds reliable messaging between the node and its immediate neighbors (only).

The default messaging provided by layer-1 is extended by setting the "want-ack" flag in the MeshPacket protobuf. If want-ack is set the following documentation from mesh.proto applies:

"""This packet is being sent as a reliable message, we would prefer it to arrive
at the destination. We would like to receive a ack packet in response.

Broadcasts messages treat this flag specially: Since acks for broadcasts would
rapidly flood the channel, the normal ack behavior is suppressed. Instead,
the original sender listens to see if at least one node is rebroadcasting this
packet (because naive flooding algorithm). If it hears that the odds (given
typical LoRa topologies) the odds are very high that every node should
eventually receive the message. So FloodingRouter.cpp generates an implicit
ack which is delivered to the original sender. If after some time we don't
hear anyone rebroadcast our packet, we will timeout and retransmit, using the
regular resend logic."""

If a transmitting node does not receive an ACK (or a NAK) packet within FIXME milliseconds, it will use layer-1 to attempt a retransmission of the sent packet. A reliable packet (at this 'zero hop' level) will be resent a maximum of three times. If no ack or nak has been received by then the local node will internally generate a nak (either for local consumption or use by higher layers of the protocol).

### Layer 3: (Naive) flooding for multi-hop messaging

Given our use-case for the initial release, most of our protocol is built around [flooding](<https://en.wikipedia.org/wiki/Flooding_(computer_networking)>). The implementation is currently 'naive' - i.e. it doesn't try to optimize flooding other than abandoning retransmission once we've seen a nearby receiver has acked the packet. Therefore, for each source packet up to N retransmissions might occur (if there are N nodes in the mesh).

Each node in the mesh, if it sees a packet on the ether with HopLimit set to a value other than zero, it will decrement that HopLimit and attempt retransmission on behalf of the original sending node.

### Layer 4: DSR for multi-hop unicast messaging

This layer is not yet fully implemented (and not yet used). But eventually (if we stay with our own transport rather than switching to QMesh or Reticulum)
we will use conventional DSR for unicast messaging. Currently (even when not requiring 'broadcasts') we send any multi-hop unicasts as 'broadcasts' so that we can
leverage our (functional) flooding implementation. This is suboptimal but it is a very rare use-case, because the odds are high that most nodes (given our small networks and 'hiking' use case) are within a very small number of hops. When any node witnesses an ack for a packet, it will realize that it can abandon its own
broadcast attempt for that packet.

## Misc notes on remaining tasks

This section is currently poorly formatted, it is mostly a mere set of todo lists and notes for @geeksville during his initial development. After release 1.0 ideas for future optimization include:

- Make flood-routing less naive (because we have GPS and radio signal strength as heuristics to avoid redundant retransmissions)
- If nodes have been user marked as 'routers', preferentially do flooding via those nodes
- Fully implement DSR to improve unicast efficiency (or switch to QMesh/Reticulum as these projects mature)

great source of papers and class notes: http://www.cs.jhu.edu/~cs647/

flood routing improvements

- DONE if we don't see anyone rebroadcast our want_ack=true broadcasts, retry as needed.

reliable messaging tasks (stage one for DSR):

- DONE generalize naive flooding
- DONE add a max hops parameter, use it for broadcast as well (0 means adjacent only, 1 is one forward etc...). Store as three bits in the header.
- DONE add a 'snoopReceived' hook for all messages that pass through our node.
- DONE use the same 'recentmessages' array used for broadcast msgs to detect duplicate retransmitted messages.
- DONE in the router receive path?, send an ack packet if want_ack was set and we are the final destination. FIXME, for now don't handle multihop or merging of data replies with these acks.
- DONE keep a list of packets waiting for acks
- DONE for each message keep a count of # retries (max of three). Local to the node, only for the most immediate hop, ignorant of multihop routing.
- DONE delay some random time for each retry (large enough to allow for acks to come in)
- DONE once an ack comes in, remove the packet from the retry list and deliver the ack to the original sender
- DONE after three retries, deliver a no-ack packet to the original sender (i.e. the phone app or mesh router service)
- DONE test one hop ack/nak with the python framework
- DONE Do stress test with acks

dsr tasks

- DONE oops I might have broken message reception
- DONE Don't use broadcasts for the network pings (close open github issue)
- DONE add ignoreSenders to radioconfig to allow testing different mesh topologies by refusing to see certain senders
- DONE test multihop delivery with the python framework

optimizations / low priority:

- read this [this](http://pages.cs.wisc.edu/~suman/pubs/nadv-mobihoc05.pdf) paper and others and make our naive flood routing less naive
- read @cyclomies long email with good ideas on optimizations and reply
- DONE Remove NodeNum assignment algorithm (now that we use 4 byte node nums)
- DONE make android app warn if firmware is too old or too new to talk to
- change nodenums and packetids in protobuf to be fixed32
- low priority: think more careful about reliable retransmit intervals
- make ReliableRouter.pending threadsafe
- bump up PacketPool size for all the new ack/nak/routing packets
- handle 51 day rollover in doRetransmissions
- use a priority queue for the messages waiting to send. Send acks first, then routing messages, then data messages, then broadcasts?

when we send a packet

- do "hop by hop" routing
- when sending, if destnodeinfo.next_hop is zero (and no message is already waiting for an arp for that node), startRouteDiscovery() for that node. Queue the message in the 'waiting for arp queue' so we can send it later when then the arp completes.
- otherwise, use next_hop and start sending a message (with ack request) towards that node (starting with next_hop).

when we receive any packet

- sniff and update tables (especially useful to find adjacent nodes). Update user, network and position info.
- if we need to route() that packet, resend it to the next_hop based on our nodedb.
- if it is broadcast or destined for our node, deliver locally
- handle routereply/routeerror/routediscovery messages as described below
- then free it

routeDiscovery

- if we've already passed through us (or is from us), then it ignore it
- use the nodes already mentioned in the request to update our routing table
- if they were looking for us, send back a routereply
- NOT DOING FOR NOW -if max_hops is zero and they weren't looking for us, drop (FIXME, send back error - I think not though?)
- if we receive a discovery packet, and we don't have next_hop set in our nodedb, we use it to populate next_hop (if needed) towards the requester (after decrementing max_hops)
- if we receive a discovery packet, and we have a next_hop in our nodedb for that destination we send a (reliable) we send a route reply towards the requester

when sending any reliable packet

- if timeout doing retries, send a routeError (nak) message back towards the original requester. all nodes eavesdrop on that packet and update their route caches.

when we receive a routereply packet

- update next_hop on the node, if the new reply needs fewer hops than the existing one (we prefer shorter paths). fixme, someday use a better heuristic

when we receive a routeError packet

- delete the route for that failed recipient, restartRouteDiscovery()
- if we receive routeerror in response to a discovery,
- fixme, eventually keep caches of possible other routes.

TODO:

- optimize our generalized flooding with heuristics, possibly have particular nodes self mark as 'router' nodes.

- DONE reread the radiohead mesh implementation - hop to hop acknowledgement seems VERY expensive but otherwise it seems like DSR
- DONE read about mesh routing solutions (DSR and AODV)
- DONE read about general mesh flooding solutions (naive, MPR, geo assisted)
- DONE reread the disaster radio protocol docs - seems based on Babel (which is AODVish)
- REJECTED - seems dying - possibly dash7? https://www.slideshare.net/MaartenWeyn1/dash7-alliance-protocol-technical-presentation https://github.com/MOSAIC-LoPoW/dash7-ap-open-source-stack - does the opensource stack implement multihop routing? flooding? their discussion mailing list looks dead-dead
- update duty cycle spreadsheet for our typical usecase

a description of DSR: https://tools.ietf.org/html/rfc4728 good slides here: https://www.slideshare.net/ashrafmath/dynamic-source-routing
good description of batman protocol: https://www.open-mesh.org/projects/open-mesh/wiki/BATMANConcept

interesting paper on lora mesh: https://portal.research.lu.se/portal/files/45735775/paper.pdf
It seems like DSR might be the algorithm used by RadioheadMesh. DSR is described in https://tools.ietf.org/html/rfc4728
https://en.wikipedia.org/wiki/Dynamic_Source_Routing

broadcast solution:
Use naive flooding at first (FIXME - do some math for a 20 node, 3 hop mesh. A single flood will require a max of 20 messages sent)
Then move to MPR later (http://www.olsr.org/docs/report_html/node28.html). Use altitude and location as heursitics in selecting the MPR set

compare to db sync algorithm?

what about never flooding gps broadcasts. instead only have them go one hop in the common case, but if any node X is looking at the position of Y on their gui, then send a unicast to Y asking for position update. Y replies.

If Y were to die, at least the neighbor nodes of Y would have their last known position of Y.

## approach 1

- send all broadcasts with a TTL
- periodically(?) do a survey to find the max TTL that is needed to fully cover the current network.
- to do a study first send a broadcast (maybe our current initial user announcement?) with TTL set to one (so therefore no one will rebroadcast our request)
- survey replies are sent unicast back to us (and intervening nodes will need to keep the route table that they have built up based on past packets)
- count the number of replies to this TTL 1 attempt. That is the number of nodes we can reach without any rebroadcasts
- repeat the study with a TTL of 2 and then 3. stop once the # of replies stops going up.
- it is important for any node to do listen before talk to prevent stomping on other rebroadcasters...
- For these little networks I bet a max TTL would never be higher than 3?

## approach 2

- send a TTL1 broadcast, the replies let us build a list of the nodes (stored as a bitvector?) that we can see (and their rssis)
- we then broadcast out that bitvector (also TTL1) asking "can any of ya'll (even indirectly) see anyone else?"
- if a node can see someone I missed (and they are the best person to see that node), they reply (unidirectionally) with the missing nodes and their rssis (other nodes might sniff (and update their db) based on this reply but they don't have to)
- given that the max number of nodes in this mesh will be like 20 (for normal cases), I bet globally updating this db of "nodenums and who has the best rssi for packets from that node" would be useful
- once the global DB is shared, when a node wants to broadcast, it just sends out its broadcast . the first level receivers then make a decision "am I the best to rebroadcast to someone who likely missed this packet?" if so, rebroadcast

## approach 3

- when a node X wants to know other nodes positions, it broadcasts its position with want_replies=true. Then each of the nodes that received that request broadcast their replies (possibly by using special timeslots?)
- all nodes constantly update their local db based on replies they witnessed.
- after 10s (or whatever) if node Y notices that it didn't hear a reply from node Z (that Y has heard from recently ) to that initial request, that means Z never heard the request from X. Node Y will reply to X on Z's behalf.
- could this work for more than one hop? Is more than one hop needed? Could it work for sending messages (i.e. for a msg sent to Z with want-reply set).

## approach 4

look into the literature for this idea specifically.

- don't view it as a mesh protocol as much as a "distributed db unification problem". When nodes talk to nearby nodes they work together
  to update their nodedbs. Each nodedb would have a last change date and any new changes that only one node has would get passed to the
  other node. This would nicely allow distant nodes to propogate their position to all other nodes (eventually).
- handle group messages the same way, there would be a table of messages and time of creation.
- when a node has a new position or message to send out, it does a broadcast. All the adjacent nodes update their db instantly (this handles 90% of messages I'll bet).
- Occasionally a node might broadcast saying "anyone have anything newer than time X?" If someone does, they send the diffs since that date.
- essentially everything in this variant becomes broadcasts of "request db updates for >time X - for _all_ or for a particular nodenum" and nodes sending (either due to request or because they changed state) "here's a set of db updates". Every node is constantly trying to
  build the most recent version of reality, and if some nodes are too far, then nodes closer in will eventually forward their changes to the distributed db.
- construct non ambigious rules for who broadcasts to request db updates. ideally the algorithm should nicely realize node X can see most other nodes, so they should just listen to all those nodes and minimize the # of broadcasts. the distributed picture of nodes rssi could be useful here?
- possibly view the BLE protocol to the radio the same way - just a process of reconverging the node/msgdb database.
