# High priority

* have node info screen show real info (including distance and heading)
* very occasionally send our position and user packet (if for nothing else so that other nodes update last_seen)
* make a screen for bluetooth not yet configured

# Medium priority

* only BLE advertise for a short time after the screen is on and button pressed - to save power and prevent people for sniffing for our BT app.
* use https://platformio.org/lib/show/1260/OneButton
* make an about to sleep screen
* make a no bluetooth configured yet screen
* don't send location packets if we haven't moved
* scrub default radio config settings for bandwidth/range/speed
* add basic crypto - http://rweather.github.io/arduinolibs/crypto.html with speck https://www.airspayce.com/mikem/arduino/RadioHead/rf95_encrypted_client_8pde-example.html
* override peekAtMessage so we can see any messages that pass through our node (even if not broadcast)?  would that be useful?
* sendToMesh can currently block for a long time, instead have it just queue a packet for a radio freertos thread
* fix the logo
* How do avalanche beacons work?  Could this do that as well?  possibly by using beacon mode feature of the RF95?
* use std::map<BLECharacteristic*, std::string> in node db
* first alpha release, article writeup
* send pr https://github.com/ThingPulse/esp8266-oled-ssd1306 to tell them about this project

# Low power consumption tasks

* have radiohead ISR send messages to RX queue directly, to allow that thread to block until we have something to send
* use https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ association sleep pattern to save power - but see https://github.com/espressif/esp-idf/issues/2070 
* stop using loop() instead use a job queue and let cpu sleep
* move lora rx/tx to own thread and block on IO
* measure power consumption and calculate battery life assuming no deep sleep
* do lowest sleep level possible where BT still works during normal sleeping, make sure cpu stays in that mode unless lora rx packet happens, bt rx packet happens or button press happens
* optionally do lora messaging only during special scheduled intervals (unless nodes are told to go to low latency mode), then deep sleep except during those intervals - before implementing calculate what battery life would be with  this feature
* see section 7.3 of https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf and have hope radio wake only when a valid packet is received.  Possibly even wake the ESP32 from deep sleep via GPIO.
* never enter deep sleep while connected to USB power (but still go to other low power modes)
* when main cpu is idle (in loop), turn cpu clock rate down and/or activate special sleep modes.  We want almost everything shutdown until it gets an interrupt.

# dynamic nodenum assignment tasks

we currently do the following crap solution:
hardwire nodenums based on macaddr.  when node boots it broadcasts its Owner info (which includes our macaddr).  If any node receives Owner messages, the other nodes reply with their owner info.
Really should instead do something like: new node sends its owner info as a provisional request.  If any other node shows that nodenum in use by a different macaddr, they reply with NodeDeny.
If the node doesn't get denied within X seconds it then sends the info as a non provisional message (and other nodes update their node db)  
But fixme, think about this and look for standard solutions - it will have problems when meshes separate change and then rejoin.

# Pre-beta priority

* swap out speck for accelerated full AES https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/hwcrypto/aes.h
* cope with nodes that have 0xff or 0x00 as the last byte of their mac
* use variable length arduino Strings in protobufs (instead of current fixed buffers)
* don't even power on bluetooth until we have some data to send to the android phone.  Most of the time we should be sleeping in a lowpower "listening for lora" only mode.  Once we have some packets for the phone, then power on bluetooth
until the phone pulls those packets.  Ever so often power on bluetooth just so we can see if the phone wants to send some packets.  Possibly might need ULP processor to help with this wake process.
* do hibernation mode to get power draw down to 2.5uA https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ 
* make sure main cpu is not woken for packets with bad crc or not addressed to this node - do that in the radio hw
* enable fast init inside the gps chip
* dynamically select node nums
* triple check fcc compliance
* allow setting full radio params from android
* pick channel center frequency based on name? "dolphin" would hash to 900Mhz, "cat" to 905MHz etc?  Or is that too opaque?
* scan to find channels with low background noise?
* share channel settings over Signal (or qr code) by embedding an an URL which is handled by the MeshUtil app.
* make jtag debugger id stable: https://askubuntu.com/questions/49910/how-to-distinguish-between-identical-usb-to-serial-adapters

# Low priority

* use two different env flags for ttgo vs lora32. https://docs.platformio.org/en/latest/ide/vscode.html#key-bindings
* sim gps data for testing nodes that don't have hardware
* have android provide position data for nodes that don't have gps
* do debug serial logging to android over bluetooth
* break out my bluetooth OTA software as a seperate library so others can use it
* Heltec LoRa32 has 8MB flash, use a bigger partition table if needed - TTGO is 4MB but has PSRAM
* add a watchdog timer
* fix GPS.zeroOffset calculation it is wrong
* handle millis() rollover in GPS.getTime - otherwise we will break after 50 days
* reneable the bluetooth battely level service on the T-BEAM, because we can read battery level there
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