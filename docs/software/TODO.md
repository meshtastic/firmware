# High priority

Items to complete soon (next couple of alpha releases).

- lower wait_bluetooth_secs to 30 seconds once we have the GPS power on (but GPS in sleep mode) across light sleep. For the time
  being I have it set at 2 minutes to ensure enough time for a GPS lock from scratch.

# Medium priority

Items to complete before the first beta release.

- Use 32 bits for message IDs
- Use fixed32 for node IDs
- Don't store position packets in the to phone fifo if we are disconnected. The phone will get that info for 'free' when it
  fetches the fresh nodedb.
- Use the RFM95 sequencer to stay in idle mode most of the time, then automatically go to receive mode and automatically go from transmit to receive mode. See 4.2.8.2 of manual.
- possibly switch to https://github.com/SlashDevin/NeoGPS for gps comms
- good source of battery/signal/gps icons https://materialdesignicons.com/
- research and implement better mesh algorithm - investigate changing routing to https://github.com/sudomesh/LoRaLayer2 ?
- check fcc rules on duty cycle. we might not need to freq hop. https://www.sunfiretesting.com/LoRa-FCC-Certification-Guide/
- use fuse bits to store the board type and region. So one load can be used on all boards
- the BLE stack is leaking about 200 bytes each time we go to light sleep
- rx signal measurements -3 marginal, -9 bad, 10 great, -10 means almost unusable. So scale this into % signal strength. preferably as a graph, with an X indicating loss of comms.
- assign every "channel" a random shared 8 bit sync word (per 4.2.13.6 of datasheet) - use that word to filter packets before even checking CRC. This will ensure our CPU will only wake for packets on our "channel"
- Note: we do not do address filtering at the chip level, because we might need to route for the mesh
  is in cleartext (so that nodes will route for other radios that are cryptoed with a key we don't know)
- add frequency hopping, dependent on the gps time, make the switch moment far from the time anyone is going to be transmitting
- share channel settings over Signal (or qr code) by embedding an an URL which is handled by the MeshUtil app.
- publish update articles on the web

# Pre-beta priority

During the beta timeframe the following improvements 'would be nice' (and yeah - I guess some of these items count as features, but it is a hobby project ;-) )

- If the phone doesn't read fromradio mailbox within X seconds, assume the phone is gone and we can stop queing location msgs
  for it (because it will redownload the nodedb when it comes back)
