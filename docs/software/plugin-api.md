# Plugin-API

This is a tutorial on how to write small plugins which run on the device.  Plugins are bits regular 'arduino' code that can send and receive packets to other nodes/apps/PCs using our mesh.

## Key concepts

All plugins should be subclasses of MeshPlugin.  By inheriting from this class and creating an instance of your new plugin your plugin will be automatically registered to receive packets.

Messages are sent to particular port numbers (similar to UDP networking).  Your new plugin should eventually pick its own port number (see below), but at first you can simply use PRIVATE_APP (which is the default).

Packets can be sent/received either as raw binary structures or as [Protobufs](https://developers.google.com/protocol-buffers).

### Class heirarchy

The relevant bits of the class heirarchy are as follows

* [MeshPlugin](/src/mesh/MeshPlugin.h) (in src/mesh/MeshPlugin.h) - you probably don't want to use this baseclass directly
  * [SinglePortPlugin](/src/mesh/SinglePortPlugin.h) (in src/mesh/SinglePortPlugin.h) - for plugins that receive from a single port number (the normal case)
    * [ProtobufPlugin](/src/mesh/ProtobufPlugin.h) (in src/mesh/ProtobufPlugin.h) - for plugins that are sending/receiving a single particular Protobuf type.  Inherit from this if you are using protocol buffers in your plugin.

You will typically want to inherit from either SinglePortPlugin (if you are just sending raw bytes) or ProtobufPlugin (if you are sending protobufs).  You'll implement your own handleReceived/handleReceivedProtobuf - probably based on the example code.

If your plugin needs to perform any operations at startup you can override and implement the setup() method to run your code.

If you need to send a packet you can call service.sendToMesh with code like this (from the examples):

```
    MeshPacket *p = allocReply();
    p->to = dest;

    service.sendToMesh(p);
```

## Example plugins

A number of [key services](/src/plugins) are implemented using the plugin API, these plugins are as follows:

* [TextMessagePlugin](/src/plugins/TextMessagePlugin.h) - receives text messages and displays them on the LCD screen/stores them in the local DB
* [NodeInfoPlugin](/src/plugins/NodeInfoPlugin.h) - receives/sends User information to other nodes so that usernames are available in the databases
* [RemoteHardwarePlugin](/src/plugins/RemoteHardwarePlugin.h) - a plugin that provides easy remote access to device hardware (for things like turning GPIOs on or off).  Intended to be a more extensive example and provide a useful feature of its own.  See [remote-hardware](remote-hardware.md) for details.
* [ReplyPlugin](/src/plugins/ReplyPlugin.h) - a simple plugin that just replies to any packet it receives (provides a 'ping' service).

## Getting started

The easiest way to get started is:

* [Build and install](build-instructions.md) the standard codebase from github.
* Copy [src/plugins/ReplyPlugin.*](/src/plugins/ReplyPlugin.cpp) into src/plugins/YourPlugin.*.  Then change the port number from REPLY_APP to PRIVATE_APP.
* Rebuild with your new messaging goodness and install on the device
* Use the [meshtastic commandline tool](https://github.com/meshtastic/Meshtastic-python) to send a packet to your board "meshtastic --dest 1234 --ping"

## Threading

It is very common that you would like your plugin to be invoked periodically.
We use a crude/basic cooperative threading system to allow this on any of our supported platforms.  Simply inherit from OSThread and implement runOnce().  See the OSThread [documentation](/src/concurrency/OSThread.h) for more details.  For an example consumer of this API see RemoteHardwarePlugin::runOnce.

## Picking a port number

For any new 'apps' that run on the device or via sister apps on phones/PCs they should pick and use a unique 'portnum' for their application.

If you are making a new app using meshtastic, please send in a pull request to add your 'portnum' to [the master list](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/portnums.proto).  PortNums should be assigned in the following range:

* 0-63 Core Meshtastic use, do not use for third party apps
* 64-127 Registered 3rd party apps, send in a pull request that adds a new entry to portnums.proto to register your application
* 256-511 Use one of these portnums for your private applications that you don't want to register publically
* 1024-66559 Are reserved for use by IP tunneling (see FIXME for more information)

All other values are reserved.

## How to add custom protocol buffers

If you would like to use protocol buffers to define the structures you send over the mesh (recommended), here's how to do that.

* Create a new .proto file in the protos directory.  You can use the existing [remote_hardware.proto](https://github.com/meshtastic/Meshtastic-protobufs/blob/master/remote_hardware.proto) file as an example.
* Run "bin/regen-protos.sh" to regenerate the C code for accessing the protocol buffers.  If you don't have the required nanopb tool, follow the instructions printed by the script to get it.
* Done!  You can now use your new protobuf just like any of the existing protobufs in meshtastic.