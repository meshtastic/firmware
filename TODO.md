# High priority

* make jtag work on second board
* notify phone when rx packets arrive, currently the phone polls at startup only
* when notified phone should download messages
* have phone use our local node number as its node number (instead of hardwired)
* have MeshService keep a node DB by sniffing user messages
* have meshservice send location data on mesh (if device has a GPS)
* make basic gui. different screens: debug, one page for each user in the user db, last received text message
* respond to the WantUsers message

# Medium priority

* use https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ association sleep pattern to save power - but see https://github.com/espressif/esp-idf/issues/2070 
* correctly map nodeids to nodenums, currently we just do a proof of concept by always doing a broadcast
* add interrupt detach/sleep mode config to lora radio so we can enable deepsleep without panicing
* figure out if we can use PA_BOOST
* scrub default radio config settings for bandwidth/range/speed
* use a freertos thread to remain blocked reading from recvfromAckTimeout, so that we don't need to keep polling it from our main thread
* override peekAtMessage so we can see any messages that pass through our node (even if not broadcast)?  would that be useful?
* sendToMesh can currently block for a long time, instead have it just queue a packet for a radio freertos thread
* see section 7.3 of https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf and have hope radio wake only when a valid packet is received.  Possibly even wake the ESP32 from deep sleep via GPIO.
* fix the logo
* do debug logging to android over bluetooth
* break out my bluetooth OTA software as a seperate library so others can use it

# Pre-beta priority

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
