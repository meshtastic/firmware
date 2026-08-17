# CrowPanel Advance 4.3/5.0/7.0 ESP32-S3 support

This work targets the Elecrow CrowPanel Advance 4.3, 5.0, and 7.0 inch
ESP32-S3 panels, including the v1.4/v1.5 hardware family. It was developed
from Meshtastic nightly commit `3a7c49972209c7b9adbda245b0d3c1d8337fd8b1`
and tested on physical hardware.

## Working hardware and features

- SX1262 LoRa using the v1.2+ native radio-header wiring. NSS/CS is GPIO 8;
  no GPIO 0 CS jumper is required.
- STC controller at I2C address `0x30` on SDA GPIO 15 and SCL GPIO 16.
- STC-controlled display brightness and backlight power.
- GT911 touch on the shared I2C bus.
- STC buzzer at boot and for received-message notifications. The notification
  follows Meshtastic's buzzer mode and External Notification settings.
- INA219 battery monitoring at I2C address `0x40`, including battery percentage.
- A shared I2C lock for STC, touch, and INA219 traffic.
- Persisted display brightness, screen timeout, and touch calibration.
- Hardware model 144, `CROWPANEL_ADV_43_50_70`, so apps can identify the
  large CrowPanel family separately from the 2.4/2.8 inch model.
- Wi-Fi map tiles downloaded asynchronously from Google Maps.

The tested SX1262 pin assignment is:

| Signal | ESP32-S3 GPIO |
| --- | ---: |
| NSS/CS | 8 |
| SCK | 5 |
| MISO | 4 |
| MOSI | 6 |
| RESET | 19 |
| DIO1/IRQ | 20 |
| BUSY | 2 |

## Buzzer configuration

In the Meshtastic UI, set Device > Buzzer Mode to **All Enabled**. Under
External Notification, enable the module and Alert Message Buzzer. PWM may
remain enabled because this variant redirects the notification to the STC
controller; I2S should remain disabled. An output duration of 1000 ms provides
a clear message alert. The 250 ms default produces only a short buzz.

## Maps and SD-card status

Map tiles currently come directly from Google Maps over an active Wi-Fi
connection. The map loader is asynchronous so HTTP and image decoding do not
block the display task, and it intentionally does not look for map tiles on the
SD card first.

SD-card initialization on the tested v1.4/v1.5 panels is still a work in
progress. Because the Meshtastic UI's Backup & Restore feature stores keys on
the SD card, that feature can remain unavailable until SD initialization is
resolved. SD-backed/offline maps are not claimed as working by this patch.

## Test firmware

`firmware/Meshtastic-CrowPanel-4.3-5.0-7.0-v2.8.0-STC-GoogleMaps-GPIO8-CS.bin`
is the application image tested on hardware. Its SHA-256 is:

`B7EFB4D9855630F8AD0E7D0B14B3DD83AC17248093EE97EAA912FB7C713CFD0C`

Use Meshtastic Flasher's **Pick your own file** option. This application image
is written at offset `0x10000`. A full erase is normally unnecessary and would
remove the node identity, channels, Wi-Fi credentials, and display settings.

The GPIO 8 image is for panels with the original v1.2+ CS routing. A panel that
has been physically modified to isolate GPIO 8 and jumper CS to GPIO 0 needs
either the modification reversed or a separate GPIO 0 build.
