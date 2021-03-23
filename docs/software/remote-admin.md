# Remote node administration

This is the first documentation for how to use the [multiple channels](channels.md) feature to enable remote adminstration of meshtastic nodes.  i.e. let you talk through the mesh to some far away node and change that nodes settings.  This is an advanced feature that (currently) few users would need.  Also, keep in mind it is possible (if you are not careful) to assign settings to that remote node that cause it to completely drop off of your mesh.

Btw: I promised to document how multi-channel is now used to secure remote GPIO/serial access.  But probably best to debug these instructions first, so I'll wait on that.  If you **do** need to use remote GPIO/serial now, just follow these instructions but name your new channel "gpio" or "serial".

## Creating the "admin" channel

Okay - now that we've summarized what multiple-channel support is, we can move on to using it to provide remote administrative access to a node.

By default, nodes will **only** respond to adminstrative commands via the local USB/bluetooth/TCP interface.  This provides basic security to prevent unauthorized access.  This is actually how 'normal' administration and settings changes work.  The only difference for the remote case is that we are sending those commands over the mesh.

Before a node will allow remote admin access, it must find a channel 
```
meshtastic --info
Connected to radio
...
Channels:
  PRIMARY psk=default { "modemConfig": "Bw125Cr48Sf4096", "psk": "AQ==" }

Primary channel URL: https://www.meshtastic.org/d/#CgUYAyIBAQ
```

So from this output you see that this node knows about only one channel and that its PSK is set to the default value.

But if you then add an admin channel (with "meshtastic --ch-add admin").  Note: the name is important it must be "admin" (sorry):

Your channels will now look like this:
```
meshtastic --ch-add admin
Connected to radio
Writing modified channels to device

meshtastic --info
Connected to radio
...
Channels:
  PRIMARY psk=default { "modemConfig": "Bw125Cr48Sf4096", "psk": "AQ==" }
  SECONDARY psk=secret { "psk": "HW7E3nMbiNbvr6MhsDonLCmj7eSAhttzjbIx/r5OQmg=", "name": "admin" }

Primary channel URL: https://www.meshtastic.org/d/#CgUYAyIBAQ
Complete URL (includes all channels): https://www.meshtastic.org/d/#CgUYAyIBAQopIiAdbsTecxuI1u-voyGwOicsKaPt5ICG23ONsjH-vk5CaCoFYWRtaW4
```

Notice that now we have a new secondary channel.  Also, the "--info" option prints out TWO URLs.  The "complete URL" includes all of the channels this node understands.  You should consider this URL something you should be very cautious about sharing.  In the case of remote adminstration, you only need the node you want to adminster and the node you are locally connected to know this new "admin" channel.

## Sharing the admin channel with other nodes

I'm going to assume you've already created the admin channel on your "local node" i.e. the meshtastic node sitting on your desk at your home.  But now you want to enable access on the "remote node" you want to eventually have far away from you.

For this step you need physical access to both the nodes.

1. Create the "admin" channel on the "local node" using the instructions above.
2. Copy the "Complete URL" someplace for permanent reference/access.
3. Connect meshtastic-python to the "remote node" over the USB port.
4. For the "remote node" type "meshtastic --seturl the-url-from-step-2".
5. Run "meshtastic --info" and confirm that the "Complete URL" is the same for both of the nodes.
6. Done!

At this point you can take your remote node and install it far away and still be able to change any of its settings.

## Remotely administering your node

Now that both your local node and the remote node contain your secret admin channel key, you can do things like this:

Get the node list from the local node.

```
meshtastic --nodes
Connected to radio
/----------------------------------------------------------------------------------------------------------\
|N|    User    |AKA|   ID    |        Position        |Battery|   SNR   |     LastHeard     |    Since     |
|-+------------+---+---------+------------------------+-------+---------+-------------------+--------------|
|1|Unknown 9058|?58|!28979058|25.0382°, 121.5731°, N/A|  N/A  |-13.50 dB|2021-03-22 09:25:42|19 seconds ago|
\----------------------------------------------------------------------------------------------------------/
```

Using the node ID from that list, send a message through the mesh telling that node to change its owner name.

```
meshtastic --dest \!28979058 --set-owner "Im Remote"
Connected to radio
Setting device owner to Im Remote
INFO:root:Requesting configuration from remote node (this could take a while)
```

And you can now confirm via the local node that the remote node has changed:

```
meshtastic --nodes 
Connected to radio
/----------------------------------------------------------------------------------------------------\
|N|  User   |AKA|   ID    |        Position        |Battery|  SNR  |     LastHeard     |    Since    |
|-+---------+---+---------+------------------------+-------+-------+-------------------+-------------|
|1|Im Remote|IR |!28979058|25.0382°, 121.5731°, N/A|  N/A  |8.75 dB|2021-03-22 09:35:42|3 minutes ago|
\----------------------------------------------------------------------------------------------------/
```

Note: you can change **any** parameter, add channels or get info from the remote node.  Here's an example of setting ls_secs and printing the complete device info from the remote node.

```
meshtastic --dest \!28979058 --set ls_secs 301 --info
Connected to radio
INFO:root:Requesting configuration from remote node (this could take a while)
Set ls_secs to 301
Writing modified preferences to device


Preferences: { "lsSecs": 301, "region": "TW" }

Channels:
  PRIMARY psk=default { "modemConfig": "Bw125Cr48Sf4096", "psk": "AQ==" }
  SECONDARY psk=secret { "psk": "HW7E3nMbiNbvr6MhsDonLCmj7eSAhttzjbIx/r5OQmg=", "name": "admin" }

Primary channel URL: https://www.meshtastic.org/d/#CgUYAyIBAQ
Complete URL (includes all channels): https://www.meshtastic.org/d/#CgUYAyIBAQopIiAdbsTecxuI1u-voyGwOicsKaPt5ICG23ONsjH-vk5CaCoFYWRtaW4
```

## Areas for future development

In the future we will add a "deadman timer" to this feature so that the remote node will revert any changes if you fail to send a special "commit changes" command.  This will protect against sending bad settings to nodes that you can't physically access.  Instead if the node does not receive a commit message within 10 minutes it will revert all changes and (hopefully) rejoin the mesh.