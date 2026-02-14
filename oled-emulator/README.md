# Meshtastic OLED Emulator

A Vue.js component and TypeScript library for emulating SSD1306/SH1106 OLED displays used in Meshtastic devices. This allows firmware UI development and testing directly in the browser.

## Features

- **Same API as firmware**: Uses the same `OLEDDisplay` API as esp8266-oled-ssd1306 library
- **Multiple display sizes**: 128x64, 128x32, 64x48, 128x128
- **Realistic rendering**: Pixel glow, OLED color tints (blue, white, yellow/blue dual)
- **XBM image support**: Render firmware icons and images
- **Font rendering**: Bitmap font support with text alignment
- **Vue 3 component**: Easy integration with Vue applications

## Installation

```bash
npm install @meshtastic/oled-emulator
```

## Quick Start

```vue
<script setup lang="ts">
import { ref, onMounted } from "vue";
import {
  OLEDDisplay,
  OLEDEmulator,
  ArialMT_Plain_10,
} from "@meshtastic/oled-emulator";

// Create display matching firmware
const display = ref(new OLEDDisplay("128x64"));

onMounted(() => {
  display.value.setFont(ArialMT_Plain_10);
  display.value.clear();
  display.value.drawString(10, 10, "Hello Mesh!");
  display.value.drawRect(5, 5, 118, 54);
  display.value.fillCircle(64, 32, 15);
});
</script>

<template>
  <OLEDEmulator
    :display="display"
    :pixel-scale="4"
    preset="ssd1306-blue"
    :enable-glow="true"
  />
</template>
```

## OLEDDisplay API

The `OLEDDisplay` class provides the same methods as the firmware's display library:

```typescript
const display = new OLEDDisplay("128x64");

// Basic drawing
display.clear();
display.setPixel(x, y);
display.drawLine(x0, y0, x1, y1);
display.drawRect(x, y, width, height);
display.fillRect(x, y, width, height);
display.drawCircle(x, y, radius);
display.fillCircle(x, y, radius);

// Text
display.setFont(ArialMT_Plain_10);
display.setTextAlignment("TEXT_ALIGN_CENTER");
display.drawString(x, y, "Hello");
display.drawStringMaxWidth(x, y, maxWidth, "Long text...");
const width = display.getStringWidth("text");

// Images (XBM format)
display.drawXbm(x, y, width, height, imageData);

// Colors
display.setColor("WHITE"); // or 'BLACK', 'INVERSE'

// Buffer access (for custom rendering)
const buffer = display.getBuffer(); // Uint8Array
```

## Display Presets

The emulator includes several realistic display presets:

| Preset                | Description                     |
| --------------------- | ------------------------------- |
| `ssd1306-blue`        | Classic blue OLED (default)     |
| `ssd1306-white`       | White OLED                      |
| `ssd1306-yellow-blue` | Dual-color (top 16 rows yellow) |
| `sh1106`              | SH1106 style                    |
| `sh1107`              | SH1107 128x128                  |
| `ssd1306-64x48`       | Small 64x48 display             |
| `ssd1306-128x32`      | Wide 128x32 display             |

## Screen Renderers

Pre-built screen renderers matching firmware UI:

```typescript
import { ScreenRenderers } from "@meshtastic/oled-emulator";

// Boot screen
ScreenRenderers.drawBootScreen(display, "2.5.0");

// Node info screen
ScreenRenderers.drawNodeInfoScreen(display, {
  shortName: "MESH",
  longName: "My Node",
  nodeId: "!abcd1234",
  lastHeard: "2m ago",
  snr: 12.5,
});

// Message screen
ScreenRenderers.drawMessageScreen(display, {
  from: "Alice",
  text: "Hello from the mesh!",
  time: "14:32",
  channel: "LongFast",
});

// And more: drawGPSScreen, drawNodeListScreen, drawSystemScreen, drawCompassScreen
```

## Development

```bash
# Install dependencies
npm install

# Run development server
npm run dev

# Build library
npm run build
```

## Future: Protocol Bridge

A future enhancement will allow connecting this emulator to a real Meshtastic firmware instance (running on native/portduino), enabling live framebuffer streaming over WebSocket for real-time display mirroring.

## License

GPL-3.0 - Same as Meshtastic firmware
