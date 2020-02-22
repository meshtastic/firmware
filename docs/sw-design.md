This is a mini design doc for various core behaviors...

# Rules for sleep

## States

From lower to higher power consumption.

* Super-deep-sleep (SDS) - everything is off, CPU, radio, bluetooth, GPS.  Only wakes due to timer or button press

* deep-sleep (DS) - CPU is off, radio is on, bluetooth and GPS is off.  Note: This mode is never used currently, because it only saves 1.5mA vs light-sleep
  onEntry: setBluetoothOn(false), call doDeepSleep
  onExit: (standard bootup code, starts in DARK)

* light-sleep (LS) - CPU is suspended (RAM stays alive), radio is on, bluetooth is off, GPS is off.  Note: currently GPS is not turned 
off during light sleep, but there is a TODO item to fix this.
  onEntry: setBluetoothOn(false), setGPSPower(false), call doLightSleep
  onExit: start trying to get gps lock: gps.startLock(), once lock arrives service.sendPosition(BROADCAST)

* No bluetooth (NB) - CPU is running, radio is on, GPS is on but bluetooth is off, screen is off.
  onEntry: setGPSPower(true), setBluetoothOn(false)
  onExit: 

* running dark (DARK) - Everything is on except screen
  onEntry: setBluetoothOn(true)
  onExit:

* full on (ON) - Everything is on
  onEntry: setBluetoothOn(true), screen.setOn(true)
  onExit: screen.setOn(false)
  
## Behavior

### events that increase CPU activity

* While in LS: Once every position_broadcast_secs (default 15 mins) - the unit will wake into DARK mode and broadcast a "networkPing" (our position) and stay alive for wait_bluetooth_secs (default 30 seconds).  This allows other nodes to have a record of our last known position if we go away and allows a paired phone to hear from us and download messages.
* While in LS: Every send_owner_interval (defaults to 4, i.e. one hour), when we wake to send our position we _also_ broadcast our owner.  This lets new nodes on the network find out about us or correct duplicate node number assignments.
* While in LS/NB/DARK: If the user presses a button we go to full ON mode for screen_on_secs (default 30 seconds).  Multiple presses keeps resetting this timeout
* While in LS/NB/DARK: If we receive text messages, we go to full ON mode for screen_on_secs (same as if user pressed a button)
* While in LS: if we receive packets on the radio we will wake and handle them and stay awake in NB mode for min_wake_secs (default 10 seconds) - if we don't have packets we need to deliver to our phone.  If we do have packets the phone would want we instead stay in DARK mode for wait_bluetooth secs.

### events that decrease cpu activity

* While in ON: If it has been more than screen_on_secs since a press, lower to DARK
* While in DARK: If time since last contact by our phone exceeds phone_timeout_secs (15 minutes), we transition down into NB mode
* While in DARK or NB: If nothing above is forcing us to stay in a higher mode (wait_bluetooth_secs, min_wake_secs) we will lower down
into either LS or SDS levels.  If either phone_sds_timeout_secs (default 1 hr) or mesh_sds_timeout_secs (default 1 hr) are exceeded we will lower into SDS mode for sds_secs (or a button press).  Otherwise we will lower into LS mode for ls_secs (default 1 hr) (or until an interrupt, button press)

TODO: Eventually these scheduled intervals should be synchronized to the GPS clock, so that we can consider leaving the lora receiver off to save even more power.

# Low power consumption tasks

General ideas to hit the power draws our spreadsheet predicts.  Do the easy ones before beta, the last 15% can be done after 1.0.

* don't even power on the gps until someone else wants our position, just stay in lora deep sleep until press or rxpacket (except for once an hour updates)
* (possibly bad idea - better to have lora radio always listen - check spreadsheet) have every node wake at the same tick and do their position syncs then go back to deep sleep
* lower BT announce interval to save battery
* change to use RXcontinuous mode and config to drop packets with bad CRC (see section 6.4 of datasheet) - I think this is already the case
* have mesh service run in a thread that stays blocked until a packet arrives from the RF95
* platformio sdkconfig CONFIG_PM and turn on modem sleep mode
* keep cpu 100% in deepsleep until irq from radio wakes it.  Then stay awake for 30 secs to attempt delivery to phone.  
* use https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ association sleep pattern to save power - but see https://github.com/espressif/esp-idf/issues/2070 and https://esp32.com/viewtopic.php?f=13&t=12182 it seems with BLE on the 'easy' draw people are getting is 80mA
* stop using loop() instead use a job queue and let cpu sleep
* measure power consumption and calculate battery life assuming no deep sleep
* do lowest sleep level possible where BT still works during normal sleeping, make sure cpu stays in that mode unless lora rx packet happens, bt rx packet happens or button press happens
* optionally do lora messaging only during special scheduled intervals (unless nodes are told to go to low latency mode), then deep sleep except during those intervals - before implementing calculate what battery life would be with  this feature
* see section 7.3 of https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf and have hope radio wake only when a valid packet is received.  Possibly even wake the ESP32 from deep sleep via GPIO.
* never enter deep sleep while connected to USB power (but still go to other low power modes)
* when main cpu is idle (in loop), turn cpu clock rate down and/or activate special sleep modes.  We want almost everything shutdown until it gets an interrupt.

