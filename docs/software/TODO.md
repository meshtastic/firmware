# Geeksville's current work queue

You probably don't care about this section - skip to the next one.

Nimble tasks:

- packets >= 79 bytes (FromRadio) cause INVALID_OFFSET (7) gatt errors to be sent to the app
  FIXME - add instrumentation
  is MTU setting heppening ever? or only on the first attempt?
  client is reading nodeinfos. do we do a second 79 byte read, and that one returns 7 because the length is weong? when the second read should have been 72 bytes?
  hmm rebooting did not fix

  disconnecting the app and reconnecting it (on the settings screen) did fix it. So is the android app confused?
  is the retry on exception thing not tearing enough down?

  could old reads from a previous session be coming back and messing up new reads? do we need to cancel all pending Jobs?
  use reliable writes? < I don't think that is the problem? >

BLE fromRadio called
getFromRadio, !available
toRadioWriteCb data 0x3ffc3d72, len 4
Trigger powerFSM 9
Client wants config, nonce=3494
Reset nodeinfo read pointer
toRadioWriteCb data 0x3ffc3d72, len 4
Trigger powerFSM 9
Client wants config, nonce=3493
Reset nodeinfo read pointer
BLE fromRadio called
getFromRadio, state=2
encoding toPhone packet to phone variant=3, 50 bytes
BLE fromRadio called
getFromRadio, state=3
encoding toPhone packet to phone variant=6, 83 bytes
BLE fromRadio called
getFromRadio, state=4
Sending nodeinfo: num=0xabdddf38, lastseen=1594754867, id=!2462abdddf38, name=Bob b
encoding toPhone packet to phone variant=4, 67 bytes
BLE fromRadio called
getFromRadio, state=4
Sending nodeinfo: num=0x28b200b4, lastseen=1595545403, id=!246f28b200b4, name=Unknown 00b4
encoding toPhone packet to phone variant=4, 79 bytes \*\*\*\*
BLE fromRadio called
getFromRadio, state=4
Sending nodeinfo: num=0xabf84098, lastseen=1593680756, id=!2462abf84098, name=bx n
encoding toPhone packet to phone variant=4, 72 bytes
BLE fromRadio called
getFromRadio, state=4
Sending nodeinfo: num=0x83f0d8e5, lastseen=1594686931, id=!e8e383f0d8e5, name=Unknown d8e5
encoding toPhone packet to phone variant=4, 64 bytes
BLE fromRadio called
getFromRadio, state=4
Done sending nodeinfos
getFromRadio, state=5

