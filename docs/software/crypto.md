# Encryption in Meshtastic

Cryptography is tricky, so we've tried to 'simply' apply standard crypto solutions to our implementation. However,
the project developers are not cryptography experts. Therefore we ask two things:

- If you are a cryptography expert, please review these notes and our questions below. Can you help us by reviewing our
  notes below and offering advice? We will happily give as much or as little credit as you wish ;-).
- Consider our existing solution 'alpha' and probably fairly secure against a not particularly aggressive adversary. But until
  it is reviewed by someone smarter than us, assume it might have flaws.

## Notes on implementation

- We do all crypto at the SubPacket (payload) level only, so that all meshtastic nodes will route for others - even those channels which are encrypted with a different key.
- Mostly based on reading [Wikipedia](<https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation#Counter_(CTR)>) and using the modes the ESP32 provides support for in hardware.
- We use AES256-CTR as a stream cypher (with zero padding on the last BLOCK) because it is well supported with hardware acceleration.

Parameters for our CTR implementation:

- Our AES key is 128 or 256 bits, shared as part of the 'Channel' specification.
- Each SubPacket will be sent as a series of 16 byte BLOCKS.
- The node number concatenated with the packet number is used as the NONCE. This counter will be stored in flash in the device and should essentially never repeat. If the user makes a new 'Channel' (i.e. picking a new random 256 bit key), the packet number will start at zero. The packet number is sent
  in cleartext with each packet. The node number can be derived from the "from" field of each packet.
- Each BLOCK for a packet has an incrementing COUNTER. COUNTER starts at zero for the first block of each packet.
- The IV for each block is constructed by concatenating the NONCE as the upper 96 bits of the IV and the COUNTER as the bottom 32 bits. Note: since our packets are small counter will really never be higher than 32 (five bits).

```
You can encrypt separate messages by dividing the nonce_counter buffer in two areas: the first one used for a per-message nonce, handled by yourself, and the second one updated by this function internally.
For example, you might reserve the first 12 bytes for the per-message nonce, and the last 4 bytes for internal use. In that case, before calling this function on a new message you need to set the first 12 bytes of nonce_counter to your chosen nonce value, the last 4 to 0, and nc_off to 0 (which will cause stream_block to be ignored). That way, you can encrypt at most 2**96 messages of up to 2**32 blocks each with the same key.

The per-message nonce (or information sufficient to reconstruct it) needs to be communicated with the ciphertext and must be unique. The recommended way to ensure uniqueness is to use a message counter. An alternative is to generate random nonces, but this limits the number of messages that can be securely encrypted: for example, with 96-bit random nonces, you should not encrypt more than 2**32 messages with the same key.

Note that for both stategies, sizes are measured in blocks and that an AES block is 16 bytes.
```

## Remaining todo

- Have the app change the crypto key when the user generates a new channel
