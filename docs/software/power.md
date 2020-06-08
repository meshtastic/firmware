# Power Management State Machine

i.e. sleep behavior

## Power measurements

Since one of the main goals of this project is long battery life, it is important to consider that in our software/protocol design. Based on initial measurements it seems that the current code should run about three days between charging, and with a bit more software work (see the [TODO list](TODO.md)) a battery life of eight days should be quite doable. Our current power measurements/model is in [this spreadsheet](https://docs.google.com/spreadsheets/d/1ft1bS3iXqFKU8SApU8ZLTq9r7QQEGESYnVgdtvdT67k/edit?usp=sharing).

## States

From lower to higher power consumption.

- Super-deep-sleep (SDS) - everything is off, CPU, radio, bluetooth, GPS. Only wakes due to timer or button press. We enter this mode only after no radio comms for a few hours, used to put the device into what is effectively "off" mode.
  onEntry: setBluetoothOn(false), call doDeepSleep
  onExit: (standard bootup code, starts in DARK)

- deep-sleep (DS) - CPU is off, radio is on, bluetooth and GPS is off. Note: This mode is never used currently, because it only saves 1.5mA vs light-sleep
  (Not currently used)

- light-sleep (LS) - CPU is suspended (RAM stays alive), radio is on, bluetooth is off, GPS is off. Note: currently GPS is not turned
  off during light sleep, but there is a TODO item to fix this.
  NOTE: On NRF52 platforms (because CPU current draw is so low), light-sleep state is never used.  
   onEntry: setBluetoothOn(false), setGPSPower(false), doLightSleep()
  onIdle: (if we wake because our led blink timer has expired) blink the led then go back to sleep until we sleep for ls_secs
  onExit: setGPSPower(true), start trying to get gps lock: gps.startLock(), once lock arrives service.sendPosition(BROADCAST)

- No bluetooth (NB) - CPU is running, radio is on, GPS is on but bluetooth is off, screen is off.
  onEntry: setBluetoothOn(false)
  onExit:

- running dark (DARK) - Everything is on except screen
  onEntry: setBluetoothOn(true)
  onExit:

- full on (ON) - Everything is on
  onEntry: setBluetoothOn(true), screen.setOn(true)
  onExit: screen.setOn(false)

- serial API usage (SERIAL) - Screen is on, device doesn't sleep, bluetooth off
  onEntry: setBluetooth off, screen on
  onExit:

## Behavior

### events that increase CPU activity

- At cold boot: The initial state (after setup() has run) is DARK
- While in DARK: if we receive EVENT_BOOT, transition to ON (and show the bootscreen). This event will be sent if we detect we woke due to reset (as opposed to deep sleep)
- While in LS: Once every position_broadcast_secs (default 15 mins) - the unit will wake into DARK mode and broadcast a "networkPing" (our position) and stay alive for wait_bluetooth_secs (default 30 seconds). This allows other nodes to have a record of our last known position if we go away and allows a paired phone to hear from us and download messages.
- While in LS: Every send*owner_interval (defaults to 4, i.e. one hour), when we wake to send our position we \_also* broadcast our owner. This lets new nodes on the network find out about us or correct duplicate node number assignments.
- While in LS/NB/DARK: If the user presses a button (EVENT_PRESS) we go to full ON mode for screen_on_secs (default 30 seconds). Multiple presses keeps resetting this timeout
- While in LS/NB/DARK: If we receive new text messages (EVENT_RECEIVED_TEXT_MSG), we go to full ON mode for screen_on_secs (same as if user pressed a button)
- While in LS: while we receive packets on the radio (EVENT_RECEIVED_PACKET) we will wake and handle them and stay awake in NB mode for min_wake_secs (default 10 seconds)
- While in NB: If we do have packets the phone (EVENT_PACKETS_FOR_PHONE) would want we transition to DARK mode for wait_bluetooth secs.
- While in DARK/ON: If we receive EVENT_BLUETOOTH_PAIR we transition to ON and start our screen_on_secs timeout
- While in NB/DARK/ON: If we receive EVENT_NODEDB_UPDATED we transition to ON (so the new screen can be shown)
- While in DARK: While the phone talks to us over BLE (EVENT_CONTACT_FROM_PHONE) reset any sleep timers and stay in DARK (needed for bluetooth sw update and nice user experience if the user is reading/replying to texts)
- while in LS/NB/DARK: if SERIAL_CONNECTED, go to serial

### events that decrease cpu activity

- While in SERIAL: if SERIAL_DISCONNECTED, go to NB
- While in ON: If PRESS event occurs, reset screen_on_secs timer and tell the screen to handle the pess
- While in ON: If it has been more than screen_on_secs since a press, lower to DARK
- While in DARK: If time since last contact by our phone exceeds phone_timeout_secs (15 minutes), we transition down into NB mode
- While in DARK or NB: If nothing above is forcing us to stay in a higher mode (wait_bluetooth_secs, min_wake_secs) we will lower down to LS state
- While in LS: If either phone_sds_timeout_secs (default 2 hr) or mesh_sds_timeout_secs (default 2 hr) are exceeded we will lower into SDS mode for sds_secs (default 1 yr) (or a button press). (Note: phone_sds_timeout_secs is currently disabled for now, because most users
  are using without a phone)
- Any time we enter LS mode: We stay in that until an interrupt, button press or other state transition. Every ls_secs (default 1 hr) and let the arduino loop() run one iteration (FIXME, not sure if we need this at all), and then immediately reenter lightsleep mode on the CPU.

TODO: Eventually these scheduled intervals should be synchronized to the GPS clock, so that we can consider leaving the lora receiver off to save even more power.
TODO: In NB mode we should put cpu into light sleep any time we really aren't that busy (without declaring LS state) - i.e. we should leave GPS on etc...

# Low power consumption tasks

General ideas to hit the power draws our spreadsheet predicts. Do the easy ones before beta, the last 15% can be done after 1.0.

- don't even power on the gps until someone else wants our position, just stay in lora deep sleep until press or rxpacket (except for once an hour updates)
- (possibly bad idea - better to have lora radio always listen - check spreadsheet) have every node wake at the same tick and do their position syncs then go back to deep sleep
- lower BT announce interval to save battery
- change to use RXcontinuous mode and config to drop packets with bad CRC (see section 6.4 of datasheet) - I think this is already the case
- have mesh service run in a thread that stays blocked until a packet arrives from the RF95
- platformio sdkconfig CONFIG_PM and turn on modem sleep mode
- keep cpu 100% in deepsleep until irq from radio wakes it. Then stay awake for 30 secs to attempt delivery to phone.
- use https://lastminuteengineers.com/esp32-sleep-modes-power-consumption/ association sleep pattern to save power - but see https://github.com/espressif/esp-idf/issues/2070 and https://esp32.com/viewtopic.php?f=13&t=12182 it seems with BLE on the 'easy' draw people are getting is 80mA
- stop using loop() instead use a job queue and let cpu sleep
- measure power consumption and calculate battery life assuming no deep sleep
- do lowest sleep level possible where BT still works during normal sleeping, make sure cpu stays in that mode unless lora rx packet happens, bt rx packet happens or button press happens
- optionally do lora messaging only during special scheduled intervals (unless nodes are told to go to low latency mode), then deep sleep except during those intervals - before implementing calculate what battery life would be with this feature
- see section 7.3 of https://cdn.sparkfun.com/assets/learn_tutorials/8/0/4/RFM95_96_97_98W.pdf and have hope radio wake only when a valid packet is received. Possibly even wake the ESP32 from deep sleep via GPIO.
- never enter deep sleep while connected to USB power (but still go to other low power modes)
- when main cpu is idle (in loop), turn cpu clock rate down and/or activate special sleep modes. We want almost everything shutdown until it gets an interrupt.
