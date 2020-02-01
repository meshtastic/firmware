

# High priority

* make message send from android go to service, then to mesh radio
* make message receive from radio go through to android
* have MeshService keep a node DB by sniffing user messages
* have meshservice send location data on mesh (if device has a GPS)

# Medium priority

* correctly map nodeids to nodenums, currently we just do a proof of concept by always doing a broadcast
* add interrupt detach/sleep mode config to lora radio so we can enable deepsleep without panicing
* figure out if we can use PA_BOOST
* scrub default radio config settings for bandwidth/range/speed
* use a freertos thread to remain blocked reading from recvfromAckTimeout, so that we don't need to keep polling it from our main thread
* override peekAtMessage so we can see any messages that pass through our node (even if not broadcast)?  would that be useful?

# Pre-beta priority

* make sure main cpu is not woken for packets with bad crc or not addressed to this node - do that in the radio hw
* enable fast init inside the gps chip
* dynamically select node nums
* triple check fcc compliance
* allow setting full radio params from android

# Done

* change the partition table to take advantage of the 4MB flash on the wroom: http://docs.platformio.org/en/latest/platforms/espressif32.html#partition-tables
* wrap in nice MeshRadio class
* add mesh send & rx
