# Temporary notes to self: delete before sending PR.

Why sw update is budsted:
		case ESP_GATTS_WRITE_EVT: {
// 
			if (param->write.handle == m_handle) {
				if (param->write.is_prep) {
					m_value.addPart(param->write.value, param->write.len);
				} else {
					setValue(param->write.value, param->write.len);
				}

				log_d(" - Response to write event: New value: handle: %.2x, uuid: %s",
						getHandle(), getUUID().toString().c_str());

				char* pHexData = BLEUtils::buildHexData(nullptr, param->write.value, param->write.len);
				log_d(" - Data: length: %d, data: %s", param->write.len, pHexData);
				free(pHexData);

Has a 100 byte limit, also it is converting to a fucking string 

## misc todo

* DONE save auth and packet counts in NVS
* DONE code deep sleep, leave screen off if waked without button, enter sleep after packet sent or timeout
* DONE store our session keys to flash and if found, then at boot use them instead of joining.
* DONE dynamically probe for screen and if not installed - do not use
* test basic preshared key config and see if it works
* test more advanced auth and see if it works
* test that rejoin with advanced auth works
* test construct node id from macaddr
* test screen and axp192 probing
* DONE test that board works when we ignore display
* test deep sleep
* test deep sleep with gps & lora off
* edit readme to be platformio based
* if current position is near last reported position, only report once per hour
* measure sleep current / calculate battery life
* split things network code into a esp32 library on platformio
* make first public release of "t-beam tracker", empasize battery life and price
* turn on our led while we are on?
* experiment with larger SF factors for higher range
* include battery level in broadcast packet
* stop calling start join, instead just send a packet and the join attempt will happen automatically

## eventual / later TODO

* make longpress forget network joining / force rejoin

# deep sleep

## pre sleep
save packet count to NVS
save lora state to RTC ram
turn off GPS
turn off LORA
turn off screen - possibly can't use the axp because it kills i2c
turn off wifi (if using it)
enter deep sleep - waiting for 5 min timeout or button press

## on wake
turn on power the same as normal boot (except do not power on screen if we booted because of timeout - we will be running 'headless')
reinit all hardware 
show boot screen (if screen is powered)
restore packet count from NVS
restore lora state from RTC ram (if possible)
wait for gps lock (max 5 seconds), when it happens send our packet
update screen (if screen powered)
send position via lora and wait for ack (or timeout after FIXME seconds)
if screen is on wait an extra 5 secs
enter deep sleep

# Packet count persistence algorithm:

On each entry to deep sleep, copy the packet count to flash filesystem.  At boot read that value from the flash filesystem.  SOLVED: Check flash filesystem wear rules and assume X sleeps per day.  How many days is that?
https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/nvs_flash.html
https://esp32.com/viewtopic.php?t=3990
writing once a second a 64 byte NVS record (it is actually 32 bytes for an int key/val pair), that would wear out the flash in a year.  Writing once every 5 minutes would be 300 years.  yay!  Do this.

On each wake from deep sleep, read the last known packet count from GPS BBR use it for future packet sends (if blank then skip).  Each time we send a packet, update the count in the GPS RAM.  As long as BBR ram doesn't die this will always work.  PROBLEM: What if BBR RAM does die


