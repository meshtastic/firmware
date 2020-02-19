# High priority
Items to complete before the first alpha release.

* post sample video to signal forum
* support non US frequencies
* make an install script to let novices install software on their boards
* retest BLE software update for both board types
* default to enter deep sleep if no LORA received for two hours (indicates user has probably left the meshS)
* first alpha release, article writeup for hackaday
* send note about Adafruit Clue
* send note to the guy who designed the cases
* send pr https://github.com/ThingPulse/esp8266-oled-ssd1306 to tell them about this project
* update the prebuilt bins for different regulatory regions

# Medium priority
Items to complete before the first beta release.

* for non GPS equipped devices, set time from phone
* GUI on oled hangs for a few seconds occasionally, but comes back
* assign every "channel" a random shared 8 bit sync word (per 4.2.13.6 of datasheet) - use that word to filter packets before even checking CRC.  This will ensure our CPU will only wake for packets on our "channel"  
* Note: we do not do address filtering at the chip level, because we might need to route for the mesh
* Use the Periodic class for both position and user periodic broadcasts
* make debug info screen show real data (including battery level & charging)
* don't forward redundant pings or ping responses to the phone, it just wastes phone battery
* don't treat north as up, instead adjust shown bearings for our guess at the users heading (i.e. subtract one from the other)
* answer to pings (because some other user is looking at our nodeinfo) with our latest location
* show radio and gps signal strength as an image
* only BLE advertise for a short time after the screen is on and button pressed - to save power and prevent people for sniffing for our BT app.
* use https://platformio.org/lib/show/1260/OneButton if necessary
* make an about to sleep screen
* don't send location packets if we haven't moved
* scrub default radio config settings for bandwidth/range/speed
* add basic crypto - http://rweather.github.io/arduinolibs/crypto.html with speck https://www.airspayce.com/mikem/arduino/RadioHead/rf95_encrypted_client_8pde-example.html
* override peekAtMessage so we can see any messages that pass through our node (even if not broadcast)?  would that be useful?
* sendToMesh can currently block for a long time, instead have it just queue a packet for a radio freertos thread
* How do avalanche beacons work?  Could this do that as well?  possibly by using beacon mode feature of the RF95?
* use std::map<NodeInfo*, std::string> in node db

# Low power consumption tasks
General ideas to hit the power draws our spreadsheet predicts.  Do the easy ones before beta, the last 15% can be done after 1.0.

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

FIXME - instead look for standard solutions.  this approach seems really suboptimal, because too many nodes will try to rebroast.

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

# Pre-beta priority
During the beta timeframe the following improvements 'would be nice' (and yeah - I guess some of these items count as features, but it is a hobby project ;-) )

