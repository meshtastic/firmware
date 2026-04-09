# ESP32 OTA Co-processor B — BLE DFU Client / I2C Slave

## Rol en el sistema

El co-procesador B reside **físicamente en el nodo remoto** junto al nRF52840 (Meshtastic). Permanece apagado la mayor parte del tiempo; el nRF52 lo enciende a través de un pin GPIO cuando se solicita una actualización OTA desde la red mesh.

```
  Red Meshtastic (LoRa)
         │
    [nRF52840]  ──── Meshtastic firmware
         │ GPIO (P1.06 = OUTPUT_GPIO_PIN)  ──► CHIP_PU/EN C5-B
         │ I2C  (P1.04 SDA / P0.11 SCL)   ◄── comandos / estado
         │ BLE  (DFU bootloader)           ◄── transferencia firmware
         │
    [ESP32 C5-B]  ◄──── este dispositivo
         │
    WiFi (2.4 GHz / 5 GHz si C5)
         │
    [ESP32 C5-A]  ──── servidor de firmware
```

---

## Hardware requerido

| Componente | Notas |
|---|---|
| ESP32-C5 / C6 / C3 | Cualquier board con WiFi + BLE y 4 MB flash |
| nRF52840 Pro Micro | Ejecutando Meshtastic con `OtaRequestModule` |
| 3 cables | SDA, SCL, GND entre nRF52 y ESP32 |

### Conexiones I2C

| nRF52840 Pro Micro | ESP32 C5-B |
|---|---|
| P1.04 (SDA, pin 36) | GPIO6 (SDA, configurable en `config.h`) |
| P0.11 (SCL, pin 11) | GPIO7 (SCL, configurable en `config.h`) |
| GND | GND |

### Control de alimentación — conexión directa a CHIP_PU/EN

```
nRF52 P1.06 (OUTPUT_GPIO_PIN) ─────────────► CHIP_PU / EN (ESP32 C5-B)
                                              (sin MOSFET ni transistor)
```

El pin CHIP_PU/EN del ESP32 acepta directamente la señal del GPIO del nRF52 (ambos a 3.3 V):

- **HIGH (3.3 V):** ESP32 encendido y en ejecución
- **LOW (0 V):** ESP32 completamente apagado — consumo **~0.002 mA**

**Importante:** el nRF52 solo pone P1.06 en HIGH **después del reboot**, en `GpioOutputModule::apply()`. El AdminModule NO activa el GPIO antes del reboot al recibir la orden OFF→ON. Esto garantiza que el C5-B arranque limpio una sola vez y no sufra un ciclo de alimentación innecesario durante el reboot del nRF52.

Si el C5-B no responde en el primer I2C scan (puede estar aún arrancando), el `OtaRequestModule` entra en `STATE_PROBE_RETRY` y sondea la dirección 0x42 directamente cada 2 s durante hasta 60 s antes de abortar.

---

## Estructura del proyecto

```
esp32_ota_coprocessor/
├── platformio.ini
└── src/
    ├── config.h             ← dirección I2C, pines, WiFi C5-A, URLs, UUIDs BLE
    ├── i2c_slave.h/.cpp     ← Wire slave en 0x42, callbacks onReceive/onRequest
    ├── wifi_downloader.h/.cpp ← WiFi STA + HTTP GET → LittleFS
    ├── ble_dfu.h/.cpp       ← Nordic Secure DFU client (NimBLE-Arduino)
    └── main.cpp             ← máquina de estados principal
```

---

## Configuración

Todo lo configurable está en [`src/config.h`](src/config.h):

```c
// I2C slave — debe coincidir con ESP32C5B_I2C_ADDR en OtaRequestModule.h
#define I2C_SLAVE_ADDR  0x42
#define I2C_SDA_PIN     6
#define I2C_SCL_PIN     7

// WiFi del C5-A — debe coincidir con AP_SSID/AP_PASS de C5-A
#define WIFI_SSID       "OTA_C5A"
#define WIFI_PASS       "meshtastic_ota"
#define WIFI_TIMEOUT_MS 20000

// URLs de los archivos en C5-A
#define FIRMWARE_URL_DAT    "http://192.168.4.1/firmware.dat"
#define FIRMWARE_URL_BIN    "http://192.168.4.1/firmware.bin"

// Rutas locales en LittleFS
#define FIRMWARE_PATH_DAT   "/fw.dat"
#define FIRMWARE_PATH_BIN   "/fw.bin"

// BLE DFU
#define DFU_TARGET_NAME     "DfuTarg"
#define DFU_SCAN_SECS       10
#define DFU_SCAN_RETRIES    6
```

