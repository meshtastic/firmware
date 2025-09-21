# IMU and Magnetometer Integration (QMI8658 + QMC6310)

This document explains the implementation work added for the LilyGo T-Beam S3 Supreme to support:

- QMI8658 6‑axis IMU over SPI (accelerometer + gyroscope) with a debug stream and a UI page
- QMC6310 3‑axis magnetometer over I2C with live hard‑iron calibration, heading computation, and a UI page
- I2C scanner improvements for robust IMU detection
- A small “live data” layer so UI screens do not reset sensors

The focus is on the math used and how the code is wired together.

---

## Files and Components

- QMI8658 (SPI) driver wrapper
  - `src/motion/QMI8658Sensor.h/.cpp`
- QMC6310 (I2C) driver wrapper
  - `src/motion/QMC6310Sensor.h/.cpp`
- Live data shared with UI (prevents sensor resets by UI)
  - `src/motion/SensorLiveData.h/.cpp` (globals `g_qmi8658Live`, `g_qmc6310Live`)
- I2C scanner and main wiring
  - `src/detect/ScanI2CTwoWire.cpp`, `src/detect/ScanI2C.cpp`, `src/main.cpp`
- UI Screens (added after the GPS screen)
  - `src/graphics/draw/DebugRenderer.h/.cpp`
  - `src/graphics/Screen.cpp`

Dependency pulled via PlatformIO:

- Lewis He SensorLib (provides `SensorQMI8658.hpp`, `SensorQMC6310.hpp`) pinned in `platformio.ini`.

---

## QMI8658 (SPI) – Implementation and Math

### Bus + Initialization

- On ESP32‑S3, we use HSPI for the IMU to avoid clashing with radio SPI:
  - Pins (T‑Beam S3 Supreme): `MOSI=35`, `MISO=37`, `SCK=36`, `IMU_CS=34`.
  - Code creates a local `SPIClass(HSPI)` and calls `begin(SCK, MISO, MOSI, -1)`; sets `IMU_CS` HIGH.
- The driver is configured as:
  - Accelerometer range: ±4 g, ODR ≈ 1000 Hz, LPF mode 0
  - Gyroscope range: ±64 dps, ODR ≈ 897 Hz, LPF mode 3
- The thread (`QMI8658Sensor`) runs continuously. When `QMI8658_DEBUG_STREAM` is enabled, it samples every pass but logs once per second.

### Units

- Accelerometer is reported in m/s² by the library. To measure total acceleration (for wake‑on‑motion), we compute:

  ```
  |a| = sqrt(ax² + ay² + az²)
  |a|_g = |a| / 9.80665
  Δ = |a|_g − 1.0
  ```

  If `Δ` exceeds a small threshold (`0.15 g`), we wake the screen.

### Debug Stream

- The debug line (1 Hz) prints:

  `QMI8658: ready=<0/1> ACC[x y z] m/s^2 GYR[x y z] dps`

  This is also mirrored into the live data struct `g_qmi8658Live` that the UI reads.

---

## QMC6310 (I2C) – Implementation and Math

### Bus + Initialization

- Address: `0x1C` on the sensors bus (Wire). The I2C scanner detects it and exposes it as `ScanI2C::QMC6310`.
- Configuration via SensorLib wrapper:
  - Mode: continuous
  - Range: 2 G
  - ODR: 50 Hz
  - Oversample: 8×, Downsample: 1×

### Hard‑Iron Calibration (live)

We continuously track min/max per axis and compute offsets as the center:

```
minX = min(minX, rawX)   maxX = max(maxX, rawX)
minY = min(minY, rawY)   maxY = max(maxY, rawY)
minZ = min(minZ, rawZ)   maxZ = max(maxZ, rawZ)

offsetX = (maxX + minX) / 2
offsetY = (maxY + minY) / 2
offsetZ = (maxZ + minZ) / 2

mx = rawX − offsetX
my = rawY − offsetY
mz = rawZ − offsetZ
```

This removes hard‑iron bias (DC offset) and is adequate for real‑time heading stabilization. For best results, slowly rotate the device on all axes for several seconds to let min/max settle.

Soft‑iron distortion (elliptical scaling) is NOT corrected here. A future enhancement can compute per‑axis scale from `(max−min)/2` or use an ellipsoid fit.

### Heading Computation

Raw 2‑D horizontal heading (no tilt compensation):

```
heading_deg = atan2(my, mx) * 180/π
heading_true = wrap_0_360( heading_deg + declination_deg + yaw_mount_offset )
```

Where:

