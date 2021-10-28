# Geeksville's current work queue

You probably don't care about this section - skip to the next one.

* usb lora dongle from pine64, add end user instructions
* measure rak4630 power draw and turn off power for GPS most of the time.  We should be able to run on the small solar panel.
* turn on watchdog reset if app hangs on nrf52 or esp32
* pine64 solar boards
* for the matrix gateway?  recommended by @sam-uk https://github.com/matrix-org/coap-proxy
* figure our wss for mqtt.meshtastic - use cloudflare? 2052 ws, 2053 crypt
* ask for vercel access
* finish plan for riot.im
* turn on setTx(timeout) and   state = setDioIrqParams(SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT); in sx1262 code
* NO add rak4600 support (with rf95 radio and limited ram)
* store esp32 crashes to flash (and 64KB coredump partition) - https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/core_dump.html
* If more nodes appear than the nodedb can hold, delete oldest entries from DB
* send debug info 'in-band'
* DONE @luxonn reports that after a while the android app stops showing new messages
* DONE release android APK - fix recent 1.2.28 crash report
* DONE remote admin busted? 
* DONE check android code - @havealoha comments about odd sleep behavior
* ABANDONED test github actions locally on linux
* DONE fix github actions per sasha tip
* tell ttgo to preinstall new bins
* DONE sendtext busted in portduino, due to bytetime calculations
* remove linux dependency in native build
* DONE tcp stream problem in python+pordtuino, server thinks client dropped when client DID NOT DROP
* DONE TCP mode for android, localhost is at 10.0.2.2
* DONE make sure USB still works in android
* add license to portduino and make announcement
* DONE naks are being dropped (though enqueuedLocal) sometimes before phone/PC gets them
* DONE have android fill in if local GPS has poor signal
* optionally restrict position sends to a named channel
* release to beta and amazon
* add reference counting to mesh packets
* allow multiple simultanteous phoneapi connections
* DONE split position.time and last_heard
* DONE update android app to use last_heard
* DONE turn off bluetooth interface ENTIRELY while using serial API (was python client times out on connect sometimes)
* DONE gps assistance from phone not working?
* DONE test latest firmware update with is_router
* DONE firmware OTA updates of is_router true nodes fails?
* DONE add UI in android app to reset to defaults https://github.com/meshtastic/Meshtastic-Android/issues/263 
* DONE TEST THIS! changing channels requires a reboot to take effect https://github.com/meshtastic/Meshtastic-device/issues/752 
* DONE bug report with remote info request timing out
* DONE retest channel changing in android (using sim?)
* DONE move remote admin doc from forum into git
* DONE check crashlytics
* DONE ask for a documentation czar
* DONE timestamps on oled screen are wrong - don't seem to be updating based on message rx (actually: this is expected behavior when no node on the mesh has GPS time)
* DONE add ch-del
* DONE channel hash suffixes are wrong on android
* DONE before next relase: test empty channel sets on android
* DONE channel sharing in android
* DONE test 1.0 firmware update on android
* DONE test 1.1 firmwhttps://github.com/meshtastic/Meshtastic-Android/issues/271are update on android
* DONE test 1.2.10 firmware update on android
* DONE test link sharing on android
* FIXED? luxon bug report - seeing rx acks for nodes that are not on the network
* DONE release py
* DONE show GPS time only if we know what global time is
* DONE android should always provide time to nodes - so that it is easier for the mesh to learn the current time

## Multichannel support

