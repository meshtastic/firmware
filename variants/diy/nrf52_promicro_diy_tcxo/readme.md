## Notes

### General
The pinout is contained in the variant.h file, and a [generic schematic](./Schematic_Pro-Micro_Pinouts%202024-12-14.pdf) is located in this directory. 

#### Note on DIO2, RXEN, TXEN, and RF switching
Several modules require external switching between transmit (Tx) and receive (Rx). This can be achieved using several methods:
1) Link the TXEN pin on the radio module to DIO2 on the same module, and then connect RXEN on the radio module to pin 0.17 on the Pro-Micro.
2) Use DIO2 to drive a logic inverter, so that when DIO2 is `high`, RXEN is `low`, and vice versa.
3) Use DIO2 to drive a pair of MOSFETs or transistors to supply the same function.

RXEN is not required to be connected if the selected module already has internal RF switching, or if external RF switching logic is already applied.
Also worth noting that the Seeed WIO SX1262 in particular only has RXEN exposed (marked RF_SW) and has the DIO2-TXEN link internally.

### LR1121 modules
The CDEbyte implementation of the LR1121 is contained in their E80 module. Naturally, CDEbyte have chosen to ignore the generic Semtech impelementation of the RF switching logic and have their own table, which is located at the bottom of the page [here](https://www.cdebyte.com/products/E80-900M2213S/2#Pin), and reflected on page 6 of their user manual, and reproduced below:

| DIO5/RFSW0 | DIO6/RFSW1 | RF status                     |
| ---------- | ---------- | ----------------------------- |
| 0          | 0          | RX                            |
| 0          | 1          | TX (Sub-1GHz low power mode)  |
| 1          | 0          | TX (Sub-1GHz high power mode) |
| 1          | 1          | TX（2.4GHz）                    |

However, looking at the sample code they provide on page 9, the values would be:

| DIO5/RFSW0 | DIO6/RFSW1 | RF status                     |
| ---------- | ---------- | ----------------------------- |
| 0          | 1          | RX                            |
| 1          | 1          | TX (Sub-1GHz low power mode)  |
| 1          | 0          | TX (Sub-1GHz high power mode) |
| 0          | 0          | TX（2.4GHz）                    |

If you want to use the Semtech default, the values are (taken from [here](https://github.com/Lora-net/SWSD006/blob/v2.6.1/lib/app_subGHz_config_lr11xx.c#L145-L154)):

<details>

```
	.rfswitch = {
		.enable = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH | LR11XX_SYSTEM_RFSW2_HIGH,
		.standby = 0,
		.rx = LR11XX_SYSTEM_RFSW0_HIGH,
		.tx = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH,
		.tx_hp = LR11XX_SYSTEM_RFSW1_HIGH,
		.tx_hf = 0,
		.gnss = LR11XX_SYSTEM_RFSW2_HIGH,
		.wifi = 0,
	},
```
</details>


| DIO5/RFSW0 | DIO6/RFSW1 | RF status                     |
| ---------- | ---------- | ----------------------------- |
| 1          | 0          | RX                            |
| 1          | 1          | TX (Sub-1GHz low power mode)  |
| 0          | 1          | TX (Sub-1GHz high power mode) |
| 0          | 0          | TX（2.4GHz）                    |
