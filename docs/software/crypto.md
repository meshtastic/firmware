# Encryption in Meshtastic

Cryptography is tricky, so we've tried to 'simply' apply standard crypto solutions to our implementation. However,
the project developers are not cryptography experts. Therefore we ask two things:

- If you are a cryptography expert, please review these notes and our questions below. Can you help us by reviewing our
  notes below and offering advice? We will happily give as much or as little credit as you wish ;-).
- Consider our existing solution 'alpha' and probably fairly secure against a not particularly aggressive adversary
(but we can't yet make a more confident statement).

## Summary of strengths/weaknesses of our current implementation

Based on comments from reviewers (see below), here's some tips for usage of these radios.  So you can know the level of protection offered:

* It is pretty likely that the AES256 security is implemented 'correctly' and an observer will not be able to decode your messages.
* Warning: If an attacker is able to get one of the radios in their position, they could either a) extract the channel key from that device or b) use that radio to listen to new communications.
* Warning: If an attacker is able to get the "Channel QR code/URL" that you share with others - that attacker could then be able to read any messages sent on the channel (either tomorrow or in the past - if they kept a raw copy of those broadcast packets)

Possible future areas of work (if there is enough interest - post in our [forum](https://meshtastic.discourse.group) if you want this):

1. Optionally requiring users to provide a PIN to regain access to the mesh.  This could be based on: intentionally locking the device, time since last use, or any member could force all members to reauthenticate,
2. Until a device reauthenticates, any other access via BLE or USB would be blocked (this would protect against attackers who are not prepared to write custom software to extract and reverse engineer meshtastic flash memory)
3. Turning on read-back protection in the device fuse-bits (this would extend protection in #2 to block all but **extremely** advanced attacks involving chip disassembly)
4. Time limiting keys used for message transmission and automatically cycling them on a schedule.  This would protect past messages from being decoded even if an attacker learns the current key.

### Notes for reviewers

If you are reviewing our implementation, this is a brief statement of our method.

- We do all crypto at the SubPacket (payload) level only, so that all meshtastic nodes will route for others - even those channels which are encrypted with a different key.
- Mostly based on reading [Wikipedia](<https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation#Counter_(CTR)>) and using the modes the ESP32 provides support for in hardware.
- We use AES256-CTR as a stream cypher (with zero padding on the last BLOCK) because it is well supported with hardware acceleration.
- Our AES key is 128 or 256 bits, shared as part of the 'Channel' specification.
- The node number concatenated with the packet number is used as the NONCE. This nonce will be stored in flash in the device and should essentially never repeat. If the user makes a new 'Channel' (i.e. picking a new random 256 bit key), the packet number will start at zero. 
- The packet number is sent in cleartext with each packet. The node number can be derived from the "from" field of each packet. (Cleartext is acceptable because it merely provides IV for each encryption run)
- Each 16 byte BLOCK for a packet has an incrementing COUNTER. COUNTER starts at zero for the first block of each packet.
- The IV for each block is constructed by concatenating the NONCE as the upper 96 bits of the IV and the COUNTER as the bottom 32 bits. Since our packets are small counter portion will really never be higher than 32 (five bits).

## Comments from reviewer #1

This reviewer is a cryptography professional, but would like to remain anonymous. We thank them for their comments ;-):

I'm assuming that meshtastic is being used to hike in places where someone capable is trying to break it - like you were going to walk around DefCon using these. I spent about an hour reviewing the encryption, and have the following notes:

* The write-up isn't quite as clear as the code.
* The code is using AES-CTR mode correctly to ensure confidentiality.
* The comment for initNonce really covers the necessary information.
* I think the bigger encryption question is "what does the encryption need to do"? As it stands, an attacker who has yet to capture any of the devices cannot reasonably capture text or location data. An attacker who captures any device in the channel/mesh can read everything going to that device, everything stored on that device, and any other communication within the channel that they captured in encrypted form. If that capability basically matches your expectations, it is suitable for whatever adventures this was intended for, then, based on information publicly available or widely disclosed, the encryption is good. If those properties are distressing (like, device history is deliberately limited and you don't want a device captured today to endanger the information sent over the channel yesterday) we could talk about ways to achieve that (most likely synchronizing time and replacing the key with its own SHA256 every X hours, and ensuring the old key is not retained unnecessarily).
* Two other things to keep in mind are that AES-CTR does not itself provide authenticity (e.g. an attacker can flip bits in replaying data and scramble the resulting plaintext), and that the current scheme gives some hints about transmission in the size. So, if you worry about an adversary deliberately messing-up messages or knowing the length of a text message, it looks like those might be possible.

I'm guessing that the network behaves somewhat like a store-and-forward network - or, at least, that the goal is to avoid establishing a two-way connection to transmit data. I'm afraid I haven't worked with mesh networks much, but remember studying them briefly in school about ten years ago.