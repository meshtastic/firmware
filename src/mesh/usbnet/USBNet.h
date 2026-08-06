#pragma once

#include "configuration.h"

#if HAS_USB_NET

#ifdef ARCH_ESP32
#include "sdkconfig.h"

// Build-truth assertion. The TinyUSB device stack is NOT part of a stock
// Meshtastic ESP32 build: every env sets custom_sdkconfig, which makes
// pioarduino rebuild ESP-IDF from source in a generated project that omits the
// arduino_tinyusb component. We pull TinyUSB back in via
// `custom_component_add = espressif/esp_tinyusb` in the variant ini.
//
// If that plumbing silently no-ops - a stale sdkconfig.<env> in the project root
// takes precedence over custom_sdkconfig, which is easy to hit - the build would
// otherwise succeed and produce an image with no USB networking in it at all.
// Fail loudly instead.
#if !defined(CONFIG_TINYUSB_NET_MODE_NCM)
#error                                                                                                                           \
    "HAS_USB_NET=1 but CONFIG_TINYUSB_NET_MODE_NCM is not set: the esp_tinyusb component did not make it into this build. Delete sdkconfig.<env> in the project root and rebuild."
#endif

// The Arduino core's USBCDC.cpp / USBMSC.cpp gate on these same two symbols,
// but expect the *arduino_tinyusb* component's esp32-hal-tinyusb.h helpers,
// which this build does not have. Turning either on un-gates a core file that
// then cannot compile. They are forced off in the variant ini; catch a
// regression here rather than in a wall of "not declared in this scope".
#if defined(CONFIG_TINYUSB_CDC_ENABLED) || defined(CONFIG_TINYUSB_MSC_ENABLED)
#error                                                                                                                           \
    "CONFIG_TINYUSB_CDC_ENABLED/MSC_ENABLED must stay off: they un-gate the Arduino core's USBCDC.cpp/USBMSC.cpp, which need arduino_tinyusb headers absent from this build."
#endif
#endif // ARCH_ESP32

/**
 * Bring up the USB-Ethernet (CDC-NCM) gadget.
 *
 * Presents the node to an attached USB host as a standard USB Ethernet adaptor
 * and serves the existing TCP phone API (port 4403) over it, so a USB-C
 * iPad/iPhone can reach the node over a wire - including in airplane mode,
 * since the link is not a radio.
 *
 * Safe to call when no host is attached; the gadget simply never enumerates.
 */
void initUsbNet();

/// Tear the gadget down (sleep / shutdown / reboot paths).
void deInitUsbNet();

/**
 * Platform back end, implemented per architecture (today only ESP32, in
 * USBNetEsp32.cpp). USBNetThread drives these in a fixed order and nothing else
 * should call them.
 */

/// Create the netif and start the DHCP server. Does not touch USB.
bool usbNetBringUpNetwork();

/// Install the TinyUSB driver and the NCM class. The link starts DOWN.
bool usbNetStartDevice();

/// Announce the link as up. Only call once the phone API is accepting: iOS runs
/// DHCP exactly once on link-up and never retries.
void usbNetRaiseLink();

/// Announce the link as down, re-arming the host's DHCP for the next raise.
void usbNetDropLink();

/// True while a host has the device enumerated and configured.
bool usbNetIsMounted();

#endif // HAS_USB_NET
