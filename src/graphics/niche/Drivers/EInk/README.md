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

- [Methods](#methods)
  - [`update(uint8_t *imageData, UpdateTypes type)`](#updateuint8_t-imagedata-updatetypes-type)
  - [`await()`](#await)
  - [`supports(UpdateTypes type)`](#supportsupdatetypes-type)
  - [`busy()`](#busy)
  - [`width()`](#width)
  - [`height()`](#height)
- [Supporting New Displays](#supporting-new-displays)
  - [Controller IC](#controller-ic)
  - [Finding Information](#finding-information)

## Methods

### `update(uint8_t *imageData, UpdateTypes type)`

Update the image on the display

- _`imageData`_ to draw to the display.
- _`type`_ which type of update to perform.
  - `FULL`
  - `FAST` (partial refresh)
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

## Supporting New Displays

_This topic is not covered in depth, but these notes may be helpful._

The `InkHUD::Drivers::EInk` class contains only the mechanism for implementing an E-Ink driver on-top of Meshtastic's `OSThread`. A driver for a specific display needs to extend this class.

### Controller IC

If your display uses a controller IC from Solomon Systech, you can probably extend the existing `Drivers::SSD16XX` class, making only minor modifications.

At this stage, displays using controller ICS from other manufacturers (UltraChip, Fitipower, etc) need to manually implemented. See `Drivers::LCMEN2R13EFC1` for an example.

Generic base classes for manufacturers other than Solomon Systech might be added here in future.

### Finding Information

#### Flex-Connector Labels

The orange flex-connector attached to E-Ink displays is often printed with an identifying label. This is not a _totally_ unique identifier, but does give a very strong clue as to the true model of the display, which can be used to search out further information.

#### Datasheets

The manufacturer of a DIY display module may publish a datasheet. These are often incomplete, but might reveal the true model of the display, or the controller IC.

If you can determine the true model name of the display, you can likely find a more complete datasheet on the display manufacturer's website. This will often provide a "typical operating sequence"; a general overview of the code used to drive the display

#### Example Code

The manufacturer of a DIY module may publish example code. You may have more luck finding example code published by the display manufacturer themselves, if you can determine the true model of the panel. These examples are a very valuable reference.

#### Other E-Ink drivers

Libraries like ZinggJM's GxEPD2 can be valuable sources of information, although your panel may not be _specifically_ supported, and only _compatible_ with a driver there, so some caution is advised.

The display selection file in GxEPD2's Hello World example is also a useful resource for matching "flex connector labels" with display models, but the flex connector label is _not_ a unique identifier, so this is only another clue.
