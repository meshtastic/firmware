# Disclaimers

This project is still pretty young but moving at a pretty good pace. Not all features are fully implemented in the current alpha builds.
Most of these problems should be solved by the beta release (within three months):

- We don't make these devices and they haven't been tested by UL or the FCC. If you use them you are experimenting and we can't promise they won't burn your house down ;-)
- The encryption implementation is good but see this list of [caveats](software/crypto.md#summary-of-strengthsweaknesses-of-our-current-implementation) to determine risks you might face.
- A number of (straightforward) software work items have to be completed before battery life matches our measurements, currently battery life is about three days. Join us on chat if you want the spreadsheet of power measurements/calculations.
- The Android API needs to be documented better
- No one has written an iOS app yet. But some good souls [are talking about it](https://github.com/meshtastic/Meshtastic-esp32/issues/14) ;-)

For more details see the [device software TODO](https://github.com/meshtastic/Meshtastic-esp32/blob/master/docs/software/TODO.md) or the [Android app TODO](https://github.com/meshtastic/Meshtastic-Android/blob/master/TODO.md).

# FAQ

If you have a question missing from this faq, please [ask in our discussion forum](https://meshtastic.discourse.group/). And if you are feeling extra generous send in a pull-request for this faq.md with whatever we answered ;-).

## Q: Which of the various supported radios should I buy?

Basically you just need the radio + (optional but recommended) battery. The TBEAM is usually better because it has gps and huge battery socket. The Heltec is basically the same hardware but without the GPS (the phone provides position data to the radio in that case, so the behavior is similar - but it does burn some battery in the phone). Also the battery for the Heltec can be smaller.

In addition to Aliexpress, (banggood.com) usually has stock and faster shipping, or Amazon. If buying a TBEAM, make sure to buy a version that includes the OLED screen - this project doesn't absolutely require the screen, but we use it if is installed.

@claesg has added links to various 3D printable cases, you can see them at (www.meshtastic.org).

## Q: Do you have plans to commercialize this project

Nope. though if some other person/group wanted to use this software and a more customized device we think that would be awesome (as long as they obey the GPL license).

## Q: Does this project use patented algorithms?

(Kindly borrowed from the geeks at [ffmpeg](http://ffmpeg.org/legal.html))

We do not know, we are not lawyers so we are not qualified to answer this. Also we have never read patents to implement any part of this, so even if we were qualified we could not answer it as we do not know what is patented. Furthermore the sheer number of software patents makes it impossible to read them all so no one (lawyer or not) could answer such a question with a definite no. We are merely geeks experimenting on a fun and free project.
