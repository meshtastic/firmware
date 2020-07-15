#pragma once

// See sw-design.md for documentation
enum Event {
    EVENT_PRESS = 1,
    EVENT_WAKE_TIMER = 2,
    EVENT_RECEIVED_PACKET = 3,
    EVENT_PACKET_FOR_PHONE = 4,
    EVENT_RECEIVED_TEXT_MSG = 5,
// #define EVENT_BOOT 6 // now done with a timed transition
    EVENT_BLUETOOTH_PAIR = 7,
    EVENT_NODEDB_UPDATED = 8,     // NodeDB has a big enough change that we think you should turn on the screen
    EVENT_CONTACT_FROM_PHONE = 9, // the phone just talked to us over bluetooth
    EVENT_LOW_BATTERY = 10, // Battery is critically low, go to sleep
    EVENT_SERIAL_CONNECTED = 11,
    EVENT_SERIAL_DISCONNECTED = 12
};