---

## Compilar y flashear

```bash
cd esp32_ota_coprocessor

# ESP32-C6 (recomendado para pruebas)
~/.platformio/penv/Scripts/pio run -e esp32c6 --target upload

# ESP32-C3
~/.platformio/penv/Scripts/pio run -e esp32c3 --target upload

# ESP32-C5 (producción)
~/.platformio/penv/Scripts/pio run -e esp32c5 --target upload

# Monitor serie
~/.platformio/penv/Scripts/pio device monitor
```

---

## Protocolo I2C

C5-B actúa como **I2C slave en la dirección `0x42`**. El nRF52 es el master.

### Comandos (nRF52 → C5-B)

| Byte | Nombre | Descripción |
|------|--------|-------------|
| `0x01` | `CMD_STATUS` | Consulta el estado actual. nRF52 hace `write(0x01)` + `requestFrom(1)` |
| `0x02` | `CMD_START` | Inicia conexión WiFi + descarga del firmware |
| `0x03` | `CMD_ENTER_DFU` | nRF52 avisa que va a entrar en DFU bootloader; C5-B inicia escaneo BLE |
| `0x04` | `CMD_DONE` | nRF52 arrancó con el nuevo firmware; C5-B limpia archivos y vuelve a IDLE |

### Bytes de estado (C5-B → nRF52, respuesta a CMD_STATUS)

| Byte | Nombre | Descripción |
|------|--------|-------------|
| `0xA0` | `STATUS_IDLE` | Esperando `CMD_START` |
| `0xA1` | `STATUS_CONNECTING` | Conectando al WiFi del C5-A |
| `0xA2` | `STATUS_DOWNLOADING` | Descargando `fw.dat` y `fw.bin` |
| `0xA3` | `STATUS_READY` | Firmware descargado, listo para DFU |
| `0xA4` | `STATUS_BLE_DFU` | Transferencia BLE DFU en progreso |
| `0xA5` | `STATUS_DFU_DONE` | DFU completado; nRF52 arranció con nuevo firmware |
| `0xAF` | `STATUS_ERROR` | Error no recuperable |

### Secuencia I2C completa

```
nRF52                          C5-B (0x42)
  │                                │
  │── write(0x02) CMD_START ───────►│  g_status = CONNECTING
  │                                │  WiFi → C5-A
  │                                │  g_status = DOWNLOADING
  │                                │  HTTP GET fw.dat → LittleFS
  │                                │  HTTP GET fw.bin → LittleFS
  │                                │  g_status = READY
  │                                │
  │── write(0x01) + read(1) ───────►│
  │◄── 0xA3 STATUS_READY ──────────│
  │                                │
  │── write(0x03) CMD_ENTER_DFU ──►│  espera 3 s (nRF52 reseteando)
  │ [nRF52 → DFU bootloader]       │  escanea BLE "DfuTarg"
  │ [nRF52 anuncia "DfuTarg"]      │  conecta, transfiere fw.dat + fw.bin
  │ [nRF52 valida + arranca]       │  g_status = DFU_DONE
  │                                │
  │── write(0x01) + read(1) ───────►│
  │◄── 0xA5 STATUS_DFU_DONE ───────│
  │                                │
  │── write(0x04) CMD_DONE ────────►│  borra fw.dat + fw.bin
  │ [nRF52 pone OUTPUT_GPIO LOW]   │  g_status = IDLE
  │ [C5-B pierde alimentación]     │  (apagado)
```

---

## Nordic Secure DFU — protocolo BLE

C5-B implementa el cliente del protocolo Nordic Secure DFU usando NimBLE-Arduino.

### Servicio y características

| UUID | Característica | Tipo de escritura |
|------|----------------|-------------------|
| `FE59` | Secure DFU Service | — |
| `8EC90001-F315-4F60-9FB8-838830DAEA50` | Control Point | Write with response + Notify |
| `8EC90002-F315-4F60-9FB8-838830DAEA50` | Packet | Write without response |

