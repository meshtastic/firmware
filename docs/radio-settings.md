# Radio settings

We use the same channel maps as LoRaWAN (though this is not LoRaWAN).

![freq table](/images/LoRa-Frequency-Bands.jpg)

See [this site](https://www.rfwireless-world.com/Tutorials/LoRa-channels-list.html) for more information.

## LoRaWAN Europe Frequency Band

The maximum power allowed is +14dBm ERP (Effective Radiated Power, see [this site](https://en.wikipedia.org/wiki/Effective_radiated_power) for more information).

### 433 MHz

There are eight channels defined with a 0.2 MHz gap between them.
Channel zero starts at 433.175 MHz

### 870 MHz

There are eight channels defined with a 0.3 MHz gap between them.
Channel zero starts at 865.20 MHz

## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBm ERP.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are separated by 2.16 MHz with respect to the adjacent channels.  
Channel zero starts at 903.08 MHz center frequency.

## Data-rates

Various data-rates are selectable when configuring a channel and are inversely proportional to the theoretical range of the devices:  

|       Channel setting      |     Data-rate        |
|----------------------------|----------------------|
| Short range (but fast)     | 21.875 kbps          |
| Medium range (but fast)    | 5.469 kbps           |
| Long range (but slower)    | 0.275 kbps           |
| Very long range (but slow) | 0.183 kbps (default) |
