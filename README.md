# Phantom + Meshtastic = Phantastic


## Overview

Meshtastic's been catching fire lately, especially with the privacy-conscious crowd looking for secure off-grid comms. It’s got encryption built in, keeping your convos locked down tight. But here's the thing—anonymity wasn’t really baked into the original design. And that opens the door to track people through their nodes, no consent needed.

Now, let’s say you're out at a protest, or maybe a hacker con, and you need to keep your identity as fluid as your connections. You'd want a system that can shed your traces. That’s where Phantastic comes in.

Phantastic’s a fork of Meshtastic, designed with a single purpose in mind: making profiling and tracking a hell of a lot harder. Every time you reset the firmware, it wipes out any identifying info, leaving no breadcrumb trail. Users can swap out their ID on the fly, whenever it’s time to disappear into the crowd.


## What are the changes
- [x] Generate a random NodeID on firmware reset 
- [ ] Add Mac address randomization to ble 
- [ ] Add Mac address randomization to wifi
- [ ] Add random delay before sending message to avoid visual profiling of a user
- [ ] Add build job to release .bin files so users can flash with web flasher
- [ ] Other ideas? Submit them as an issue or PR!

## How can we trust you?
Trust me? You shouldn't. Look at the code changes. It's pretty straightforward stuff. If you have the skill, compile and flash from source. Don't trust some random .bin uploaded by some random cyberpunk.


### Get Started

Follow the normal Meshtastic building and flashing instructions, just with this repo instead of the official one

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.


