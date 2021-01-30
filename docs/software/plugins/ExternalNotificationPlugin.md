The ExternalNotification Plugin will allow you to connect a speaker, LED or other device to notify you when a message has been received from the mesh network.

# Configuration

These are the settings that can be configured.

    ext_notification_plugin_enabled
        Is the plugin enabled?
        
        0 = Disabled (Default)
        1 = Enabled

    ext_notification_plugin_active
        Is your external circuit triggered when our GPIO is low or high?

        0 = Active Low (Default)
        1 = Active High

    ext_notification_plugin_alert_message
        Do you want to be notified on an incoming message?

        0 = Disabled (Default)
        1 = Alert when a text message comes

    ext_notification_plugin_alert_bell
        Do you want to be notified on an incoming bell?

        0 = Disabled (Default)
        1 = Alert when the bell character is received

    ext_notification_plugin_output
        What GPIO is your external circuit attached?

        GPIO of the output. (Default = 13)

    ext_notification_plugin_output_ms
        How long do you want us to trigger your external circuit?
    
        Amount of time in ms for the alert. Default is 1000.


# Usage Notes

For basic usage, start with:

	ext_notification_plugin_enabled = 1
	ext_notification_plugin_alert_message = 1
    
Depending on how your external cirtcuit configured is configured, you may need to set the active state to true.

	ext_notification_plugin_active = 1
    
# External Hardware

Be mindful of the max current sink and source of the esp32 GPIO. The easiest devices to interface with would be either an LED or Active Buzzer.

Ideas for external hardware:

* LED
* Active Buzzer
* Flame thrower
* Strobe Light
* Siren
    
# Known Problems

* This won't directly support an passive (normal) speaker as it does not generate any audio wave forms.
* This currently only supports the esp32. Other targets may be possible, I just don't have to test with.
* This plugin only monitors text messages. We won't trigger on any other packet types.

# Need more help?

Go to the Meshtastic Discourse Group if you have any questions or to share how you have used this.

https://meshtastic.discourse.group