* fix the frequency error reading in the RF95 RX code (can't do floating point math in an ISR ;-) 
* See CustomRF95::send and fix the problem of dropping partially received packets if we want to start sending
* swap out speck for hw-accelerated full AES https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/hwcrypto/aes.h
* use variable length arduino Strings in protobufs (instead of current fixed buffers)
* don't even power on bluetooth until we have some data to send to the android phone.  Most of the time we should be sleeping in a lowpower "listening for lora" only mode.  Once we have some packets for the phone, then power on bluetooth
until the phone pulls those packets.  Ever so often power on bluetooth just so we can see if the phone wants to send some packets.  Possibly might need ULP processor to help with this wake process.
* do hibernation mode to get power draw down to 2.5uA https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ 
* make sure main cpu is not woken for packets with bad crc or not addressed to this node - do that in the radio hw
* enable fast init inside the gps chip
* triple check fcc compliance
* pick channel center frequency based on name? "dolphin" would hash to 900Mhz, "cat" to 905MHz etc?  Or is that too opaque?
* scan to find channels with low background noise?
* share channel settings over Signal (or qr code) by embedding an an URL which is handled by the MeshUtil app.

# Low priority
Items after the first final candidate release.

* make a no bluetooth configured yet screen - include this screen in the loop if the user hasn't yet paired
* the AXP debug output says it is trying to charge at 700mA, but the max I've seen is 180mA, so AXP registers probably need to be set to tell them the circuit can only provide 300mAish max. So that the low charge rate kicks in faster and we don't wear out batteries.
* increase the max charging rate a bit for 18650s, currently it limits to 180mA (at 4V).  Work backwards from the 500mA USB limit (at 5V) and let the AXP charge at that rate.
* if radio params change fundamentally, discard the nodedb
* discard very old nodedb records (> 1wk)
* using the genpartitions based table doesn't work on TTGO so for now I stay with my old memory map
* We let anyone BLE scan for us (FIXME, perhaps only allow that until we are paired with a phone and configured) 
* use two different buildenv flags for ttgo vs lora32. https://docs.platformio.org/en/latest/ide/vscode.html#key-bindings
* sim gps data for testing nodes that don't have hardware
* have android provide position data for nodes that don't have gps
* do debug serial logging to android over bluetooth
* break out my bluetooth OTA software as a seperate library so others can use it
* Heltec LoRa32 has 8MB flash, use a bigger partition table if needed - TTGO is 4MB but has PSRAM
* add a watchdog timer
* fix GPS.zeroOffset calculation it is wrong
* handle millis() rollover in GPS.getTime - otherwise we will break after 50 days
* reneable the bluetooth battery level service on the T-BEAM, because we can read battery level there
* report esp32 device code bugs back to the mothership via android

# Done

* change the partition table to take advantage of the 4MB flash on the wroom: http://docs.platformio.org/en/latest/platforms/espressif32.html#partition-tables
* wrap in nice MeshRadio class
* add mesh send & rx
* make message send from android go to service, then to mesh radio
* make message receive from radio go through to android
* test loopback tx/rx path code without using radio
* notify phone when rx packets arrive, currently the phone polls at startup only
* figure out if we can use PA_BOOST - yes, it seems to be on both boards
* implement new ble characteristics
* have MeshService keep a node DB by sniffing user messages
* have a state machine return the correct FromRadio packet to the phone, it isn't always going to be a MeshPacket.  Do a notify on fromnum to force the radio to read our state machine generated packets
* send my_node_num when phone sends WantsNodes
* have meshservice periodically send location data on mesh (if device has a GPS)
* implement getCurrentTime() - set based off gps but then updated locally
* make default owner record have valid usernames
* message loop between node 0x28 and 0x7c
* check in my radiolib fixes
* figure out what is busted with rx
* send our owner info at boot, reply if we see anyone send theirs
* add manager layers
* confirm second device receives that gps message and updates device db
* send correct hw vendor in the bluetooth info - needed so the android app can update different radio models
* correctly map nodeids to nodenums, currently we just do a proof of concept by always doing a broadcast
* add interrupt detach/sleep mode config to lora radio so we can enable deepsleep without panicing
* make jtag work on second board
* implement regen owner and radio prefs
* use a better font
* make nice screens (boot, about to sleep, debug info (gps signal, #people), latest text, person info - one frame per person on network)
* turn framerate from ui->state.frameState to 1 fps (or less) unless in transition
* switch to my gui layout manager
* make basic gui. different screens: debug, one page for each user in the user db, last received text message
* make button press cycle between screens
* save our node db on entry to sleep
* fix the logo
* sent/received packets (especially if a node was just reset) have variant of zero sometimes - I think there is a bug (race-condtion?) in the radio send/rx path.
* DONE dynamic nodenum assignment tasks
* make jtag debugger id stable: https://askubuntu.com/questions/49910/how-to-distinguish-between-identical-usb-to-serial-adapters
* reported altitude is crap
* good tips on which bands might be more free https://github.com/TheThingsNetwork/ttn/issues/119
* finish power measurements (GPS on during sleep vs LCD on during sleep vs LORA on during sleep) and est battery life
* make screen sleep behavior work
* make screen advance only when a new node update arrives, a new text arrives or the user presses a button, turn off screen after a while
* after reboot, channel number is getting reset to zero! fix!
* send user and location events much less often
* send location (or if not available user) when the user wakes the device from display sleep (both for testing and to improve user experience)
* make real implementation of getNumOnlineNodes
* very occasionally send our position and user packet based on the schedule in the radio info (if for nothing else so that other nodes update last_seen)
* show real text info on the text screen
* apply radio settings from android land
* cope with nodes that have 0xff or 0x00 as the last byte of their mac
* allow setting full radio params from android
* add receive timestamps to messages, inserted by esp32 when message is received but then shown on the phone
* update build to generate both board types
* have node info screen show real info (including distance and heading)
* blink the power led less often
* have radiohead ISR send messages to RX queue directly, to allow that thread to block until we have something to send
* move lora rx/tx to own thread and block on IO
* keep our pseudo time moving forward even if we enter deep sleep (use esp32 rtc)