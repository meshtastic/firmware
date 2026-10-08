# Heltec Vision Master E290 - InkHUD panel split

Same V0.3.2 PCB ships two 2.9" panels. The flex print is the only reliable ID; firmware cannot read it (e-ink SPI is MOSI-only).

| Flex print                     | InkHUD env                                                           | FAST / partial                       |
| ------------------------------ | -------------------------------------------------------------------- | ------------------------------------ |
| `15-22243-M0282-42_AD-4225076` | `heltec-vision-master-e290-inkhud` (stock `DEPG0290BNS800`)          | Custom LUT. OTP FAST does not paint. |
| `FPC-7519revb`                 | `heltec-vision-master-e290-inkhud-otp` (`-D EINK_HELTEC_VME290_OTP`) | OTP LUT. Stock custom LUT ghosts.    |

FULL refresh works on both. If stock firmware looks like the user button died on an FPC-7519 unit, check whether only FAST updates are failing.

Classic UI (`heltec-vision-master-e290`, GxEPD2) is unchanged.
