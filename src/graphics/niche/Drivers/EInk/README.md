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

    // Setup EInk driver
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
  - [`width`](#width)
  - [`height`](#height)
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
  - `CLEANUP` (optional non-inverting maintenance refresh)

Not every driver supports every update type. Check `driver->supports(EInk::CLEANUP)` before requesting maintenance; drivers advertise supported types as capability bits. `CLEANUP` does not imply support for grayscale.

The image data is a row-major 1-bit image, with eight horizontal pixels per byte and the MSB representing the leftmost pixel. A set bit is white; a cleared bit is black. Each row occupies `(width + 7) / 8` bytes, including padding when the width is not divisible by eight. Keep the buffer alive and unchanged until the update completes.

```cpp
#include <cstddef>
#include <cstdint>
#include <vector>

const uint16_t w = driver->width;
const uint16_t h = driver->height;
const std::size_t rowBytes = (static_cast<std::size_t>(w) + 7) / 8;
std::vector<uint8_t> image(rowBytes * h, 0xFF); // Start with white pixels.

const uint16_t x = 12;
const uint16_t y = 2;
if (x < w && y < h) {
    const std::size_t yBytes = static_cast<std::size_t>(y) * rowBytes;
    const std::size_t xBytes = x / 8;
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x % 8)));
    image[yBytes + xBytes] &= static_cast<uint8_t>(~mask); // Draw black.
}

driver->update(image.data(), NicheGraphics::Drivers::EInk::FULL);
driver->await(); // Finish before the local vector is destroyed or reused.
```

### `await()`

Wait for an in-progress update to complete before continuing

### `supports(UpdateTypes type)`

Check if display supports a specific update type. `true` if supported.

- _`type`_ type to check

### `busy()`

Check if display is already performing an `update()`. `true` if already updating.

### `width`

Read-only instance field (`driver->width`): width of the display, in pixels. Note: most displays are portrait. Your UI will need to implement rotation in software.

### `height`

Read-only instance field (`driver->height`): height of the display, in pixels. Note: most displays are portrait. Your UI will need to implement rotation in software.

## Supporting New Displays

_This topic is not covered in depth, but these notes may be helpful._

The `NicheGraphics::Drivers::EInk` class contains only the mechanism for implementing an E-Ink driver on-top of Meshtastic's `OSThread`. A driver for a specific display needs to extend this class.

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
