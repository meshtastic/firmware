# Multiple channel support

Version 1.2 of the software adds support for "multiple (simultaneous) channels".  The idea behind this feature is that a mesh can allow multiple users/groups to be share common mesh infrastructure.  Even including routing messages for others when no one except that subgroup of users has the encryption keys for their private channel.

### What is the PRIMARY channel

The way this works is that each node keeps a list of channels it knows about.  One of those channels (normally the first 1) is labelled as the "PRIMARY" channel.  The primary channel is the **only** channel that is used to set radio parameters.  i.e. this channel controls things like spread factor, coding rate, bandwidth etc... Indirectly this channel also is used to select the specific frequency that all members of this mesh are talking over.

This channel may or may not have a PSK (encryption).  If you are providing mesh to 'the public' we recommend that you always leave this channel with its default psk.  The default PSK is technically encrypted (and random users sniffing the ether would have to use meshtastic to decode it), but the key is included in the github source code and you should assume any 'attacker' would have it.  But for a 'public' mesh you want this, because it allows anyone using meshtastic in your area to send packets through 'your' mesh.

Note: Older meshtastic applications that don't yet understand multi-channel support will only show the user this channel.  

### How to use SECONDARY channels

Any channel you add after that PRIMARY channel is SECONDARY.  Secondary channels are used only for encyryption and (in the case of some special applications) security.  If you would like to have a private channel over a more public mesh, you probably want to create a SECONDARY channel.  When sharing that URL with your private group you will share the "Complete URL".  The complete URL includes your secondary channel (for encryption) and the primary channel (to provide radio/mesh access).

Secondary channels **must** have a PSK (encryption).