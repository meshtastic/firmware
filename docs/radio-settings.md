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

### About

Various data-rates are selectable when configuring a channel and are inversely proportional to the theoretical range of the devices.

Considerations:

* Spreading Factor - How much we "spread" our data over time.
* * Each step up in Spreading Factor dobules the airtime to transmit.
* * Each step up in Spreading Factor adds about 2.5db extra link budget.
* Bandwidth - How big of a slice of the spectrum we use.
* * Each doubling of the bandwidth is almost 3db less link budget.
* * Bandwidths less than 31 may be unstable unless you have a high quality Crystal Ossilator.
* Coding Rate - How much redundency we encode to resist noise.
* * Increasing coding rate increases reliability while decrasing data-rate.
* * 4/5 - 1.25x overhead
* * 4/6 - 1.5x overhead
* * 4/7 - 1.75x overhead
* * 4/8 - 2x overhead


### Pre-Defined

We have four predefined channels. These are the most common settings and have been proven to work well:

| Channel setting            | Alt Channel Name | Data-rate            | SF / Symbols | Coding Rate | Bandwidth |
|:---------------------------|:-----------------|:---------------------|:-------------|:------------|:----------|
| Short range (but fast)     | Short Fast       | 21.875 kbps          | 7 / 128      | 4/5         | 125       |
| Medium range (but fast)    | Medium           | 5.469 kbps           | 7 / 512      | 4/5         | 500       |
| Long range (but slower)    | Long Alt         | 0.275 kbps           | 9 / 512      | 4/8         | 31        |
| Very long range (but slow) | Long Slow        | 0.183 kbps (default) | 12 / 4096    | 4/8         | 125       |

### Custom Settings

You may want to select other channels for your usage. The other settings can be set by using the Python API.

> meshtastic --setchan spread_factor 10 --setchan coding_rate 8 --setchan bandwidth 125

Some example settings:

| Data-rate            | SF / Symbols | Coding Rate | Bandwidth | Note |
|:---------------------|:-------------|:------------|:----------|:-----|
| 3.125 kbps           | 8 / 256      | 4/5         | 125       | |
| 1.953 kbps           | 8 / 256      | 4/8         | 125       | |
| 1.343 kbps           | 11 / 2048    | 4/8         | 512       | | 
| 1.099 kbps           | 9 / 512      | 4/8         | 125       | |
| 0.814 kbps           | 10 / 1024    | 4/6         | 125       | |
| 0.610 kbps           | 10 / 1024    | 4/8         | 125       | |
| 0.488 kbps           | 11 / 2048    | 4/6         | 125       | |
| 0.336 kbps           | 11 / 2048    | 4/8         | 125       | |
| 0.092 kbps           | 12 / 4096    | 4/8         | 62        | |
| 0.046 kbps           | 12 / 4096    | 4/8         | 31        | Twice the range of "Long Slow" |


## Cryptography

The presahred key used by the devices can be updated.

* 0 = No crypto
* 1 = Default channel key
* 2 - 10 = The default channel key, except with 1 through 9 added to the last byte

To disable cryptography:

> meshtastic --setchan psk 0