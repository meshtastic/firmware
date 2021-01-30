# SerialPlugin

A simple interface to send messages over the mesh network by sending strings
over a serial port.

Default is to use RX GPIO 16 and TX GPIO 17.


# Basic Usage:

        1) Enable the plugin by setting serialplugin_enabled to 1.
        2) Set the pins (serialplugin_rxd / serialplugin_rxd) for your preferred RX and TX GPIO pins.
           On tbeam, recommend to use:
                RXD 35
                TXD 15
        3) Set serialplugin_timeout to the amount of time to wait before we consider
           your packet as "done".
        4) (Optional) In SerialPlugin.h set the port to PortNum_TEXT_MESSAGE_APP if you want to
           send messages to/from the general text message channel.
        5) Connect to your device over the serial interface at 38400 8N1.
        6) Send a packet up to 240 bytes in length. This will get relayed over the mesh network.
        7) (Optional) Set serialplugin_echo to 1 and any message you send out will be echoed back
           to your device.

# TODO (in this order):

* Define a verbose RX mode to report on mesh and packet infomration.
        - This won't happen any time soon.

# Known Problems

    * Until the plugin is initilized by the startup sequence, the TX pin is in a floating
      state. Device connected to that pin may see this as "noise".
    * Will not work on NRF and the Linux device targets.
    
# Need help?

Need help with this plugin? Post your question on the Meshtastic Discourse:

https://meshtastic.discourse.group
