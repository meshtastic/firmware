# NicheGraphics

A pattern / collection of resources for creating custom UIs, to target small groups of devices which have specific design requirements.

For an example, see the `heltec-vision-master-e290-inkhud` platformio env.

- platformio.ini

  - suppress default Meshtastic components (Screen, ButtonThread, etc)
  - define `MESHTASTIC_INCLUDE_NICHE_GRAPHICS`
  - (possibly) Edit `build_src_filter` to include our new nicheGraphics.h file

- nicheGraphics.h
  - `#include` all necessary components
  - perform all setup and config inside a `setupNicheGraphics()` method
