# Remote Hardware Service

These are 'programmer focused' notes on using the "remote hardware" feature.  

Note: This feature uses a preinstalled plugin in the device code and associated commandline flags/classes in the python code.  You'll need to be running at least version 1.2.23 (or later) of the python and device code to use this feature.

You can get the latest python tool/library with "pip3 install --upgrade meshtastic" on Windows/Linux/OS-X.

## Supported operations in the initial release

- Set any GPIO
- Read any GPIO
- Receive notification of changes in any GPIO.

## Setup

GPIO access is fundamentally 'dangerous' because invalid options can physically burn-up hardware.  To prevent access from untrusted users you must first make a "gpio" channel that is used for authenticated access to this feature.  You'll need to install this channel on both the local and remote node.

The procedure using the python command line tool is:

1. Connect local device via USB
2. "meshtastic --ch-add admin; meshtastic --info" thn copy the (long) "Complete URL" that info printed
3. Connect remote device via USB (or use the remote admin feature to reach it through the mesh, but that's beyond the scope of this tutorial)
4. "meshtastic --seturl theurlyoucopiedinstep2"

Now both devices can talk over the "gpio" channel.

## Doing GPIO operations

Here's some examples using the command line tool.  

## Using GPIOs from python

You can programmatically do operations from your own python code by using the meshtastic "RemoteHardwareClient" class - see the python documentation for more details.

Writing a GPIO
```
meshtastic  --port /dev/ttyUSB0 --gpio-wrb 4 1 --dest \!28979058 
Connected to radio
Writing GPIO mask 0x10 with value 0x10 to !28979058
```

Reading a GPIO
```
meshtastic --port /dev/ttyUSB0 --gpio-rd 0x10 --dest \!28979058 
Connected to radio
Reading GPIO mask 0x10 from !28979058
GPIO read response gpio_value=16
```

Watching for GPIO changes:
```
meshtastic --port /dev/ttyUSB0 --gpio-watch 0x10 --dest \!28979058 
Connected to radio
Watching GPIO mask 0x10 from !28979058
Received RemoteHardware typ=GPIOS_CHANGED, gpio_value=16
Received RemoteHardware typ=GPIOS_CHANGED, gpio_value=0
Received RemoteHardware typ=GPIOS_CHANGED, gpio_value=16
< press ctrl-c to exit >
```