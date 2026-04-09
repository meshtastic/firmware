# ESP32 OTA Co-processor A — Firmware Uploader / File Server

## Rol en el sistema

El co-procesador A es el **lado con acceso físico al técnico**. Su función es recibir el firmware de Meshtastic (.zip Nordic DFU) desde un móvil o portátil y servirlo al co-procesador B cuando este lo solicite.

```
        Técnico (móvil/PC)
              │
         WiFi AP (2.4 GHz)
              │
         [ESP32 C5-A]  ◄──── este dispositivo
              │
         WiFi (2.4 GHz o 5 GHz si C5)
              │
         [ESP32 C5-B]  ──── nodo remoto
              │ BLE DFU
         [nRF52840]    ──── Meshtastic
```

C5-A **no necesita internet**. El técnico descarga el `.zip` oficial de Meshtastic en su teléfono y lo sube a C5-A a través del navegador.

---

## Hardware requerido

| Componente | Notas |
|---|---|
| ESP32-C5 / C6 / C3 | Cualquier board con WiFi y 4 MB flash |
| Antena WiFi | La integrada es suficiente para uso en campo |
| Fuente de alimentación | USB o batería 3.3V/5V |

> **Recomendación para pruebas:** ESP32-C6 (800 KB SRAM, WiFi 6). El C3 funciona pero tiene RAM justa.

---

## Estructura del proyecto

```
esp32_ota_coprocessor_A/
├── platformio.ini
└── src/
    ├── config.h            ← credenciales AP y rutas LittleFS
    ├── fw_manager.h/.cpp   ← parser ZIP + extracción DEFLATE (zlib)
    ├── http_server.h/.cpp  ← AsyncWebServer: UI web + endpoints
    └── main.cpp            ← setup (AP + HTTP) + loop
```

---

## Configuración

Todo lo configurable está en [`src/config.h`](src/config.h):

```c
#define AP_SSID     "OTA_C5A"           // SSID del punto de acceso
#define AP_PASS     "meshtastic_ota"    // Contraseña (mín. 8 chars)
#define AP_CHANNEL  6                   // Canal WiFi (1-13)
#define AP_IP       "192.168.4.1"       // IP del gateway (por defecto ESP32)

#define ZIP_TMP_PATH  "/upload.zip"     // Archivo temporal durante upload
#define FW_DAT_PATH   "/fw.dat"         // Init packet extraído
#define FW_BIN_PATH   "/fw.bin"         // Firmware binario extraído
```

> Los archivos extraídos persisten en LittleFS entre reinicios. Al arrancar, C5-A detecta si ya hay firmware cacheado y lo pone disponible de inmediato.

---

## Compilar y flashear

```bash
cd esp32_ota_coprocessor_A

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

## Uso — Subir firmware desde el móvil

1. **Descargar** el `.zip` de Meshtastic para `nrf52_promicro_diy_tcxo` desde  
   `https://github.com/meshtastic/firmware/releases`  
   (archivo: `firmware-nrf52_promicro_diy_tcxo-x.x.x.xxxxxxx.zip`)

2. **Conectar** el móvil/portátil al WiFi `OTA_C5A`

3. **Abrir** `http://192.168.4.1` en el navegador

4. **Seleccionar** el `.zip` y pulsar **"Subir y extraer"**

5. La barra de progreso muestra el upload. Al terminar, C5-A extrae automáticamente los archivos y la UI muestra:
   ```
   ✓ Firmware listo | DAT: 138 B   BIN: 512.4 KB
   ```

6. El firmware queda disponible indefinidamente (persiste en flash). C5-B puede descargarlo en cualquier momento.

---

## API HTTP

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/` | Interfaz web (formulario de subida) |
| `POST` | `/upload` | Recibe el `.zip` (multipart/form-data, campo `file`) |
| `GET` | `/status` | Estado en JSON |
| `GET` | `/firmware.dat` | Sirve el init packet al C5-B |
| `GET` | `/firmware.bin` | Sirve el firmware binario al C5-B |

### Respuesta de `/status`

```json
{
  "state": "ready",
  "dat_b": 138,
  "bin_b": 524800
}
```

Valores de `state`: `empty` · `uploading` · `extracting` · `ready` · `error`

### Respuesta de `POST /upload`

```json
{ "ok": true }
```

En caso de error: `{ "ok": false, "error": "write failed" }`

---

## Formato ZIP esperado (Nordic DFU)

El `.zip` oficial de Meshtastic/Adafruit nRF52 contiene:

```
firmware-nrf52_promicro_diy_tcxo-2.x.x.zip
├── manifest.json                            ← ignorado por C5-A
├── firmware-nrf52_promicro_diy_tcxo-2.x.x.dat   ← init packet (firmado)
└── firmware-nrf52_promicro_diy_tcxo-2.x.x.bin   ← aplicación (~500 KB)
```

C5-A detecta los archivos por extensión (`.dat` → `fw.dat`, `.bin` → `fw.bin`).  
Soporta compresión `STORED` (método 0) y `DEFLATED` (método 8).  
No soporta Data Descriptors (bit 3 del ZIP flags) — los zips de Meshtastic no los usan.

---

## Diagrama de estados internos

```
EMPTY
  │  POST /upload recibido
  ▼
UPLOADING  (escribiendo upload.zip en LittleFS)
  │  upload completo
  ▼
EXTRACTING (fwManagerTick() en loop(): parse ZIP + inflate)
  │                         │
  ▼ éxito                   ▼ error
READY                     ERROR
  │  (fw.dat + fw.bin
  │   disponibles)
  ▼  nuevo upload
UPLOADING  ...
```

---

## Dependencias

| Librería | Versión | Uso |
|----------|---------|-----|
| `mathieucarbou/ESPAsyncWebServer` | ^3.3.23 | Servidor HTTP async |
| `mathieucarbou/AsyncTCP` | ^3.3.6 | TCP para AsyncWebServer |
| `zlib` | (incluida en ESP-IDF) | Descompresión DEFLATE del ZIP |

---

## Notas de diseño

- **Sin dependencias de ZIP externas:** el parser ZIP está implementado directamente en `fw_manager.cpp` (~150 líneas). Usa `zlib` del ESP-IDF (ya en el toolchain), que implementa DEFLATE raw (modo `-MAX_WBITS`).
- **La extracción corre en `loop()`**, no en el callback del webserver, para no bloquear la tarea async de AsyncTCP.
- **El firmware persiste entre reinicios.** Si C5-A se reinicia con archivos ya extraídos, los detecta en el arranque y pasa directamente a estado `READY`.
- **C5-A no tiene lógica I2C ni BLE.** Es un dispositivo completamente independiente; solo necesita WiFi.
