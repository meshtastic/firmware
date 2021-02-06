# About

  This is a work in progress and is not yet available.

The Store Request Plugin is an implementation of a Store and Forward system to enable resilient messaging in the event that a client device is disconnected from the main network.

Because of the increased network traffic for this overhead, it's not adviced to use this if you are duty cycle limited for your airtime usage nor is it adviced to use this for SF12.

# Running notes

This will only work on nodes that are designated as a Router.

Initial Requirements:

* Must be installed on a router node.
* * This is an artificial limitation, but is in place to enforce best practices.
* * Router nodes are intended to be always online. If this plugin misses any messages, the reliability of the stored messages will be reduced
* Esp32 Processor based device with external PSRAM. (tbeam v1.0 and tbeamv1.1, maybe others)

Initial Features
* 