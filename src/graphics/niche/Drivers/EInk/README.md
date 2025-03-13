# NicheGraphics - E-Ink Driver

A driver for E-Ink SPI displays. Suitable for re-use by various NicheGraphics UIs.

Your UI should use the class `NicheGraphics::Drivers::EInk` .
When you set up a hardware variant, you will use one of the specific display model classes, which extend the EInk class.

An example setup might look like this:

```cpp
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // An imaginary UI
    YourCustomUI *yourUI = new YourCustomUI();

    // Setup SPI
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    // Setup Enk driver
    Drivers::EInk *driver = new Drivers::DEPG0290BNS800;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY);

    // Pass the driver to your UI
    YourUI::driver = driver;
}
```

## Methods

### `update(uint8_t *imageData, UpdateTypes type)`

Update the image on the display

- _`imageData`_ to draw to the display.
- _`type`_ which type of update to perform.
  - `FULL`
  - `FAST`
  - (Other custom types may be possible)

The imageData is a 1-bit image. X-Pixels are 8-per byte, with the MSB being the leftmost pixel. This was not an InkHUD design decision; it is the raw format accepted by the E-Ink display controllers ICs.

_To-do: add a helper method to `InkHUD::Drivers::EInk` to do this arithmetic for you._

```cpp
uint16_t w = driver::width();
uint16_t h = driver::height();

uint8_t image[ (w/8) * h ]; // X pixels are 8-per-byte

image[0] |= (1 << 7); // Set pixel x=0, y=0
image[0] |= (1 << 0); // Set pixel x=7, y=0
image[1] |= (1 << 7); // Set pixel x=8, y=0

uint8_t x = 12;
uint8_t y = 2;
uint8_t yBytes = y * (w/8);
uint8_t xBytes = x / 8;
uint8_t xBits = (7-x) % 8;
image[yByte + xByte] |= (1 << xBits); // Set pixel x=12, y=2
```

### `await()`

Wait for an in-progress update to complete before continuing

### `supports(UpdateTypes type)`

Check if display supports a specific update type. `true` if supported.

- _`type`_ type to check

### `busy()`

Check if display is already performing an `update()`. `true` if already updating.

### `width()`

Width of the display, in pixels. Note: most displays are portrait. Your UI will need to implement rotation in software.

### `height()`

Height of the display, in pixels. Note: most displays are portrait. Your UI will need to implement rotation in software.