# Mesh broadcast algoritm

FIXME - instead look for standard solutions.  this approach seems really suboptimal, because too many nodes will try to rebroast.  If
all else fails could always use the stock radiohead solution - though super inefficent.

## approach 1

* send all broadcasts with a TTL
* periodically(?) do a survey to find the max TTL that is needed to fully cover the current network.
* to do a study first send a broadcast (maybe our current initial user announcement?) with TTL set to one (so therefore no one will rebroadcast our request)
* survey replies are sent unicast back to us (and intervening nodes will need to keep the route table that they have built up based on past packets)
* count the number of replies to this TTL 1 attempt.  That is the number of nodes we can reach without any rebroadcasts
* repeat the study with a TTL of 2 and then 3.  stop once the # of replies stops going up.
* it is important for any node to do listen before talk to prevent stomping on other rebroadcasters...
* For these little networks I bet a max TTL would never be higher than 3?

## approach 2

* send a TTL1 broadcast, the replies let us build a list of the nodes (stored as a bitvector?) that we can see (and their rssis)
* we then broadcast out that bitvector (also TTL1) asking "can any of ya'll (even indirectly) see anyone else?"
* if a node can see someone I missed (and they are the best person to see that node), they reply (unidirectionally) with the missing nodes and their rssis (other nodes might sniff (and update their db) based on this reply but they don't have to)
* given that the max number of nodes in this mesh will be like 20 (for normal cases), I bet globally updating this db of "nodenums and who has the best rssi for packets from that node" would be useful
* once the global DB is shared, when a node wants to broadcast, it just sends out its broadcast . the first level receivers then make a decision "am I the best to rebroadcast to someone who likely missed this packet?" if so, rebroadcast

## approach 3

* when a node X wants to know other nodes positions, it broadcasts its position with want_replies=true.  Then each of the nodes that received that request broadcast their replies (possibly by using special timeslots?)
* all nodes constantly update their local db based on replies they witnessed.
* after 10s (or whatever) if node Y notices that it didn't hear a reply from node Z (that Y has heard from recently ) to that initial request, that means Z never heard the request from X.  Node Y will reply to X on Z's behalf.
* could this work for more than one hop?  Is more than one hop needed?  Could it work for sending messages (i.e. for a msg sent to Z with want-reply set). 

## approach 4

look into the literature for this idea specifically.

* don't view it as a mesh protocol as much as a "distributed db unification problem".  When nodes talk to nearby nodes they work together
to update their nodedbs.  Each nodedb would have a last change date and any new changes that only one node has would get passed to the 
other node.  This would nicely allow distant nodes to propogate their position to all other nodes (eventually).
* handle group messages the same way, there would be a table of messages and time of creation.
* when a node has a new position or message to send out, it does a broadcast.  All the adjacent nodes update their db instantly (this handles 90% of messages I'll bet).  
* Occasionally a node might broadcast saying "anyone have anything newer than time X?"  If someone does, they send the diffs since that date.
* essentially everything in this variant becomes broadcasts of "request db updates for >time X - for _all_ or for a particular nodenum" and nodes sending (either due to request or because they changed state) "here's a set of db updates".  Every node is constantly trying to
build the most recent version of reality, and if some nodes are too far, then nodes closer in will eventually forward their changes to the distributed db.
* construct non ambigious rules for who broadcasts to request db updates.  ideally the algorithm should nicely realize node X can see most other nodes, so they should just listen to all those nodes and minimize the # of broadcasts. the distributed picture of nodes rssi could be useful here?
* possibly view the BLE protocol to the radio the same way - just a process of reconverging the node/msgdb database.
