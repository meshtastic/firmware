# Geeksville's current work queue

You probably don't care about this section - skip to the next one.

1.2 cleanup & multichannel support:

* DONE call RouterPlugin for *all* packets - not just Router packets
* generate channel hash from the name of the channel+the psk (not just one or the other)
* send a hint that can be used to select which channel to try and hash against with each message
* DONE remove deprecated
* DONE fix setchannel in phoneapi.cpp
* DONE set mynodeinfo.max_channels
* DONE set mynodeinfo.num_bands (formerly num_channels)
* DONE fix sniffing of non Routing packets
* DONE enable remote setttings access by moving settings operations into a regular plugin (move settings ops out of PhoneAPI)
* DONE move portnum up?
* DONE remove region specific builds from the firmware
* restrict settings operations to the admin channel
* add gui in android app for setting region
* make an alpha channel for the python API
* "FIXME - move the radioconfig/user/channel READ operations into SettingsMessage as well"
* DONE scrub protobufs to make sure they are absoloute minimum wiresize (in particular Data, ChannelSets and positions)
* change syncword
* allow chaning packets in single transmission - to increase airtime efficiency and amortize packet overhead
* DONE move most parts of meshpacket into the Data packet, so that we can chain multiple Data for sending when they all have a common destination and key.
* when selecting a MeshPacket for transmit, scan the TX queue for any Data packets we can merge together as a WirePayload.  In the low level send/rx code expand that into multiple MeshPackets as needed (thus 'hiding' from MeshPacket that over the wire we send multiple datapackets
* confirm we are still calling the plugins for messages inbound from the phone (or generated locally)
* confirm we are still multi hop routing flood broadcasts
* confirm we are still doing resends on unicast reliable packets
* add support for full DSR unicast delivery
* DONE move acks into routing
* DONE make all subpackets different versions of data
* DONE move routing control into a data packet
* have phoneapi done via plugin
* figure out how to add micro_delta to position, make it so that phone apps don't need to understand it?
* only send battery updates a max of once a minute
* add multichannel support in python
* add channel selection for sending
* record recevied channel in meshpacket
* test remote settings operations (confirm it works 3 hops away)
* add channel restrictions for plugins (and restrict routing plugin to the "control" channel)
* make a primaryChannel global and properly maintain it when the phone sends setChannel
* move setCrypto call into packet send and packet decode code
* implement 'small location diffs' change
* move battery level out of position? 
* DOUBLE CHECK android app can still upgrade 1.1 and 1.0 loads
 
eink:

* new battery level sensing
* measure current draw
* DONE: fix backlight
* USB is busted because of power enable mode?
* OHH BME280!  THAT IS GREAT!
* make new screen work, ask for datasheet
* say I think you could ship this
* leds seem busted
* usb doesn't stay connected
* check GPS works
* check GPS fast locking
* send email about variants & faster flash programming - https://github.com/geeksville/Meshtastic-esp32/commit/f110225173a77326aac029321cdb6491bfa640f6
* send PR for bootloader
* fix nrf52 time/date
* send new master bin file
* send email about low power mode problems
* support new flash chip in appload, possibly use low power mode
* swbug! stuck busy tx occurred!
  
For app cleanup:

* use structured logging to kep logs in ram.  Also send logs as packets to api clients
* DONE writeup nice python options docs (common cases, link to protobuf docs)
* have android app link to user manual
* DONE only do wantReplies once per packet type, if we change network settings force it again
* update positions and nodeinfos based on packets we just merely witness on the mesh.  via isPromsciousPort bool, remove sniffing
* DONE make device build always have a valid version
* DONE do fixed position bug https://github.com/meshtastic/Meshtastic-device/issues/536
* DONE check build guide
* DONE write devapi user guide
* DONE update android code: https://developer.android.com/topic/libraries/view-binding/migration
* DONE test GPIO watch
* DONE set --set-chan-fast, --set-chan-default
* writeup docs on gpio 
* DONE make python ping command
* DONE make hello world example service
* DONE have python tool check max packet size before sending to device
* DONE if request was sent reliably, send reply reliably
* DONE require a recent python api to talk to these new device loads
* DONE require a recent android app to talk to these new device loads
* DONE fix handleIncomingPosition
* DONE move want_replies handling into plugins
* DONE on android for received positions handle either old or new positions / user messages
* DONE on android side send old or new positions as needed / user messages
* DONE test python side handle new position/user messages
* DONE make a gpio example. --gpiowrb 4 1, --gpiord 0x444, --gpiowatch 0x3ff
* DONE fix position sending to use new plugin
* DONE Add SinglePortNumPlugin - as the new most useful baseclass
* DONE move positions into regular data packets (use new app framework)
* DONE move user info into regular data packets (use new app framework)
* DONE test that positions, text messages and user info still work
* DONE test that position, text messages and user info work properly with new android app and old device code
* DONE do UDP tunnel
* DONE fix the RTC drift bug
* move python ping functionality into device, reply with rxsnr info
* use channels for gpio security https://github.com/meshtastic/Meshtastic-device/issues/104
* MeshPackets for sending should be reference counted so that API clients would have the option of checking sent status (would allow removing the nasty 30 sec timer in gpio watch sending)

For high speed/lots of devices/short range tasks:

- When guessing numhops for sending: if I've heard from many local (0 hop neighbors) decrease hopcount by 2 rather than 1. 
This should nicely help 'router' nodes do the right thing when long range, or if there are many local nodes for short range.
- fix timeouts/delays to be based on packet length at current radio settings

* update faq with antennas https://meshtastic.discourse.group/t/range-test-ideas-requested/738/2
* update faq on recommended android version and phones
* add help link inside the app, reference a page on the wiki
* turn on amazon reviews support
* add a tablet layout (with map next to messages) in the android app

# Old docs to merge

MESH RADIO PROTOCOL

Old TODO notes on the mesh radio protocol, merge into real docs someday...

for each named group we have a pre-shared key known by all group members and
wrapped around the device. you can only be in one group at a time (FIXME?!) To
join the group we read a qr code with the preshared key and ParamsCodeEnum. that
gets sent via bluetooth to the device.  ParamsCodeEnum maps to a set of various
radio params (regulatory region, center freq, SF, bandwidth, bitrate, power
etc...) so all members of the mesh can have their radios set the same way.

once in that group, we can talk between 254 node numbers.
to get our node number (and announce our presence in the channel) we pick a
random node number and broadcast as that node with WANT-NODENUM(my globally
unique name).  If anyone on the channel has seen someone _else_ using that name
within the last 24 hrs(?) they reply with DENY-NODENUM. Note: we might receive
multiple denies.  Note: this allows others to speak up for some other node that
might be saving battery right now. Any time we hear from another node (for any
message type), we add that node number to the unpickable list.  To dramatically
decrease the odds a node number we request is already used by someone. If no one
denies within TBD seconds, we assume that we have that node number.  As long as
we keep talking to folks at least once every 24 hrs, others should remember we
have it.

Once we have a node number we can broadcast POSITION-UPDATE(my globally unique
name, lat, lon, alt, amt battery remaining).  All receivers will use this to a)
update the mapping of who is at what node nums, b) the time of last rx, c)
position.  If we haven't heard from that node in a while we reply to that node
(only) with our current POSITION_UPDATE state - so that node (presumably just
rejoined the network) can build a map of all participants.

We will periodically broadcast POSITION-UPDATE as needed based on distance moved
or a periodic minimum heartbeat.

If user wants to send a text they can SEND_TEXT(dest user, short text message).
Dest user is a node number, or 0xff for broadcast.

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
