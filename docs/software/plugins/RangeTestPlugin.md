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

## Other things to keep in mind

Be sure to turn off either the plugin configured as a sender or the device where the plugin setup as sender when not in use. This will use a lot of time on air and will spam your channel.

Also be mindful of your space usage on the file system. It has protections from filling up the space but it's best to delete old range test results.


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

# Need more help?

Go to the Meshtastic Discourse Group if you have any questions or to share how you have used this.

https://meshtastic.discourse.group
