# High priority

* have meshservice periodically send location data on mesh (if device has a GPS)
* implement getCurrentTime() - set based off gps but then updated locally
* implement regen owner and radio prefs
* confirm second device receives that gps message and updates device db

* save our node db (and any rx packets waiting for phone) to flash - see DeviceState protobuf
* port my graphics library over from the sw102, same screen controller and resolution
* very occasionally send our position and user packet (if for nothing else so that other nodes update last_seen)
* switch to my gui layout manager
* make jtag work on second board
* make basic gui. different screens: debug, one page for each user in the user db, last received text message

# Medium priority

* Heltec LoRa32 has 8MB flash, use a bigger partition table if needed - TTGO is 4MB but has PSRAM
* use two different env flags for ttgo vs lora32. https://docs.platformio.org/en/latest/ide/vscode.html#key-bindings
* don't send location packets if we haven't moved
* send correct hw vendor in the bluetooth info - needed so the android app can update different radio models
* use https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ association sleep pattern to save power - but see https://github.com/espressif/esp-idf/issues/2070 
* correctly map nodeids to nodenums, currently we just do a proof of concept by always doing a broadcast
* add interrupt detach/sleep mode config to lora radio so we can enable deepsleep without panicing
* scrub default radio config settings for bandwidth/range/speed
* use a freertos thread to remain blocked reading from recvfromAckTimeout, so that we don't need to keep polling it from our main thread
* override peekAtMessage so we can see any messages that pass through our node (even if not broadcast)?  would that be useful?
* sendToMesh can currently block for a long time, instead have it just queue a packet for a radio freertos thread
* see section 7.3 of https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf and have hope radio wake only when a valid packet is received.  Possibly even wake the ESP32 from deep sleep via GPIO.
* fix the logo
* do debug logging to android over bluetooth
* break out my bluetooth OTA software as a seperate library so others can use it
* never enter deep sleep while connected to USB power (but still go to other low power modes)
* How do avalanche beacons work?  Could this do that as well?  possibly by using beacon mode feature of the RF95?
* use std::map<BLECharacteristic*, std::string> in node db

# Low power consumption tasks

* stop using loop() instead use a job queue and let cpu sleep
* move lora rx/tx to own thread and block on IO
* measure power consumption and calculate battery life assuming no deep sleep
* do lowest sleep level possible where BT still works during normal sleeping, make sure cpu stays in that mode unless lora rx packet happens, bt rx packet happens or button press happens
* optionally do lora messaging only during special scheduled intervals (unless nodes are told to go to low latency mode), then deep sleep except during those intervals - before implementing calculate what battery life would be with  this feature

# Pre-beta priority

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