## Notes

### General
The pinout is contained in the variant.h file, and a [generic schematic](./Schematic_Pro-Micro_Pinouts%202024-12-14.pdf) is located in this directory. Note that RXEN is not required if the selected module already has internal RF switching, or if external RF switching logic is already applied.

### LR1121 modules
The CDEbyte implementation of the LR1121 is contained in their E80 module. Naturally, CDEbyte have chosen to ignore the generic Semtech impelementation of the RF switching logic and have their own table, which is located at the bottom of the page [here](https://www.cdebyte.com/products/E80-900M2213S/2#Pin), and reproduced below for posterity:

| DIO5/RFSW0 | DIO6/RFSW1 | RF status                     |
| ---------- | ---------- | ----------------------------- |
| 0          | 0          | RX                            |
| 0          | 1          | TX (Sub-1GHz low power mode)  |
| 1          | 0          | TX (Sub-1GHz high power mode) |
| 1          | 1          | TX（2.4GHz）                    |

If you want to use the Semtech default, the values are (taken from [here](https://github.com/Lora-net/SWSD006/blob/73f59215b0103de7011056105011c5f5cc47f1e8/lib/app_subGHz_config_lr11xx.c#L145C1-L154C4)):

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