- Figure out why the RF95 ISR is never seeing RH_RF95_VALID_HEADER, so it is not protecting our rx packets from getting stomped on by sends
- fix the frequency error reading in the RF95 RX code (can't do floating point math in an ISR ;-)
- See CustomRF95::send and fix the problem of dropping partially received packets if we want to start sending
- make sure main cpu is not woken for packets with bad crc or not addressed to this node - do that in the radio hw
- triple check fcc compliance
- pick channel center frequency based on channel name? "dolphin" would hash to 900Mhz, "cat" to 905MHz etc? allows us to hide the concept of channel # from hte user.
- scan to find channels with low background noise? (Use CAD mode of the RF95 to automatically find low noise channels)
- make a no bluetooth configured yet screen - include this screen in the loop if the user hasn't yet paired
- if radio params change fundamentally, discard the nodedb
- reneable the bluetooth battery level service on the T-BEAM, because we can read battery level there

# Spinoff project ideas

- an open source version of https://www.burnair.ch/skynet/
- a paragliding app like http://airwhere.co.uk/
- a version with a solar cell for power, just mounted high to permanently provide routing for nodes in a valley. Someone just pointed me at disaster.radio
- How do avalanche beacons work? Could this do that as well? possibly by using beacon mode feature of the RF95?
- provide generalized (but slow) internet message forwarding servie if one of our nodes has internet connectivity

# Low priority

Items after the first final candidate release.

- use variable length arduino Strings in protobufs (instead of current fixed buffers)
- use BLEDevice::setPower to lower our BLE transmit power - extra range doesn't help us, it costs amps and it increases snoopability
- make an install script to let novices install software on their boards
- use std::map<NodeInfo\*, std::string> in node db
- make a HAM build: yep - that's a great idea. I'll add it to the TODO. should be pretty painless - just a new frequency list, a bool to say 'never do encryption' and use hte callsign as that node's unique id. -from Girts
- don't forward redundant pings or ping responses to the phone, it just wastes phone battery
- use https://platformio.org/lib/show/1260/OneButton if necessary
- don't send location packets if we haven't moved
- scrub default radio config settings for bandwidth/range/speed
- answer to pings (because some other user is looking at our nodeinfo) with our latest location (not a stale location)
- show radio and gps signal strength as an image
- only BLE advertise for a short time after the screen is on and button pressed - to save power and prevent people for sniffing for our BT app.
- make mesh aware network timing state machine (sync wake windows to gps time)
- split out the software update utility so other projects can use it. Have the appload specify the URL for downloads.
- read the PMU battery fault indicators and blink/led/warn user on screen
- the AXP debug output says it is trying to charge at 700mA, but the max I've seen is 180mA, so AXP registers probably need to be set to tell them the circuit can only provide 300mAish max. So that the low charge rate kicks in faster and we don't wear out batteries.
- increase the max charging rate a bit for 18650s, currently it limits to 180mA (at 4V). Work backwards from the 500mA USB limit (at 5V) and let the AXP charge at that rate.
- discard very old nodedb records (> 1wk)
- using the genpartitions based table doesn't work on TTGO so for now I stay with my old memory map
- We let anyone BLE scan for us (FIXME, perhaps only allow that until we are paired with a phone and configured)
- use two different buildenv flags for ttgo vs lora32. https://docs.platformio.org/en/latest/ide/vscode.html#key-bindings
- sim gps data for testing nodes that don't have hardware
- do debug serial logging to android over bluetooth
- break out my bluetooth OTA software as a seperate library so others can use it
- Heltec LoRa32 has 8MB flash, use a bigger partition table if needed - TTGO is 4MB but has PSRAM
- add a watchdog timer
- handle millis() rollover in GPS.getTime - otherwise we will break after 50 days
- report esp32 device code bugs back to the mothership via android

# Done

- change the partition table to take advantage of the 4MB flash on the wroom: http://docs.platformio.org/en/latest/platforms/espressif32.html#partition-tables
- wrap in nice MeshRadio class
- add mesh send & rx
- make message send from android go to service, then to mesh radio
- make message receive from radio go through to android
- test loopback tx/rx path code without using radio
- notify phone when rx packets arrive, currently the phone polls at startup only
- figure out if we can use PA_BOOST - yes, it seems to be on both boards
- implement new ble characteristics
- have MeshService keep a node DB by sniffing user messages
- have a state machine return the correct FromRadio packet to the phone, it isn't always going to be a MeshPacket. Do a notify on fromnum to force the radio to read our state machine generated packets
- send my_node_num when phone sends WantsNodes
- have meshservice periodically send location data on mesh (if device has a GPS)
- implement getCurrentTime() - set based off gps but then updated locally
- make default owner record have valid usernames
- message loop between node 0x28 and 0x7c
- check in my radiolib fixes
- figure out what is busted with rx
- send our owner info at boot, reply if we see anyone send theirs
- add manager layers
- confirm second device receives that gps message and updates device db
- send correct hw vendor in the bluetooth info - needed so the android app can update different radio models
- correctly map nodeids to nodenums, currently we just do a proof of concept by always doing a broadcast
- add interrupt detach/sleep mode config to lora radio so we can enable deepsleep without panicing
- make jtag work on second board
- implement regen owner and radio prefs
- use a better font
- make nice screens (boot, about to sleep, debug info (gps signal, #people), latest text, person info - one frame per person on network)
- turn framerate from ui->state.frameState to 1 fps (or less) unless in transition
- switch to my gui layout manager
- make basic gui. different screens: debug, one page for each user in the user db, last received text message
- make button press cycle between screens
- save our node db on entry to sleep
- fix the logo
- sent/received packets (especially if a node was just reset) have variant of zero sometimes - I think there is a bug (race-condtion?) in the radio send/rx path.
- DONE dynamic nodenum assignment tasks
- make jtag debugger id stable: https://askubuntu.com/questions/49910/how-to-distinguish-between-identical-usb-to-serial-adapters
- reported altitude is crap
- good tips on which bands might be more free https://github.com/TheThingsNetwork/ttn/issues/119
- finish power measurements (GPS on during sleep vs LCD on during sleep vs LORA on during sleep) and est battery life
- make screen sleep behavior work
- make screen advance only when a new node update arrives, a new text arrives or the user presses a button, turn off screen after a while
- after reboot, channel number is getting reset to zero! fix!
- send user and location events much less often
- send location (or if not available user) when the user wakes the device from display sleep (both for testing and to improve user experience)
- make real implementation of getNumOnlineNodes
- very occasionally send our position and user packet based on the schedule in the radio info (if for nothing else so that other nodes update last_seen)
- show real text info on the text screen
- apply radio settings from android land
- cope with nodes that have 0xff or 0x00 as the last byte of their mac
- allow setting full radio params from android
- add receive timestamps to messages, inserted by esp32 when message is received but then shown on the phone
- update build to generate both board types
- have node info screen show real info (including distance and heading)
- blink the power led less often
- have radiohead ISR send messages to RX queue directly, to allow that thread to block until we have something to send
- move lora rx/tx to own thread and block on IO
- keep our pseudo time moving forward even if we enter deep sleep (use esp32 rtc)
- for non GPS equipped devices, set time from phone
- GUI on oled hangs for a few seconds occasionally, but comes back
- update local GPS position (but do not broadcast) at whatever rate the GPS is giving it
- don't send our times to other nodes
- don't trust times from other nodes
- draw compass rose based off local walking track
- add requestResponse optional bool - use for location broadcasts when sending tests
- post sample video to signal forum
- support non US frequencies
- send pr https://github.com/ThingPulse/esp8266-oled-ssd1306 to tell them about this project
- document rules for sleep wrt lora/bluetooth/screen/gps. also: if I have text messages (only) for the phone, then give a few seconds in the hopes BLE can get it across before we have to go back to sleep.
- wake from light sleep as needed for our next scheduled periodic task (needed for gps position broadcasts etc)
- turn bluetooth off based on our sleep policy
- blink LED while in LS sleep mode
- scrolling between screens based on press is busted
- Use Neo-M8M API to put it in sleep mode (on hold until my new boards arrive)
- update the prebuilt bins for different regulatory regions
- don't enter NB state if we've recently talked to the phone (to prevent breaking syncing or bluetooth sw update)
- have sw update prevent BLE sleep
- manually delete characteristics/descs
- leave lora receiver always on
- protobufs are sometimes corrupted after sleep!
- stay awake while charging
- check gps battery voltage
- if a position report includes ground truth time and we don't have time yet, set our clock from that. It is better than nothing.
- retest BLE software update for both board types
- report on wikifactory
- send note to the guy who designed the cases
- turn light sleep on aggressively (while lora is on but BLE off)
- Use the Periodic class for both position and user periodic broadcasts
- don't treat north as up, instead adjust shown bearings for our guess at the users heading (i.e. subtract one from the other)
- sendToMesh can currently block for a long time, instead have it just queue a packet for a radio freertos thread
- don't even power on bluetooth until we have some data to send to the android phone. Most of the time we should be sleeping in a lowpower "listening for lora" only mode. Once we have some packets for the phone, then power on bluetooth
  until the phone pulls those packets. Ever so often power on bluetooth just so we can see if the phone wants to send some packets. Possibly might need ULP processor to help with this wake process.
- do hibernation mode to get power draw down to 2.5uA https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/
- fix GPS.zeroOffset calculation it is wrong
- (needs testing) fixed the following during a plane flight:
  Have state machine properly enter deep sleep based on loss of mesh and phone comms.
  Default to enter deep sleep if no LORA received for two hours (indicates user has probably left the mesh).
- (fixed I think) text messages are not showing on local screen if screen was on
- add links to todos
- link to the kanban page
- add a getting started page
- finish mesh alg reeval
- ublox gps parsing seems a little buggy (we shouldn't be sending out read solution commands, the device is already broadcasting them)
- turn on gps https://github.com/sparkfun/SparkFun_Ublox_Arduino_Library/blob/master/examples/Example18_PowerSaveMode/Example18_PowerSaveMode.ino
- switch gps to 38400 baud https://github.com/sparkfun/SparkFun_Ublox_Arduino_Library/blob/master/examples/Example11_ResetModule/Example2_FactoryDefaultsviaSerial/Example2_FactoryDefaultsviaSerial.ino
- Use Neo-M8M API to put it in sleep mode
- use gps sleep mode instead of killing its power (to allow fast position when we wake)
- enable fast lock and low power inside the gps chip
- Make a FAQ
- add a SF12 transmit option for _super_ long range
- figure out why this fixme is needed: "FIXME, disable wake due to PMU because it seems to fire all the time?"
- "AXP192 interrupt is not firing, remove this temporary polling of battery state"
- make debug info screen show real data (including battery level & charging) - close corresponding github issue
- remeasure wake time power draws now that we run CPU down at 80MHz