### Secuencia de transferencia (implementada en `ble_dfu.cpp`)

```
1. Scan BLE → busca "DfuTarg" (reintentos cada 10 s, hasta 60 s)
2. Connect → negociación MTU (preferencia 512; nRF52 bootloader acepta hasta 247)
3. Discover service FE59 → get Control Point + Packet characteristics
4. Subscribe a notificaciones de Control Point

── Init packet (.dat) ──────────────────────────────────────────
5. Select Command Object (0x06, 0x01) → obtiene max_size
6. Create Command Object (0x01, 0x01, dat_size LE32)
7. Write fw.dat → Packet (chunks de MTU-3 bytes, write without response)
8. Calculate Checksum (0x03) → verifica CRC32
9. Execute (0x04) → nRF52 valida firma del init packet

── Firmware binary (.bin) ───────────────────────────────────────
10. Select Data Object (0x06, 0x02) → obtiene max_size del segmento
11. Para cada segmento de max_size bytes del .bin:
    a. Create Data Object (0x01, 0x02, seg_size LE32)
    b. Write segmento → Packet (chunks de MTU-3 bytes)
    c. Calculate Checksum → verifica CRC32 acumulado
    d. Execute → nRF52 almacena y verifica el segmento
12. Tras el último Execute: nRF52 valida CRC total y arranca nuevo firmware
```

### CRC32

Se usa el polinomio estándar IEEE 802.3 (`0xEDB88320`), igual que el bootloader de Adafruit. El CRC es **acumulado** a lo largo de todos los segmentos del `.bin`.

---

## Máquina de estados (main.cpp)

```
Arranque
   │ i2cSlaveInit(0x42)
   │ LittleFS.begin()
   ▼
IDLE (g_status = 0xA0)
   │ CMD_START recibido
   ▼
CONNECTING (0xA1) ──── WiFi.begin(WIFI_SSID) ────────────────► ERROR (0xAF)
   │ conectado
   ▼
DOWNLOADING (0xA2) ── GET /firmware.dat + /firmware.bin ────► ERROR
   │ descargado
   ▼
READY (0xA3) ── WiFi.disconnect()
   │ CMD_ENTER_DFU recibido
   │ delay(3000)  ← espera bootloader nRF52
   ▼
BLE_DFU (0xA4) ── scan + connect + Nordic DFU ─────────────► ERROR
   │ DFU completado
   ▼
DFU_DONE (0xA5)
   │ CMD_DONE recibido
   │ LittleFS.remove(fw.dat + fw.bin)
   ▼
IDLE (0xA0)  ← (alimentación cortada por nRF52 en este punto)
```

---

## Dependencias

| Librería | Versión | Uso |
|----------|---------|-----|
| `h2zero/NimBLE-Arduino` | ^1.4.3 | Stack BLE (scan, connect, GATT client) |
| `LittleFS` | (incluida en esp32 core) | Almacenamiento de fw.dat y fw.bin |
| `WiFi` + `HTTPClient` | (incluidas en esp32 core) | Descarga desde C5-A |

---

## Notas de diseño

- **C5-B permanece apagado** la mayor parte del tiempo. Solo consume energía durante la ventana de OTA (típicamente 2-5 minutos).
- **Sin estado persistente en NVS.** Tras cada encendido, C5-B empieza en `STATUS_IDLE`. El nRF52 detecta el estado `STATUS_DFU_DONE` en el arranque post-OTA gracias a que C5-B sigue encendido (GPIO sigue en HIGH) hasta recibir `CMD_DONE`.
- **WiFi y BLE no se usan simultáneamente** en el C5-B: primero descarga por WiFi (luego desconecta), luego usa BLE para el DFU. Esto evita interferencias RF y simplifica la gestión de recursos.
- **Timeout de descarga:** el `HTTPClient` tiene un timeout de 30 s por archivo. El poll de status del nRF52 tiene un timeout de 5 min (60 × 5 s).
- **Reintento de scan BLE:** hasta 6 intentos de 10 s cada uno (60 s total) para encontrar "DfuTarg". Tiempo suficiente para que el bootloader nRF52 arranque y empiece a anunciar.