* DONE cleanup the external notification and serial plugins
* non ack version of stress test fails sometimes!
* tx fault test has a bug #734 - * turn off fault 8: https://github.com/meshtastic/Meshtastic-device/issues/734
* DONE move device types into an enum in nodeinfo
* DONE fix android to use new device types for firmware update
* nrf52 should preserve local time across reset
* cdcacm bug on nrf52: emittx thinks it emitted but client sees nothing.  works again later
* nrf52: segger logs have errors in formatting that should be impossible (because not going through serial, try stalling on segger)
* DONE call RouterPlugin for *all* packets - not just Router packets
* DONE generate channel hash from the name of the channel+the psk (not just one or the other)
* DONE send a hint that can be used to select which channel to try and hash against with each message
* DONE remove deprecated
* DONE fix setchannel in phoneapi.cpp
* DONE set mynodeinfo.max_channels
* DONE set mynodeinfo.num_bands (formerly num_channels)
* DONE fix sniffing of non Routing packets
* DONE enable remote setttings access by moving settings operations into a regular plugin (move settings ops out of PhoneAPI)
* DONE move portnum up?
* DONE remove region specific builds from the firmware
* DONE test single channel without python
* DONE Use "default" for name if name is empty
* DONE fix python data packet receiving (nothing showing in log?)
* DONE implement 'get channels' Admin plugin operation
* DONE use get-channels from python
* DONE use get channels & get settings from android
* DONE use set-channel from python
* DONE make settings changes from python work
* DONE pthon should stop fetching channels once we've reached our first empty channel definition (hasSettings == true)
* DONE add check for old devices with new API library
* DONE release python api
* DONE release protobufs
* DONE release to developers
* DONE fix setch-fast in python tool
* age out pendingrequests in the python API
* DONE stress test channel download from python, sometimes it seems like we don't get all replies, bug was due to simultaneous android connection
* DONE combine acks and responses in a single message if possible (do routing plugin LAST and drop ACK if someone else has already replied)
* DONE don't send packets we received from the phone BACK TOWARDS THE PHONE (possibly use fromnode 0 for packets the phone sends?)
* DONE fix 1.1.50 android debug panel display
* DONE test android channel setting
* DONE release to users
* DONE warn in android app about unset regions
* DONE use set-channel from android
* DONE add gui in android app for setting region
* DONE clean up python channel usage
* DONE use bindToChannel to limit admin access for remote nodes
* DONE move channels and radio config out of device settings
* DONE test remote info and remote settings changes
* make python tests more exhaustive
* DONE pick default random admin key
* exclude admin channels from URL?
* make a way to share just secondary channels via URL
* generalize the concept of "shortstrings" use it for both PSKs and well known channel names.  Possibly use a ShortString class.
* use single byte 'well known' channel names for admin, gpio, etc...
* use presence of gpio channel to enable gpio ops, same for serial etc...
* DONE restrict gpio & serial & settings operations to the admin channel (unless local to the current node)
* DONE add channel restrictions for plugins (and restrict routing plugin to the "control" channel)
* stress test multi channel
* DONE investigate @mc-hamster report of heap corruption
* DONE use set-user from android
* untrusted users should not be allowed to provide bogus times (via position broadcasts) to the rest of the mesh.  Invent a new lowest quality notion of UntrustedTime.
* use portuino TCP connection to debug with python API
* document the relationship between want_response (indicating remote node received it) and want_ack (indicating that this message should be sent reliably - and also get acks from the first rx node and naks if it is never delivered)
* DONE android should stop fetching channels once we've reached our first empty channel definition (hasSettings == true)
* DONE warn in python api if we are too new to talk to the device code
* DONE make a post warning about 1.2, telling how to stay on old android & python clients.  link to this from the android dialog message and python version warning.
* DONE "FIXME - move the radioconfig/user/channel READ operations into SettingsMessage as well"
* DONE scrub protobufs to make sure they are absoloute minimum wiresize (in particular Data, ChannelSets and positions)
* DONE change syncword (now ox2b)
* allow chaning packets in single transmission - to increase airtime efficiency and amortize packet overhead
* DONE move most parts of meshpacket into the Data packet, so that we can chain multiple Data for sending when they all have a common destination and key.
* when selecting a MeshPacket for transmit, scan the TX queue for any Data packets we can merge together as a WirePayload.  In the low level send/rx code expand that into multiple MeshPackets as needed (thus 'hiding' from MeshPacket that over the wire we send multiple datapackets
* DONE confirm we are still calling the plugins for messages inbound from the phone (or generated locally)
* DONE confirm we are still multi hop routing flood broadcasts
* DONE confirm we are still doing resends on unicast reliable packets
* add history to routed packets: https://meshtastic.discourse.group/t/packet-source-tracking/2764/2
* add support for full DSR unicast delivery
* DONE move acks into routing
* DONE make all subpackets different versions of data
* DONE move routing control into a data packet
* have phoneapi done via plugin (will allow multiple simultaneous API clients - stop disabling BLE while using phone API)
* use reference counting and dynamic sizing for meshpackets. - use https://docs.microsoft.com/en-us/cpp/cpp/how-to-create-and-use-shared-ptr-instances?view=msvc-160 (already used in arduino)
* let multiple PhoneAPI endpoints work at once
* allow multiple simultaneous bluetooth connections (create the bluetooth phoneapi instance dynamically based on client id)
* DONE figure out how to add micro_delta to position, make it so that phone apps don't need to understand it?
* only send battery updates a max of once a minute
* DONE add python channel selection for sending
* DONE record recevied channel in meshpacket
* test remote settings operations (confirm it works 3 hops away)
* DONE make a primaryChannel global and properly maintain it when the phone sends setChannel
* DONE move setCrypto call into packet send and packet decode code
* implement 'small location diffs' change
* move battery level out of position? 
* consider "A special exception (FIXME, not sure if this is a good idea) - packets that arrive on the local interface 
  are allowed on any channel (this lets the local user do anything)."  Probably by adding a "secure_local_interface" settings bool.
* DOUBLE CHECK android app can still upgrade 1.1 and 1.0 loads
 
For app cleanup:

* don't store redundant User admin or position broadcasts in the ToPhone queue (only keep one per sending node per proto type, and only most recent)
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

# Completed

## eink 1.0

* DONE check email of reported issues
* DONE turn off vbus driving (in bootloader)
* new battery level sensing
* current draw no good
* DONE: fix backlight
* DONE - USB is busted because of power enable mode?
* test CPU voltage? something is bad with RAM (removing eink module does not help)
* test that board leaves bootloader always
* test USB - works in bootloader
* test LEDs
* Test BME280
* test gps
* check GPS fast locking
* tested! dlora
* test eink backlight
* tested! eink
* test buttons
* test battery charging
* test serial flash
* send updated app and bootloader image
* OHH BME280!  THAT IS GREAT!
* make new screen work, ask for datasheet
* say I think you could ship this
* leds seem busted
* fix hw_model: "nrf52unknown"
* use larger icon for meshtastic logo
* send email about variants & faster flash programming - https://github.com/geeksville/Meshtastic-esp32/commit/f110225173a77326aac029321cdb6491bfa640f6
* send PR for bootloader
* fix nrf52 time/date
* send new master bin file
* send email about low power mode problems
* support new flash chip in appload, possibly use low power mode
* swbug! stuck busy tx occurred!
  
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