- `declination_deg` compensates for local magnetic declination (positive East, negative West). We support a build‑time macro `QMC6310_DECLINATION_DEG`.
- `yaw_mount_offset` lets you nudge heading for how the board is mounted; build‑time macro `QMC6310_YAW_MOUNT_OFFSET`.
- `wrap_0_360(θ)` folds θ into `[0, 360)` by repeated add/subtract 360.

Screen orientation (0/90/180/270) is applied after heading is computed and normalized.

### Tilt‑Compensated Heading (future option)

If pitch/roll are available (from IMU), heading can be tilt‑compensated:

```
mx' = mx*cos(θ) + mz*sin(θ)
my' = mx*sin(φ)*sin(θ) + my*cos(φ) − mz*sin(φ)*cos(θ)
heading = atan2(my', mx')
```

Where `φ` is roll and `θ` is pitch (radians), derived from accelerometer. This is not implemented yet but can be added.

### Live Data

The magnetometer thread writes the latest raw XYZ, offsets and heading into `g_qmc6310Live` for the UI to display without touching hardware.

---

## I2C Scanner Improvements

### Dual‑address detection for IMUs

- QMI8658 is now probed at both `0x6B` and `0x6A`.
- To avoid collisions with chargers on `0x6B`, we first check:
  - BQ24295 ID via register `0x0A == 0xC0`
  - BQ25896 via `0x14` (bits 1:0 == `0b10`)
- If not a charger, we read `0x0F`:
  - `0x6A` → classify as LSM6DS3
  - otherwise → classify as QMI8658

### IMU‑only late rescan

- After a normal pass, if neither QMI8658 nor LSM6DS3 is found on a port, we wait 700 ms and probe just `0x6A/0x6B` again. This helps boards that power the IMU slightly late.

---

## UI Screens

Two additional screens are inserted right after the GPS screen:

1) QMC6310 screen
   - Shows `Heading`, `offX/offY`, and `rawX/rawY` (1 Hz)
   - Data source: `g_qmc6310Live`

2) QMI8658 screen
   - Shows `ACC x/y/z` (m/s²) and `GYR x/y/z` (dps) (1 Hz)
   - Data source: `g_qmi8658Live`

Because these screens read the live data structs, they do NOT call `begin()` on sensors (which would reset them). This resolved the issue where the screen showed all zeros after switching.

---

## Build Flags and Configuration

- Global debug stream (QMI8658):
  - `-D QMI8658_DEBUG_STREAM` (enabled in the T‑Beam S3 Core variant)
  - When enabled, the main will also start a parallel IMU debug thread even if an I2C accelerometer/magnetometer is present.

- Declination and mount offset for QMC6310 heading (optional):
  - `-D QMC6310_DECLINATION_DEG=<deg>` (e.g., `-0.25` for ≈ 0°15′ W)
  - `-D QMC6310_YAW_MOUNT_OFFSET=<deg>` (tune to match a known reference)

- SensorLib dependency (Lewis He):
  - Pinned in `platformio.ini` under `[arduino_base]`:
    `https://github.com/lewisxhe/SensorLib/archive/769b48472278aeaa62d5d0526eccceb74abe649a.zip`

---

## Example Logs

```
QMC6310: head=137.5 off[x=-12900 y=9352 z=12106] raw[x=-12990 y=9435 z=12134]
QMI8658: ready=1 ACC[x=-0.782 y=0.048 z=0.539] m/s^2  GYR[x=11.742 y=4.570 z=1.836] dps
```

The values on the UI screens should match these, because both screens read from the live data updated by the threads.

---

## Known Limitations and Next Steps

- QMC6310 uses live hard‑iron calibration only; no soft‑iron compensation yet. We can add per‑axis scale from `(max−min)/2` or use an ellipsoid fit for better accuracy.
- Heading is not tilt‑compensated; adding pitch/roll from the IMU and applying the standard compensation will stabilize heading during motion.
- We currently do not persist calibration offsets across boots; adding storage (NVS) would improve user experience.
- The QMI8658 debug stream is designed for development and can be disabled via build flag to reduce log noise.

---

## Troubleshooting

- If QMI8658 shows zeros on the UI screen, ensure `QMI8658_DEBUG_STREAM` is enabled or let the background IMU thread initialize first (it sets `g_qmi8658Live.initialized`).
- If QMC6310 heading appears constrained or jumps, rotate the device slowly on all axes for 10–20 seconds to update min/max; verify you’re on the correct I2C port and address (`0x1C`).
- If the I2C scan does not find the IMU on power‑on, check that the late‑rescan log appears; some boards power the sensor rail slightly later.