2020-07-23 16:52:47.843 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.RadioInterfaceService: Broadcasting connection=true
2020-07-23 16:52:47.845 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:47.845 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:47.846 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.CONNECT_CHANGED
2020-07-23 16:52:47.847 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: onConnectionChanged=CONNECTED
2020-07-23 16:52:47.847 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Starting config nonce=4091
2020-07-23 16:52:47.849 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: queuing 4 bytes to f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:47.849 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: writeC f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:47.852 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.CONNECT_CHANGED
2020-07-23 16:52:47.852 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: onConnectionChanged=CONNECTED
2020-07-23 16:52:47.852 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Starting config nonce=4090
2020-07-23 16:52:47.852 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: device sleep timeout cancelled
2020-07-23 16:52:47.853 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: queuing 4 bytes to f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:47.853 6478-16336/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: writeC f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:48.294 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: writeC f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:48.296 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5 is completed, resuming status=0, res=android.bluetooth.BluetoothGattCharacteristic@1717693
2020-07-23 16:52:48.296 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: Received 79 bytes from radio
2020-07-23 16:52:48.299 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:48.300 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.RECEIVE_FROMRADIO
2020-07-23 16:52:48.301 6478-6478/com.geeksville.mesh E/com.geeksville.mesh.service.MeshService: Invalid Protobuf from radio, len=79 (exception Protocol message had invalid UTF-8.)
2020-07-23 16:52:48.301 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.RECEIVE_FROMRADIO
2020-07-23 16:52:48.302 6478-6478/com.geeksville.mesh E/com.geeksville.mesh.service.MeshService: Invalid Protobuf from radio, len=79 (exception Protocol message had invalid UTF-8.)
2020-07-23 16:52:48.384 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth\$BluetoothContinuation: Starting work: writeC f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work writeC f75c76d2-129e-4dad-a1dd-7866124401e7 is completed, resuming status=0, res=android.bluetooth.BluetoothGattCharacteristic@82276d0
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: write of 4 bytes completed
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
...
07-23 16:52:48.302 6478-6478/com.geeksville.mesh E/com.geeksville.mesh.service.MeshService: Invalid Protobuf from radio, len=79 (exception Protocol message had invalid UTF-8.)
2020-07-23 16:52:48.384 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: writeC f75c76d2-129e-4dad-a1dd-7866124401e7
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work writeC f75c76d2-129e-4dad-a1dd-7866124401e7 is completed, resuming status=0, res=android.bluetooth.BluetoothGattCharacteristic@82276d0
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: write of 4 bytes completed
2020-07-23 16:52:48.387 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:48.474 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:48.476 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work writeC f75c76d2-129e-4dad-a1dd-7866124401e7 is completed, resuming status=0, res=android.bluetooth.BluetoothGattCharacteristic@82276d0
2020-07-23 16:52:48.476 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: write of 4 bytes completed
2020-07-23 16:52:48.833 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:48.835 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5 is completed, resuming status=0, res=android.bluetooth.BluetoothGattCharacteristic@1717693
2020-07-23 16:52:48.835 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.BluetoothInterface: Received 79 bytes from radio
2020-07-23 16:52:48.837 6478-19966/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: Enqueuing work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:48.839 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.RECEIVE_FROMRADIO
2020-07-23 16:52:48.840 6478-6478/com.geeksville.mesh E/com.geeksville.mesh.service.MeshService: Invalid Protobuf from radio, len=79 (exception Protocol message had invalid UTF-8.)
2020-07-23 16:52:48.840 6478-6478/com.geeksville.mesh D/com.geeksville.mesh.service.MeshService: Received broadcast com.geeksville.mesh.RECEIVE_FROMRADIO
2020-07-23 16:52:48.842 6478-6478/com.geeksville.mesh E/com.geeksville.mesh.service.MeshService: Invalid Protobuf from radio, len=79 (exception Protocol message had invalid UTF-8.)
2020-07-23 16:52:49.104 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth$BluetoothContinuation: Starting work: readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:49.106 6478-27868/com.geeksville.mesh D/com.geeksville.mesh.service.SafeBluetooth: work readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5 is completed, resuming status=7, res=android.bluetooth.BluetoothGattCharacteristic@1717693
2020-07-23 16:52:49.107 6478-27868/com.geeksville.mesh W/com.geeksville.mesh.service.BluetoothInterface: Scheduling reconnect because error during doReadFromRadio - disconnecting, Bluetooth status=7 while doing readC 8ba2bcc2-ee02-4a55-a531-c525c5e454d5
2020-07-23 16:52:49.108 6478-6546/com.geeksville.mesh W/com.geeksville.mesh.service.BluetoothInterface: Forcing disconnect and hopefully device will comeback (disabling forced refresh)
2020-07-23 16:52:49.108 6478-6546/com.geeksville.mesh I/com.geeksville.mesh.service.SafeBluetooth: Closing our GATT connection

- restart advertising after client disconnects (confirm this works if client goes out of range)
- started RPA long test, jul 22 6pm
- implement nimble software update api

* update protocol description per cyclomies email thread
* update faq with antennas https://meshtastic.discourse.group/t/range-test-ideas-requested/738/2
* update faq on recommended android version and phones
* add help link inside the app, reference a page on the wiki
* turn on amazon reviews support
* add a tablet layout (with map next to messages) in the android app

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

- implement nimble battery level service
- Nimble implement device info service remaining fields (hw version etc)
- Turn on RPA addresses for the device side in Nimble
- Try to teardown less of the Nimble protocol stack across sleep
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
