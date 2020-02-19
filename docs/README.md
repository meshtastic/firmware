# What is Meshtastic?

Meshtastic is a project that lets you use
inexpensive GPS mesh radios as an extensible, super long battery life mesh GPS communicator.  These radios are great for hiking, skiing, paragliding - 
essentially any hobby where you don't have reliable internet access.  Each member of your private mesh can always see the location and distance of all other
members and any text messages sent to your group chat.

The radios automatically create a mesh to forward packets as needed, so everyone in the group can receive messages from even the furthest member.  The radios
will optionally work with your phone, but no phone is required.

Typical time between recharging the radios should be about eight days.

This project is currently pre-alpha, but if you have questions please join our chat [![Join the chat at https://gitter.im/Meshtastic/community](https://badges.gitter.im/Meshtastic/community.svg)](https://gitter.im/Meshtastic/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge).

This software is 100% open source and developed by a group of hobbyist experimenters.  No warranty is provided, if you'd like to improve it - we'd love your help.  Please post in the chat.  

[![Youtube video demo](desk-video-screenshot.png)](https://www.youtube.com/watch?v=WlNbMbVZlHI "Meshtastic early demo")

## Meshtastic Android app
The source code for the Meshtastic Android app is [here](https://github.com/geeksville/Meshtastic-Android).
Soon our first alpha release of will be released here:

[![Download at https://play.google.com/store/apps/details?id=com.geeksville.mesh](https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png)](https://play.google.com/store/apps/details?id=com.geeksville.mesh&referrer=utm_source%3Dhomepage%26anid%3Dadmob)

If you would like to join our bleeding-edge alpha test group for this app, we'd love to have you.  Three steps:
1. Join [this Google group](https://groups.google.com/forum/#!forum/meshtastic-alpha-testers) with the account you use in Google Play.
2. Go to this [URL](https://play.google.com/apps/testing/com.geeksville.mesh) to opt-in to the alpha test.
3. If you encounter any problems or have questions, post in our gitter chat and we'll help.

## Supported hardware
We currently support two brands of radios.  The [TTGO T-Beam](https://www.aliexpress.com/item/4000119152086.html) and the [Heltec LoRa 32](https://heltec.org/project/wifi-lora-32/).  Most users should buy the T-Beam and a 18650 battery (total cost less than $35).  Make
sure to buy the frequency range which is legal for your country.  For the USA, you should buy the 915MHz version.  Getting a version that include a screen
is optional, but highly recommended.

We don't yet distribute prebuilt binaries.  But soon (by Feb 22) we will have a file that you can easily install on your radio via USB.  Once our software is installed, all future software updates happen over bluetooth from your phone.

For a nice 3D printable case see this [design](https://www.thingiverse.com/thing:3773717) by [bsiege](https://www.thingiverse.com/bsiege).
