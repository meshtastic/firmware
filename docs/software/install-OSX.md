(Here's some quick tips on installing the device code from OS-X, thanks to @android606)

First time using LoRa for anything, just checking it out.

I bought a T-Beam on eBay, followed the instructions to install the firmware here:
[https://github.com/meshtastic/Meshtastic-esp32](https://github.com/meshtastic/Meshtastic-esp32)

I'm using a Mac for this, so that might account for differences in the steps to get it working. I just swapped out my SSD last month, I'm using a pretty fresh install of OS X 10.15.3/Catalina.

I got it working fairly smoothly, but there were two hang-ups I thought I'd mention:

1. I am about 0% familiar with Python, so there were some issues getting esptool.py working. Basically, this OS X comes with Python 2.7 and no pip. Pip installed okay, so I used it to install esptool. Esptool appeared to install correctly, but I couldn't get it to work to save my life. Simply typing "esptool.py" doesn't work, and I just don't know enough python to figure out why. For some reason, it installs but isn't in the \$PATH anywhere, and I don't know where it went. Python 2.7 kept giving me warning messages about being old and unsupported, so I figured that might be a hint that I should upgrade.

I ended up doing this:

- brew install pyenv (to install pyenv)
- pyenv install 3.7.7 (to install and select python 3.7.7)
- pyenv global 3.7.7 (to select the new version of python)
- brew install pip (to install pip3)
- pip3 install --upgrade esptool (note I specifically had to use "pip3", not "pip")

...then I was able to execute esptool.py

2. esptool.py didn't work though, because the virtual com port wasn't showing up as a device. I had to install a driver from Silicon Labs, which I got here:
   [driver for the CP210X USB to UART bridge from Silicon Labs](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)

After I installed that, esptool.py was completely happy and the firmware loaded right up.
