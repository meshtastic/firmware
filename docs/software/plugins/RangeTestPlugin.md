# About

The RangeTest Plugin will help you perform range and coverage tests.

# Configuration

These are the settings that can be configured.

    range_test_plugin_enabled
        Is the plugin enabled?
        
        0 = Disabled (Default)
        1 = Enabled

    range_test_plugin_save
        If enabled, we will save a log of all received messages to /static/rangetest.csv which you can access from the webserver. We will abort
        writing if there is less than 50k of space on the filesystem to prevent filling up the storage.

        0 = Disabled (Default)
        1 = Enabled

    range_test_plugin_sender
        Number of seconds to wait between sending packets. Using the long_slow channel configuration, it's best not to go more frequent than once every 60 seconds. You can be more agressive with faster settings. 0 is default which disables sending messages.

# Usage Notes

For basic usage, you will need two devices both with a GPS. A device with a paired phone with GPS may work, I have not tried it. 

The first thing to do is to turn on the plugin. With the plugin turned on, the other settings will be available:

	range_test_plugin_enabled = 1

If you want to send a message every 60 seconds:
    
	range_test_plugin_sender = 60

To save a log of the messages:

    range_test_plugin_save = 1

Recommended settings for a sender at different radio settings:

    Long Slow  ... range_test_plugin_sender = 60
    Long Alt   ... range_test_plugin_sender = 30
    Medium     ... range_test_plugin_sender = 15
    Short Fast ... range_test_plugin_sender = 15

## Python API Examples

### Sender 

    meshtastic --set range_test_plugin_enabled 1
    meshtastic --set range_test_plugin_sender 60

### Receiver

    meshtastic --set range_test_plugin_enabled 1
    meshtastic --set range_test_plugin_save 1

## Other things to keep in mind

Be sure to turn off either the plugin configured as a sender or the device where the plugin setup as sender when not in use. This will use a lot of time on air and will spam your channel.

Also be mindful of your space usage on the file system. It has protections from filling up the space but it's best to delete old range test results.

# Application Examples

## Google Integration

@jfirwin on our forum [meshtastic.discourse.org](https://meshtastic.discourse.group/t/new-plugin-rangetestplugin/2591/49?u=mc-hamster) shared how to integrate the resulting csv file with Google Products.

### Earth

Steps:

1. [Download](https://www.google.com/earth/versions/#download-pro) 1 and open Google Earth
   1. Select File > Import
   2. Select CSV
   3. Select Delimited, Comma
   4. Make sure the button that states “This dataset does not contain latitude/longitude information, but street addresses” is unchecked
   5. Select “rx lat” & “rx long” for the appropriate lat/lng fields
   6. Click finish
2. When it prompts you to create a style template, click yes.
   1. Set the name field to whichever column you want to be displayed on the map (don’t worry about this too much, when you click on an icon, all the relavant data appears)
   2. select a color, icon, etc. and hit ok.

Your data will load onto the map, make sure to click the checkbox next to your dataset in the sidebar to view it.

### My Maps

You can use [My Maps](http://mymaps.google.com/). It takes CSVs and the whole interface is much easier to work with.

Google has instructions on how to do that [here](https://support.google.com/mymaps/answer/3024836?co=GENIE.Platform%3DDesktop&hl=en#zippy=%2Cstep-prepare-your-info%2Cstep-import-info-into-the-map).

You can style the ranges differently based on the values, so you can have the pins be darker the if the SNR or RSSI (if that gets added) is higher. 

# Known Problems

* If turned on, using mesh network will become unwieldly because messages are sent over the same channel as the other messages. See TODO below.

# TODO

* Right now range test messages go over the TEXT_MESSAGE_APP port. We need a toggle to switch to optionally send over RANGE_TEST_APP.

# FAQ

Q: Where is rangetest.csv saved?
A: Turn on the WiFi on your device as either a WiFi client or a WiFi AP. Once you can connect to your device, go to /static and you will see rangetest.csv.

Q: Do I need to have WiFi turned on for the file to be saved?
A: Nope, it'll just work.

Q: Do I need a phone for this plugin?
A: There's no need for a phone.

Q: Can I use this as a message logger?
A: While it's not the intended purpose, sure, why not. Do it!

Q: What will happen if I run out of space on my device?
A: We have a protection in place to keep you from completly filling up your device. This will make sure that other device critical functions will continue to work. We will reserve at least 50k of free space.

Q: What do I do with the rangetest.csv file when I'm done?
A: Go to /static and delete the file.

Q: Can I use this as a sender while on battery power?
A: Yes, but your battery will run down quicker than normal. While sending, we tell the device not to go into low-power mode since it needs to keep to a fairly strict timer.

Q: Why is this operating on incoming messages instead of the existing location discovery protocol?
A: This plugin is still young and currently supports monitoring just one port at a time. I decided to use the existing message port because that is easy to test with. A future version will listen to multiple ports to be more promiscuous. 

# Need more help?

Go to the Meshtastic Discourse Group if you have any questions or to share how you have used this.

https://meshtastic.discourse.group
