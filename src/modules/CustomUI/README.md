# Custom UI Module for Meshtastic

A modular UI framework for Meshtastic ESP32-S3 devices with external ST7789 display and 4x4 matrix keypad support.

## Overview

This custom UI module provides a complete interface for Meshtastic devices, featuring:
- **External ST7789 Display** (320x240 landscape)
- **4x4 Matrix Keypad** input
- **Message Queue System** with popup notifications
- **Multi-screen Navigation** architecture
- **Real-time System Monitoring**

## Hardware Requirements

### Display (ST7789)
- **Resolution**: 320x240 (landscape orientation)
- **Connection**: Software SPI
- **Pin Configuration**:
  ```
  MOSI (Data)    -> GPIO 5
  SCLK (Clock)   -> GPIO 6
  CS (Chip Sel)  -> GPIO 1
  DC (Data/Cmd)  -> GPIO 2
  RST (Reset)    -> GPIO 3
  BL (Backlight) -> GPIO 4
  ```

### Keypad (4x4 Matrix)
- **Layout**: Standard 4x4 matrix (0-9, A-D, *, #)
- **Debounce**: 110ms
- **Pin Configuration**:
  ```
  Row Pins: R1=47, R2=33, R3=34, R4=7
  Col Pins: C1=48, C2=21, C3=20, C4=19
  ```

### Key Mappings
```
┌───┬───┬───┬───┐
│ 1 │ 2 │ 3 │ A │  A = Back Navigation
├───┼───┼───┼───┤
│ 4 │ 5 │ 6 │ B │
├───┼───┼───┼───┤
│ 7 │ 8 │ 9 │ C │
├───┼───┼───┼───┤
│ * │ 0 │ # │ D │
└───┴───┴───┴───┘

Key Functions:
- '1' : Open Nodes List
- 'A' : Navigate Back
- Any key: Dismiss message popup
```

## Architecture

### Module Structure

```
CustomUI/
├── CustomUIModule.cpp/h    # Main module, message queue, input handling
├── UINavigator.cpp/h        # Screen navigation and state management
├── UIDataState.cpp/h        # Data model with change detection
├── BaseScreen.cpp/h         # Base class for all screens
├── HomeScreen.cpp/h         # Main dashboard screen
└── NodesListScreen.cpp/h    # Mesh nodes list screen
```

### Screen Hierarchy

```
HomeScreen (default)
    └─► NodesListScreen
        └─► (navigate back)
```

## Features

### 1. Message Queue System
- **Capacity**: 10 messages
- **Auto-popup**: Messages automatically display when received
- **Persistent**: Queue maintained until dismissed
- **Information Shown**:
  - Sender name
  - Node ID
  - Timestamp (relative)
  - Message content (word-wrapped)

### 2. Home Screen
Displays real-time system information:
- **Node Information**: ID, short name
- **Network Status**: Connected/Searching, node count
- **System Stats**: 
  - LoRa connection status
  - Free heap memory
  - System uptime
- **LoRa Configuration**: Region, modem preset
- **Power Status**: Battery percentage or external power indicator

### 3. Nodes List Screen
Shows all mesh nodes with:
- Node short name or ID
- Last heard timestamp
- Scrollable list (future enhancement)

### 4. Input Handling
- **Hardware Button** (GPIO 0): Navigate to nodes list / dismiss messages
- **Keypad Keys**: Fast response (50ms polling rate)
  - Instant feedback with 110ms debounce
  - Multiple functions per screen context

## Performance Optimizations

### Update Strategy
- **Data Update**: Every 2 seconds
- **Display Update**: Every 100ms (when needed)
- **Input Polling**: Every 50ms (20Hz for instant response)
- **Dirty Rectangle**: Timestamp updates without full redraw

### Change Detection
- Only redraws when actual data changes
- Compares new data vs. cached data
- Efficient memory usage with static buffers

## Build Configuration

### Platform.ini Entry
```ini
[env:heltec-v3-custom]
extends = env:heltec-v3
build_flags =
    ${env:heltec-v3.build_flags}
    -DVARIANT_heltec_v3_custom
```

### Required Libraries
- Adafruit ST7735 and ST7789 Library
- Adafruit GFX Library
- Keypad Library (by Mark Stanley, Alexander Brevig)

## Usage

### Compilation
The module only compiles when building the `heltec-v3-custom` variant:
```bash
platformio run -e heltec-v3-custom
```

### Navigation Flow
1. **Power On**: Shows "HACKER CENTRAL" splash screen
2. **Home Screen**: Default view with system stats
3. **Press '1'**: Opens nodes list
4. **Press 'A'**: Returns to previous screen
5. **Receive Message**: Auto-popup appears
6. **Press Any Key**: Dismisses message, returns to current screen

### Message Handling
- Incoming text messages automatically queue
- Popup shows immediately if no other popup active
- Multiple messages queue up (FIFO)
- Counter badge shows unread message count
- Messages persist until explicitly dismissed

## Data Sources

### System Data
- **Node Info**: From `nodeDB` (NodeDB.h)
- **Battery**: From `powerStatus` (PowerStatus.h)
- **Memory**: From ESP32 `ESP.getFreeHeap()`
- **LoRa Config**: From `config.lora` (configuration.h)
- **Uptime**: From Arduino `millis()`

### Network Data
- **Node List**: From `nodeDB->getMeshNodeByIndex()`
- **Last Heard**: Timestamp from node records
- **Connection Status**: Based on node count > 1

## Customization

### Adding New Screens
1. Create new class inheriting from `BaseScreen`
2. Implement required methods:
   - `onEnter()` / `onExit()`
   - `handleInput()`
   - `needsUpdate()`
   - `draw()`
3. Add screen instance to `UINavigator`
4. Add navigation method in `UINavigator`

### Adding New Key Functions
1. Edit `CustomUIModule::checkKeypadInput()`
2. Add new case to switch statement
3. Call navigator or custom function

### Styling
Colors defined in `HomeScreen.h`:
```cpp
#define COLOR_BACKGROUND ST77XX_BLACK
#define COLOR_TEXT       ST77XX_WHITE
#define COLOR_ACCENT     ST77XX_CYAN
#define COLOR_HEADER     ST77XX_GREEN
#define COLOR_SUCCESS    ST77XX_GREEN
#define COLOR_WARNING    ST77XX_YELLOW
#define COLOR_ERROR      ST77XX_RED
```

## Troubleshooting

### Display Not Working
- Check SPI pin connections
- Verify backlight is on (GPIO 4 HIGH)
- Confirm software SPI initialization
- Check display rotation setting

### Keypad Not Responding
- Verify row/column pin connections
- Check debounce time (default 110ms)
- Confirm polling rate is 50ms
- Test with serial logging enabled

### Battery Shows 100% Always
- Verify board has battery sensor
- Check `powerStatus` initialization
- Confirm `BATTERY_PIN` or `HAS_PMU` defined
- Review variant configuration

### Messages Not Appearing
- Check message queue size (max 10)
- Verify `TEXT_MESSAGE_APP` port handling
- Confirm `ProcessMessage::CONTINUE` returned
- Check popup dismiss logic

## Debug Logging

Enable logging to see module activity:
- Keypad presses
- Message queue operations
- Navigation events
- Display updates

Look for log prefix: `🔧 CUSTOM UI:`

## Future Enhancements

- [ ] Settings screen
- [ ] Direct messaging screen
- [ ] Channel configuration screen
- [ ] Scrolling for long lists
- [ ] More keypad shortcuts
- [ ] Touch screen support
- [ ] Custom themes
- [ ] Graph visualizations

## License

Part of the Meshtastic project - GPL-3.0 License

## Credits

Created for Heltec V3 custom variant with external display and keypad support.
