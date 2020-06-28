# Geeksville's current work queue

You probably don't care about this section - skip to the next one.

- implement first cut of router mode: preferentially handle flooding, and change sleep and GPS behaviors
- NRF52 BLE support

# Medium priority

Items to complete before 1.0.

# Post 1.0 ideas

- finish DSR for unicast
- check fcc rules on duty cycle. we might not need to freq hop. https://www.sunfiretesting.com/LoRa-FCC-Certification-Guide/ . Might need to add enforcement for europe though.
- make a no bluetooth configured yet screen - include this screen in the loop if the user hasn't yet paired
- if radio params change fundamentally, discard the nodedb
- re-enable the bluetooth battery level service on the T-BEAM
- provide generalized (but slow) internet message forwarding service if one of our nodes has internet connectivity (MQTT) [ Not a requirement but a personal interest ]

# Low priority ideas

Items after the first final candidate release.

- dynamic frequency scaling could save a lot of power on ESP32, but it seems to corrupt uart (even with ref_tick set correctly)
- Change back to using a fixed sized MemoryPool rather than MemoryDynamic (see bug #149)
- scan to find channels with low background noise? (Use CAD mode of the RF95 to automatically find low noise channels)
- If the phone doesn't read fromradio mailbox within X seconds, assume the phone is gone and we can stop queing location msgs
  for it (because it will redownload the nodedb when it comes back)
- add frequency hopping, dependent on the gps time, make the switch moment far from the time anyone is going to be transmitting
- assign every "channel" a random shared 8 bit sync word (per 4.2.13.6 of datasheet) - use that word to filter packets before even checking CRC. This will ensure our CPU will only wake for packets on our "channel"
- the BLE stack is leaking about 200 bytes each time we go to light sleep
- use fuse bits to store the board type and region. So one load can be used on all boards
- Don't store position packets in the to phone fifo if we are disconnected. The phone will get that info for 'free' when it
  fetches the fresh nodedb.
- Use the RFM95 sequencer to stay in idle mode most of the time, then automatically go to receive mode and automatically go from transmit to receive mode. See 4.2.8.2 of manual.
- Use fixed32 for node IDs, packetIDs, successid, failid, and lat/lon - will require all nodes to be updated, but make messages slightly smaller.
- add "store and forward" support for messages, or move to the DB sync model. This would allow messages to be eventually delivered even if nodes are out of contact at the moment.
- use variable length Strings in protobufs (instead of current fixed buffers). This would save lots of RAM
- use BLEDevice::setPower to lower our BLE transmit power - extra range doesn't help us, it costs amps and it increases snoopability
- make a HAM build: just a new frequency list, a bool to say 'never do encryption' and use hte callsign as that node's unique id. -from Girts
- don't forward redundant pings or ping responses to the phone, it just wastes phone battery
- don't send location packets if we haven't moved significantly
- scrub default radio config settings for bandwidth/range/speed
- show radio and gps signal strength as an image
- only BLE advertise for a short time after the screen is on and button pressed - to save power and prevent people for sniffing for our BT app.
- make mesh aware network timing state machine (sync wake windows to gps time) - this can save LOTS of battery
- split out the software update utility so other projects can use it. Have the appload specify the URL for downloads.
- read the PMU battery fault indicators and blink/led/warn user on screen
- discard very old nodedb records (> 1wk)
- handle millis() rollover in GPS.getTime - otherwise we will break after 50 days
- report esp32 device code bugs back to the mothership via android
- change BLE bonding to something more secure. see comment by pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND)

Changes related to wifi support on ESP32:

- iram space: https://esp32.com/viewtopic.php?t=8460
- set https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html spi ram bss
- figure out if iram or bluetooth classic caused ble problems
- post bug on esp32-arduino with BLE bug findings

# Spinoff project ideas

- an open source version of https://www.burnair.ch/skynet/
- a paragliding app like http://airwhere.co.uk/
- How do avalanche beacons work? Could this do that as well? possibly by using beacon mode feature of the RF95?
