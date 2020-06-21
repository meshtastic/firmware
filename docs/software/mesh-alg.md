# Mesh broadcast algorithm

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
- Remove NodeNum assignment algorithm (now that we use 4 byte node nums)
- make android app warn if firmware is too old or too new to talk to
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

# Old notes

FIXME, merge into the above:

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
