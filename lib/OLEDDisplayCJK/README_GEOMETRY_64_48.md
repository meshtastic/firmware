# GEOMETRY_64_48

The 64x48 geometry setting are working with the `Wire.h` and `brzo_i2c` libraries.

I've tested it successfully with a WEMOS D1 mini Lite and a WEMOS OLED shield

Initialization code:

- Wire
```
#include <Wire.h>
#include <SSD1306Wire.h>
SSD1306Wire display(0x3c, D2, D1, GEOMETRY_64_48 ); // WEMOS OLED shield
```

- BRZO i2c
```
#include <SSD1306Brzo.h>
SSD1306Brzo display(0x3c, D2, D1, GEOMETRY_64_48 ); // WEMOS OLED Shield
```
