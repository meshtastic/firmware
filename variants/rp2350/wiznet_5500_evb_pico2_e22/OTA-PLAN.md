# Plan de implementación: OTA via Ethernet para RP2350

## Contexto

| Aspecto | ESP32 (actual) | RP2350 (actual) | RP2350 (objetivo) |
|---------|----------------|-----------------|-------------------|
| OTA | WiFi/BLE via loader en ota_1 | Solo USB BOOTSEL | **Ethernet TCP** |
| Flash | Particiones dual (ota_0 + ota_1) | Flat (app + LittleFS) | Usar Updater + PicoOTA nativos |
| Red | WiFi nativa | W5500 Ethernet (SPI0) | W5500 Ethernet |

---

# FASE 0 — Resultados de investigación

## 0.1 API Flash del RP2350

### Hallazgo clave: arduino-pico YA TIENE un sistema OTA completo

El framework arduino-pico (earlephilhower) incluye:

1. **Bootloader OTA integrado** (12KB, en cada imagen flash):
   - Reside en los primeros 12KB de flash (0x10000000–0x10002FFF)
   - Se ejecuta **antes** del app en cada boot
   - Monta LittleFS, busca `otacommand.bin`
   - Si existe y CRC32 es válido: lee `firmware.bin` de LittleFS → flashea sector por sector
   - Soporta **GZIP** nativo (descomprime al flashear)
   - Power-fail safe (excepto auto-update del propio bootloader)

2. **Clase `Updater`** (API compatible con ESP8266/ESP32):
   ```cpp
   #include <Updater.h>
   Update.begin(size);           // Abre firmware.bin en LittleFS
   Update.write(data, len);      // Escribe chunks (buffer 4KB)
   Update.setMD5("...");         // Verificación MD5
   Update.end();                 // Cierra → picoOTA.commit() → reinicia a bootloader
   ```

3. **Clase `PicoOTA`** (bajo nivel):
   ```cpp
   picoOTA.begin();
   picoOTA.addFile("firmware.bin");  // Soporta GZIP automático
   picoOTA.commit();                 // Escribe otacommand.bin con CRC32
   // Al reiniciar, el bootloader aplica la actualización
   ```

### Funciones flash de bajo nivel (pico-sdk)
```c
// hardware/flash.h
#define FLASH_PAGE_SIZE    256    // Mínimo para write
#define FLASH_SECTOR_SIZE  4096   // Mínimo para erase

flash_range_erase(offset, count);    // offset desde XIP_BASE, count múltiplo de 4096
flash_range_program(offset, data, count); // count múltiplo de 256
```

### Seguridad multicore
```cpp
// Para operaciones flash directas (U_FS):
noInterrupts();
rp2040.idleOtherCore();
flash_range_erase(...);
flash_range_program(...);
rp2040.resumeOtherCore();
interrupts();
```

**NOTA: Para U_FLASH (OTA de firmware) NO se usan flash_range_* directamente.** El Updater escribe a LittleFS (archivo normal), y el bootloader OTA hace el flasheo real en el siguiente boot.

## 0.2 Mapa de memoria flash

### Layout actual (wiznet_5500_evb_pico2_e22, 2MB flash)

```
Dirección       Offset    Tamaño     Contenido
─────────────── ───────── ────────── ──────────────────────
0x10000000      0x000000  12 KB      Bootloader OTA (integrado)
0x10003000      0x003000  ~1,496 KB  App code (FLASH region del linker)
0x101BF400      0x1BF400  512 KB     LittleFS (_FS_start → _FS_end)
0x10200400      0x200400  —          Fin de flash útil
```

De `memmap_default.ld`:
```
FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 1568768  (≈1,532 KB = 1.496 MB)
_FS_start = 0x101BF400
_FS_end   = 0x10200400
```

### Tamaños medidos
| Recurso | Tamaño |
|---------|--------|
| Flash total | 2,097,152 bytes (2 MB) |
| App compilada (.bin) | **1,012,772 bytes (989 KB)** |
| App GZIP comprimida | **628,601 bytes (614 KB)** |
| LittleFS disponible | **524,288 bytes (512 KB)** |

### Problema: firmware GZIP (614 KB) > LittleFS (512 KB)

El firmware comprimido con GZIP **no cabe** en el LittleFS actual de 512KB.

### Soluciones evaluadas

| Opción | Descripción | Pros | Contras |
|--------|-------------|------|---------|
| **A: Aumentar filesystem a 768KB** | `board_build.filesystem_size = 0.75m` | Simple, usa Updater/PicoOTA nativos | Reduce espacio de código a ~1,280KB (firmware actual 989KB, queda margen ~300KB) |
| **B: Streaming directo a flash** | No usar LittleFS, escribir directamente a staging area | No depende del tamaño FS | Hay que reimplementar mucho, pierde power-fail safety del bootloader |
| **C: Reducir firmware** | Optimizar tamaño del binario | Cabe en 512KB FS | Difícil conseguir -27% reducción, frágil |

### **Decisión: Opción A — Aumentar filesystem a 768KB**

Justificación:
- Firmware actual: 989KB → espacio código necesario: ~1MB
- Con FS=768KB: espacio código = 2MB - 768KB = ~1,280KB → **margen de ~300KB**
- GZIP 614KB cabe en 768KB → **margen de ~154KB**
- Reutilizamos **toda** la infraestructura OTA nativa de arduino-pico (Updater + PicoOTA + bootloader)
- Power-fail safe gratis
- MD5 verification gratis
- Mínimo código nuevo necesario

## 0.3 Formato OTA: .bin con CRC32 + MD5 header

### Flujo completo de transferencia

```
[PC/Script Python]                    [RP2350 + W5500]
      |                                      |
      |  1. TCP connect :4243                |
      |─────────────────────────────────────→|
      |                                      |
      |  2. Header: magic + size + CRC32     |
      |─────────────────────────────────────→|
      |                                      | Valida tamaño,
      |                    ACK               | llama Update.begin(size)
      |←─────────────────────────────────────|
      |                                      |
      |  3. Firmware .bin.gz (chunks 1KB)    |
      |─────────────────────────────────────→| Update.write(chunk)
      |                    ACK por chunk     |
      |←─────────────────────────────────────|
      |  ... repite hasta completar ...      |
      |                                      |
      |  4. (último chunk)                   |
      |─────────────────────────────────────→| Update.end()
      |                                      | → picoOTA.commit()
      |                    RESULT: OK        |
      |←─────────────────────────────────────|
      |                                      | watchdog_reboot()
      |                                      | Bootloader aplica update
```

### Header del protocolo TCP (12 bytes)

```c
typedef struct {
    uint8_t  magic[4];     // "MOTA" (Meshtastic OTA)
    uint32_t firmwareSize; // Tamaño del .bin.gz en bytes (little-endian)
    uint32_t crc32;        // CRC32 del payload completo
} __attribute__((packed)) OTAHeader;
```

### Verificaciones
1. **CRC32** del payload completo (calculado por el script, verificado tras recepción)
2. **MD5** via `Update.setMD5()` — el Updater lo verifica internamente
3. **CRC32 del bootloader** — PicoOTA escribe `otacommand.bin` con su propio CRC32, el bootloader lo verifica antes de aplicar

### Formato del binario
- **Archivo enviado**: firmware `.bin` (raw binary) → comprimido con GZIP → enviado via TCP
- **El bootloader descomprime GZIP** automáticamente al flashear
- No se necesita header especial en el binario, solo en el protocolo TCP

> **IMPORTANTE: Solo archivos `.bin` — NO `.uf2`**
>
> El pipeline OTA (Updater → picoOTA → bootloader) escribe los bytes recibidos directamente a flash.
> No hay detección ni conversión de formato UF2 en ningún punto de la cadena.
> Enviar un `.uf2` por OTA **corrompe el firmware** porque los headers UF2 (32 bytes cada 512 bytes)
> se escriben como si fueran código ejecutable. El resultado es un nodo que cae en modo BOOTSEL.
>
> El script `eth-ota-upload.py` rechaza archivos `.uf2` automáticamente.
> Los archivos `.uf2` solo sirven para flasheo USB vía BOOTSEL/picotool.

## Por qué NO usar picowota directamente

1. **WiFi-only** — picowota depende del chip CYW43 (Pico W). Nuestras boards no tienen WiFi, tienen W5500 Ethernet.
2. **Build system incompatible** — picowota usa CMake + Pico SDK nativo. Meshtastic usa PlatformIO + arduino-pico (earlephilhower).
3. **Bootloader duplica stack de red** — picowota incluye un stack WiFi completo (~300-400KB) en el bootloader. Con Ethernet sería menor, pero sigue siendo desperdicio de flash.

## Enfoque: Usar Updater + PicoOTA nativos de arduino-pico

**Hallazgo de Fase 0:** arduino-pico ya incluye un sistema OTA completo (bootloader 12KB + clase Updater + PicoOTA). Solo necesitamos:
1. Un servidor TCP que reciba el firmware via Ethernet
2. Pasar los datos al `Updater` nativo
3. Aumentar LittleFS a 768KB para que quepa el firmware GZIP

No necesitamos implementar escritura flash manual ni staging area — el bootloader integrado lo hace todo.

---

# PLAN DE IMPLEMENTACIÓN (actualizado tras Fase 0)

## ~~Fase 0: Investigación~~ ✅ COMPLETADA

Ver sección "FASE 0 — Resultados de investigación" arriba.

---

## Fase 1: Configuración de build + aumento LittleFS

| Tarea | Detalle |
|-------|---------|
| 1.1 | Cambiar `board_build.filesystem_size` de `0.5m` a `0.75m` **solo** en los platformio.ini de `wiznet_5500_evb_pico2_e22` y `pico2_w5500_e22`. Esto deja ~1,280KB para código (firmware actual 989KB, margen ~300KB). |
| 1.2 | Añadir build flag `-D HAS_ETHERNET_OTA` en ambos platformio.ini. |
| 1.3 | Verificar que `Updater` y `PicoOTA` se resuelven automáticamente (están en arduino-pico core). |
| 1.4 | Compilar y verificar que el firmware sigue funcionando correctamente. |

---

## Fase 2: Servidor TCP para recepción OTA

**Nuevos archivos:** `src/mesh/eth/ethOTA.h` / `ethOTA.cpp`

| Tarea | Detalle |
|-------|---------|
| 2.1 | Crear servidor TCP en puerto **4243** usando `EthernetServer` (librería ya integrada). |
| 2.2 | Implementar recepción de header (12 bytes) + validación magic "MOTA" + verificación de tamaño. |
| 2.3 | Llamar `Update.begin(firmwareSize)` — esto abre `firmware.bin` en LittleFS para escritura. |
| 2.4 | Recibir chunks de 1KB y pasarlos a `Update.write()` con ACK por chunk. |
| 2.5 | Al completar, verificar CRC32 y llamar `Update.end()` → esto ejecuta `picoOTA.commit()`. |
| 2.6 | Reiniciar con `rp2040.reboot()` — el bootloader integrado aplica el update automáticamente. |
| 2.7 | Timeout de inactividad (30s sin datos → `Update.end(false)` para abortar). |

### Pseudocódigo del servidor

```cpp
void handleOTAClient(EthernetClient &client) {
    OTAHeader hdr;
    client.readBytes((char*)&hdr, sizeof(hdr));
    
    if (memcmp(hdr.magic, "MOTA", 4) != 0) return;
    
    LittleFS.begin();
    if (!Update.begin(hdr.firmwareSize)) {
        client.write(0x02); // size error
        return;
    }
    
    client.write(0x06); // ACK header
    
    uint8_t buf[1024];
    size_t remaining = hdr.firmwareSize;
    OTACRC32 crc;
    
    while (remaining > 0) {
        size_t toRead = min(remaining, sizeof(buf));
        size_t got = client.readBytes(buf, toRead);
        if (got == 0) break; // timeout
        Update.write(buf, got);
        crc.add(buf, got);
        remaining -= got;
        client.write(0x06); // ACK chunk
    }
    
    if (crc.get() != hdr.crc32) {
        Update.end(false); // abort
        client.write(0x01); // CRC fail
        return;
    }
    
    if (Update.end(true)) {
        client.write(0x00); // OK
        delay(500);
        rp2040.reboot(); // Bootloader aplicará el update
    } else {
        client.write(0x03); // write error
    }
}
```

---

## Fase 3: Integración con el firmware Meshtastic

| Tarea | Detalle |
|-------|---------|
| 3.1 | Inicializar el servidor OTA en `ethClient.cpp` después de que Ethernet esté conectado (junto a initApiServer, MQTT, etc.). |
| 3.2 | Llamar `ethOTA.loop()` periódicamente desde el loop principal de Ethernet para aceptar conexiones. |
| 3.3 | Opcionalmente: integrar con `AdminModule` para que un comando admin pueda activar/desactivar el servidor OTA remotamente. |

---

## Fase 4: Script Python de upload

**Nuevo archivo:** `bin/eth-ota-upload.py`

| Tarea | Detalle |
|-------|---------|
| 4.1 | Script Python que acepta: IP del dispositivo + archivo `.bin` (o `.bin.gz`). |
| 4.2 | Si el archivo es `.bin`, lo comprime con GZIP automáticamente. |
| 4.3 | Calcula CRC32 del payload comprimido. |
| 4.4 | Envía header "MOTA" + size + CRC32, espera ACK. |
| 4.5 | Envía chunks de 1KB con barra de progreso, espera ACK por chunk. |
| 4.6 | Espera resultado final (OK / error). |

```bash
# Uso:
python bin/eth-ota-upload.py --host 192.168.1.100 --port 4243 firmware.bin
# Auto-comprime a GZIP y envía
```

---

## ~~Fase 5: Seguridad~~ ✅ COMPLETADA

| Tarea | Detalle | Estado |
|-------|---------|--------|
| 5.1 | **Autenticación PSK**: Challenge-response con SHA256. Servidor envía nonce 32 bytes → cliente responde SHA256(nonce ‖ PSK) → servidor verifica con comparación constant-time. | ✅ |
| 5.2 | **PSK configurable**: Default `"meshtastic_ota_default_psk_v1!!!"` (32 bytes). Override via `USERPREFS_OTA_PSK` build define. Script Python acepta `--psk` (UTF-8) o `--psk-hex`. | ✅ |
| 5.3 | **Anti-brick**: El bootloader de arduino-pico ya es power-fail safe (verifica CRC antes de flashear). Si `otacommand.bin` es corrupto, simplemente no actualiza y arranca el firmware anterior. | ✅ (nativo) |
| 5.4 | **Rate limiting**: Cooldown de 5s (`OTA_AUTH_COOLDOWN_MS`) tras autenticación fallida. Solo una conexión OTA a la vez (Updater singleton). | ✅ |
| 5.5 | **Timing attack mitigation**: Comparación constant-time (XOR accumulation) del hash de autenticación. | ✅ |

### Detalles de implementación Fase 5

**Protocolo de autenticación** (prepended al protocolo MOTA existente):

```
[Cliente]                              [Servidor]
    |  1. TCP connect :4243                |
    |─────────────────────────────────────→|
    |                                      | Genera nonce[32]
    |                 nonce (32 bytes)      |
    |←─────────────────────────────────────|
    |                                      |
    | SHA256(nonce || PSK) (32 bytes)      |
    |─────────────────────────────────────→| Verifica (constant-time XOR)
    |                                      |
    |            ACK (0x06) / ERR (0x07)   |
    |←─────────────────────────────────────|
    |                                      |
    |  ... protocolo MOTA continúa ...     |
```

**Crypto**: SHA256 de la librería `rweather/Crypto@0.4.0` (ya dependencia existente).

**Archivos modificados:**
- `src/mesh/eth/ethOTA.cpp` — `authenticateClient()`, `computeAuthHash()`, PSK storage, rate limiting
- `bin/eth-ota-upload.py` — `authenticate()`, `--psk`/`--psk-hex` CLI args, `hashlib.sha256`

---

## Resumen de archivos a crear/modificar

| Archivo | Acción |
|---------|--------|
| `src/mesh/eth/ethOTA.h` | **Nuevo** — Servidor TCP OTA |
| `src/mesh/eth/ethOTA.cpp` | **Nuevo** — Implementación protocolo + Updater |
| `bin/eth-ota-upload.py` | **Nuevo** — Script de upload Python |
| `variants/rp2350/wiznet_5500_evb_pico2_e22/platformio.ini` | **Modificar** — `filesystem_size=0.75m` + `HAS_ETHERNET_OTA` |
| `variants/rp2350/pico2_w5500_e22/platformio.ini` | **Modificar** — ídem |
| `src/mesh/eth/ethClient.cpp` | **Modificar** — Inicializar servidor OTA |

**YA NO SE NECESITAN:**
- ~~`src/platform/rp2xx0/RP2350OTA.h/.cpp`~~ — el `Updater` nativo lo reemplaza
- ~~Modificar `AdminModule.cpp`~~ — opcional, no crítico para v1

---

## Orden de implementación

```
Fase 1 (build config)  →  Fase 2 (TCP server + Updater)  →  Fase 3 (integración)
                                                                   ↓
                                                           Fase 4 (script Python)
                                                                   ↓
                                                           Fase 5 (seguridad)
```

---

## Riesgos principales

| Riesgo | Mitigación |
|--------|-----------|
| Firmware GZIP no cabe en LittleFS | Aumentar `filesystem_size` a 0.75m (614KB GZIP < 768KB FS) |
| Corte de energía durante escritura a LittleFS | LittleFS es journaled; archivo parcial se puede borrar y reintentar |
| Corte de energía durante flasheo por bootloader | El bootloader verifica CRC antes de escribir; si falla, no toca el firmware actual |
| SPI0 compartido (W5500 + flash) | El `Updater` escribe a LittleFS (no directamente a flash); el bootloader flashea en cold boot sin Ethernet activo → sin conflicto SPI |
| Código crece y supera 1,280KB con FS=0.75m | Monitorear tamaño en CI; el firmware actual es 989KB con margen de 300KB |

---
---

# REFERENCIA DE IMPLEMENTACIÓN COMPLETA

> Esta sección documenta el código **tal como fue implementado** (no pseudocódigo).
> Última actualización: todas las fases (0–5) completadas.

## Archivos creados/modificados

| Archivo | Tipo | Descripción |
|---------|------|-------------|
| `src/mesh/eth/ethOTA.h` | **Nuevo** | Header del servidor TCP OTA |
| `src/mesh/eth/ethOTA.cpp` | **Nuevo** | Implementación: autenticación + protocolo MOTA + Updater |
| `bin/eth-ota-upload.py` | **Nuevo** | Script Python de upload con auth + progress bar |
| `src/mesh/eth/ethClient.cpp` | **Modificado** | Integración: include + `initEthOTA()` + `ethOTALoop()` |
| `variants/rp2350/wiznet_5500_evb_pico2_e22/platformio.ini` | **Modificado** | `filesystem_size=0.75m` + `-D HAS_ETHERNET_OTA` |
| `variants/rp2350/pico2_w5500_e22/platformio.ini` | **Modificado** | Ídem |

## Build sizes (Fase 5 — final)

| Variant | Flash | RAM |
|---------|-------|-----|
| `wiznet_5500_evb_pico2_e22` (2MB flash) | 77.0% (1,006,724 / 1,306,624 bytes) | 18.1% |
| `pico2_w5500_e22` (4MB flash) | 29.6% (1,006,708 / 3,403,776 bytes) | 18.1% |

---

## Protocolo TCP completo (con autenticación)

Puerto: **4243** (`ETH_OTA_PORT`)

```
[Cliente Python]                       [Servidor RP2350]
    |                                        |
    |  1. TCP connect :4243                  |
    |───────────────────────────────────────→|
    |                                        |
    |           ╔══ AUTENTICACIÓN ══╗        |
    |           ║                    ║        |
    |                 nonce (32 bytes)        | Genera nonce random
    |←───────────────────────────────────────|
    |                                        |
    | SHA256(nonce || PSK) (32 bytes)        |
    |───────────────────────────────────────→| Verifica (constant-time XOR)
    |                                        |
    |         ACK (0x06) o ERR_AUTH (0x07)   |
    |←───────────────────────────────────────|
    |           ║                    ║        |
    |           ╚════════════════════╝        |
    |                                        |
    |           ╔══ TRANSFERENCIA ══╗        |
    |           ║                    ║        |
    |  2. Header MOTA (12 bytes)             |
    |    magic("MOTA") + size(u32) + crc(u32)|
    |───────────────────────────────────────→| Valida magic, size
    |                                        | Update.begin(size)
    |                    ACK (0x06)          |
    |←───────────────────────────────────────|
    |                                        |
    |  3. Firmware .bin.gz (chunks 1KB)      |
    |───────────────────────────────────────→| Update.write(chunk)
    |  ... streaming sin ACK por chunk ...   | crc32Update(chunk)
    |                                        |
    |  4. (último byte enviado)              |
    |───────────────────────────────────────→| Verifica CRC32
    |                                        | Update.end(true)
    |                                        | → picoOTA.commit()
    |               RESULT (1 byte)          |
    |←───────────────────────────────────────| 0x00=OK, otros=error
    |           ║                    ║        |
    |           ╚════════════════════╝        |
    |                                        | rp2040.reboot()
    |                                        | Bootloader aplica update
```

### Códigos de respuesta (enum `OTAResponse`)

| Código | Nombre | Significado |
|--------|--------|-------------|
| `0x00` | `OTA_OK` | Update exitoso, device reiniciando |
| `0x01` | `OTA_ERR_CRC` | CRC32 del firmware no coincide |
| `0x02` | `OTA_ERR_SIZE` | Tamaño inválido (0 o > 1MB) |
| `0x03` | `OTA_ERR_WRITE` | Error de escritura a LittleFS / Update.end() falló |
| `0x04` | `OTA_ERR_MAGIC` | Magic bytes no son "MOTA" |
| `0x05` | `OTA_ERR_BEGIN` | `Update.begin()` falló (sin espacio en LittleFS?) |
| `0x06` | `OTA_ACK` / `OTA_ERR_TIMEOUT` | ACK (contexto auth/header) / Timeout inactividad 30s |
| `0x07` | `OTA_ERR_AUTH` | Autenticación fallida (PSK incorrecta o cooldown activo) |

**Nota:** `OTA_ACK` y `OTA_ERR_TIMEOUT` comparten valor `0x06` — se distinguen por contexto (ACK se envía durante handshake, TIMEOUT durante transferencia).

### Header MOTA (12 bytes, little-endian)

```c
struct __attribute__((packed)) OTAHeader {
    uint8_t magic[4];      // "MOTA" (0x4D 0x4F 0x54 0x41)
    uint32_t firmwareSize; // Bytes del payload GZIP
    uint32_t crc32;        // CRC32 del payload completo
};
```

---

## Autenticación PSK — Detalles

### Algoritmo

1. Servidor genera **nonce aleatorio de 32 bytes** (via `random()` en bloques de 4 bytes)
2. Servidor envía nonce al cliente
3. Cliente calcula `SHA256(nonce || PSK)` y envía los 32 bytes del hash
4. Servidor calcula el mismo hash y compara con **XOR constant-time**
5. Si match → envía `OTA_ACK (0x06)`, si no → envía `OTA_ERR_AUTH (0x07)`

### PSK por defecto

```
Texto:   "meshtastic_ota_default_psk_v1!!!"  (32 bytes UTF-8)
Hex:     6d65736874617374 69635f6f74615f64
         656661756c745f70 736b5f7631212121
```

### Generar PSK personalizado

Se recomienda usar una clave aleatoria de **32 bytes** (256 bits). La PSK puede tener cualquier longitud, pero 32 bytes es ideal para SHA256.

#### Opción 1: Python (recomendado)

```bash
# Genera 32 bytes aleatorios y muestra ambos formatos (hex y C array)
python -c "
import os
key = os.urandom(32)
hex_str = key.hex()
c_array = ', '.join(f'0x{b:02x}' for b in key)
print(f'Hex:      {hex_str}')
print(f'C array:  {{{c_array}}}')
print()
print(f'Para userPrefs.jsonc:')
print(f'  \"OTA_PSK\": \"{{{c_array}}}\"')
print()
print(f'Para eth-ota-upload.py:')
print(f'  --psk-hex {hex_str}')
"
```

Ejemplo de salida:
```
Hex:      a3f1d7e902b84c6f1a5e39d0c7b2f48e6d1a0b5c3e7f9284d6e0a1b3c5f7280e
C array:  {0xa3, 0xf1, 0xd7, 0xe9, 0x02, 0xb8, 0x4c, 0x6f, 0x1a, 0x5e, 0x39, 0xd0, 0xc7, 0xb2, 0xf4, 0x8e, 0x6d, 0x1a, 0x0b, 0x5c, 0x3e, 0x7f, 0x92, 0x84, 0xd6, 0xe0, 0xa1, 0xb3, 0xc5, 0xf7, 0x28, 0x0e}

Para userPrefs.jsonc:
  "OTA_PSK": "{0xa3, 0xf1, 0xd7, ...}"

Para eth-ota-upload.py:
  --psk-hex a3f1d7e902b84c6f...
```

#### Opción 2: OpenSSL (Linux/macOS)

```bash
# Genera 32 bytes aleatorios en hex
openssl rand -hex 32

# Ejemplo salida: a3f1d7e902b84c6f1a5e39d0c7b2f48e6d1a0b5c3e7f9284d6e0a1b3c5f7280e
```

Para convertir el hex a formato C array:
```bash
openssl rand -hex 32 | sed 's/\(..\)/0x\1, /g; s/, $//'
```

#### Opción 3: PowerShell (Windows)

```powershell
# Genera 32 bytes aleatorios
$bytes = New-Object byte[] 32
[System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
$hex = [BitConverter]::ToString($bytes) -replace '-',''
$cArray = ($bytes | ForEach-Object { "0x{0:x2}" -f $_ }) -join ', '

Write-Host "Hex:      $hex"
Write-Host "C array:  {$cArray}"
Write-Host ""
Write-Host "Para userPrefs.jsonc:"
Write-Host "  `"OTA_PSK`": `"{$cArray}`""
Write-Host ""
Write-Host "Para eth-ota-upload.py:"
Write-Host "  --psk-hex $($hex.ToLower())"
```

#### Opción 4: String UTF-8 manual

Si prefieres un string legible (menos seguro que bytes aleatorios):
- Debe tener **exactamente la misma longitud** en firmware y script Python
- Usa caracteres ASCII imprimibles
- Mínimo recomendado: 16 caracteres

```bash
# Usar directamente como string en el script:
python bin/eth-ota-upload.py --host 192.168.1.100 --psk "MiClaveOTA_Secreta2026!!" firmware.bin
```

Para firmware con string UTF-8, convierte a C array:
```bash
python -c "
psk = 'MiClaveOTA_Secreta2026!!'
c_array = ', '.join(f'0x{b:02x}' for b in psk.encode('utf-8'))
print(f'\"OTA_PSK\": \"{{{c_array}}}\"')
"
```

> **Importante**: La PSK del firmware y del script Python **deben ser idénticas** (mismos bytes). Si se usa `--psk` en el script, los bytes son la codificación UTF-8 del string. Si se usa `--psk-hex`, son los bytes directos del hex.

### Configurar PSK personalizado

**En firmware** — editar el archivo `userPrefs.jsonc` en la raíz del proyecto (`firmware/userPrefs.jsonc`):
```jsonc
{
  "OTA_PSK": "{0xa3, 0xf1, 0xd7, 0xe9, ...}"  // Array de bytes C, cualquier longitud
}
```
Esto genera `-DUSERPREFS_OTA_PSK={0xa3,...}` en el build (procesado por `bin/platformio-custom.py`).

> **Nota**: Después de cambiar la PSK, debes recompilar y flashear el firmware por USB/serial (ya que la nueva PSK aún no está en el dispositivo para autenticar un OTA).

**En script Python:**
```bash
# Con hex (misma clave generada arriba):
python bin/eth-ota-upload.py --host 192.168.1.100 --psk-hex a3f1d7e902b84c6f1a5e39d0c7b2f48e6d1a0b5c3e7f9284d6e0a1b3c5f7280e firmware.bin

# Con string UTF-8 (si configuraste un string como PSK):
python bin/eth-ota-upload.py --host 192.168.1.100 --psk "MiClaveOTA_Secreta2026!!" firmware.bin
```

### Rate limiting

- Tras auth fallida: cooldown de **5 segundos** (`OTA_AUTH_COOLDOWN_MS`)
- Durante cooldown, cualquier conexión recibe `OTA_ERR_AUTH` inmediatamente
- Variable: `lastAuthFailure` (timestamp `millis()`)

### Crypto

- **Librería**: `rweather/Crypto@0.4.0` (ya dependencia del proyecto para cifrado mesh)
- **Clase**: `SHA256` con API: `reset()` → `update(data, len)` → `finalize(hash, 32)`
- **No HMAC**: La librería no incluye HMAC; se usa `SHA256(nonce || PSK)` que es seguro para challenge-response (el nonce previene replay attacks)

---

## Integración en ethClient.cpp

Tres bloques de código añadidos, todos protegidos por `#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)`:

### 1. Include (línea ~9)
```cpp
#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)
#include "mesh/eth/ethOTA.h"
#endif
```

### 2. Inicialización (dentro del bloque de Ethernet conectado, después de `initApiServer()` y UDP multicast)
```cpp
#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)
            initEthOTA();
#endif
```

### 3. Loop (al final de la función `reconnectETH()`, antes del `return 5000`)
```cpp
#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)
    ethOTALoop();
#endif
```

`ethOTALoop()` se ejecuta cada **5 segundos** (período de `reconnectETH()`). Es non-blocking: solo llama `otaServer->accept()` y si hay cliente, procesa el OTA completo de forma síncrona (bloquea el loop ~30-60s durante transferencia).

---

## Cambios en platformio.ini (ambos variants)

Cambios idénticos en:
- `variants/rp2350/wiznet_5500_evb_pico2_e22/platformio.ini`
- `variants/rp2350/pico2_w5500_e22/platformio.ini`

### 1. Filesystem size (línea 8)
```ini
# Increase LittleFS from 0.5m to 0.75m so GZIP firmware (~614KB) fits for OTA staging
board_build.filesystem_size = 0.75m
```
**Antes:** `0.5m` (512KB) — firmware GZIP 614KB no cabía.
**Después:** `0.75m` (768KB) — margen de ~154KB.

### 2. Build flag (línea 19)
```ini
  -D HAS_ETHERNET_OTA
```
Habilita la compilación condicional de todo el código OTA.

---

## Script Python: Instrucciones de uso

### Requisitos
- Python 3.6+ (usa f-strings, `hashlib`, `gzip`, `struct`)
- No requiere dependencias externas (solo stdlib)

### Uso básico (PSK por defecto)
```bash
python bin/eth-ota-upload.py --host 192.168.1.100 firmware.bin
```

### Con PSK personalizado
```bash
# String UTF-8:
python bin/eth-ota-upload.py --host 192.168.1.100 --psk "mi_clave" firmware.bin

# Hex:
python bin/eth-ota-upload.py --host 192.168.1.100 --psk-hex 6d69636c617665 firmware.bin
```

### Con puerto y timeout personalizados
```bash
python bin/eth-ota-upload.py --host 192.168.1.100 --port 4243 --timeout 120 firmware.bin
```

### Opciones completas
```
positional arguments:
  firmware              Path to firmware .bin or .bin.gz file

options:
  --host HOST           Device IP address (required)
  --port PORT           OTA port (default: 4243)
  --timeout TIMEOUT     Socket timeout in seconds (default: 60)
  --psk PSK             Pre-shared key as UTF-8 string
  --psk-hex PSK_HEX     Pre-shared key as hex string
```

### Flujo del script
1. Lee el archivo `.bin` (o `.bin.gz`)
2. Si no está comprimido, aplica GZIP nivel 9
3. Calcula CRC32 del payload comprimido
4. Conecta TCP al dispositivo
5. **Autenticación**: recibe nonce → envía `SHA256(nonce || PSK)` → espera ACK
6. Envía header MOTA (12 bytes) → espera ACK
7. Envía firmware en chunks de 1KB con barra de progreso
8. Espera resultado final (OK / error)

### Ejemplo de salida exitosa
```
Meshtastic Ethernet OTA Upload
========================================
Firmware raw size: 1,012,772 bytes
GZIP compressed: 628,601 bytes (62.1%)
Connecting to 192.168.1.100:4243...
Connected.
Authenticating...
Authentication successful.
Header sent: size=628,601, CRC32=0xABCD1234
Header accepted. Uploading firmware...
  [████████████████████████████████████████] 100% 628,601/628,601 (85.3 KB/s)
  Transfer complete in 7.2s
Waiting for verification...
SUCCESS: OK — Update staged, device rebooting

Device is rebooting with new firmware.
```

---

## Dependencias

| Dependencia | Versión | Uso | Ya existía |
|-------------|---------|-----|-----------|
| `arduino-libraries/Ethernet` | `^2.0.2` | `EthernetServer`, `EthernetClient` | ✅ (lib_deps de ambos variants) |
| `rweather/Crypto` | `0.4.0` | `SHA256` para autenticación challenge-response | ✅ (dependencia global del proyecto) |
| `ErriezCRC32` | — | `crc32Update()`, `crc32Final()` para verificar firmware | ✅ (dependencia global) |
| `Updater.h` | (arduino-pico core) | `Update.begin()`, `.write()`, `.end()` para staging OTA | ✅ (parte del framework) |

**No se añadió ninguna dependencia nueva.**

---

## Código fuente completo

### ethOTA.h

```cpp
#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#ifdef WIZNET_5500_EVB_PICO2
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

#define ETH_OTA_PORT 4243

/// Initialize the Ethernet OTA server (call after Ethernet is connected)
void initEthOTA();

/// Poll for incoming OTA connections (call periodically from ethClient reconnect loop)
void ethOTALoop();

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
```

### ethOTA.cpp (`src/mesh/eth/ethOTA.cpp`)

```cpp
#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#include "ethOTA.h"
#include <ErriezCRC32.h>
#include <SHA256.h>
#include <Updater.h>

/// Protocol header sent by the upload tool
struct __attribute__((packed)) OTAHeader {
    uint8_t magic[4];      // "MOTA" (Meshtastic OTA)
    uint32_t firmwareSize; // Size of the firmware payload in bytes (little-endian)
    uint32_t crc32;        // CRC32 of the entire firmware payload
};

/// Response codes sent back to the client
enum OTAResponse : uint8_t {
    OTA_OK = 0x00,
    OTA_ERR_CRC = 0x01,
    OTA_ERR_SIZE = 0x02,
    OTA_ERR_WRITE = 0x03,
    OTA_ERR_MAGIC = 0x04,
    OTA_ERR_BEGIN = 0x05,
    OTA_ERR_TIMEOUT = 0x06,
    OTA_ACK = 0x06, // ACK uses ASCII ACK character
    OTA_ERR_AUTH = 0x07,
};

static const uint32_t OTA_TIMEOUT_MS = 30000;       // 30s inactivity timeout
static const size_t OTA_CHUNK_SIZE = 1024;           // 1KB receive buffer
static const uint32_t OTA_AUTH_COOLDOWN_MS = 5000;   // 5s cooldown after failed auth
static const size_t OTA_NONCE_SIZE = 32;
static const size_t OTA_HASH_SIZE = 32;

// OTA PSK — override via USERPREFS_OTA_PSK in userPrefs.jsonc
#ifdef USERPREFS_OTA_PSK
static const uint8_t otaPSK[] = USERPREFS_OTA_PSK;
#else
// Default PSK (CHANGE THIS for production deployments)
static const uint8_t otaPSK[] = {0x6d, 0x65, 0x73, 0x68, 0x74, 0x61, 0x73, 0x74, 0x69, 0x63, 0x5f,
                                  0x6f, 0x74, 0x61, 0x5f, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74,
                                  0x5f, 0x70, 0x73, 0x6b, 0x5f, 0x76, 0x31, 0x21, 0x21, 0x21};
// = "meshtastic_ota_default_psk_v1!!!"
#endif
static const size_t otaPSKSize = sizeof(otaPSK);

static EthernetServer *otaServer = nullptr;
static uint32_t lastAuthFailure = 0;

static bool readExact(EthernetClient &client, uint8_t *buf, size_t len)
{
    size_t received = 0;
    uint32_t lastActivity = millis();

    while (received < len) {
        if (!client.connected()) {
            return false;
        }
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = min((size_t)avail, len - received);
            size_t got = client.read(buf + received, toRead);
            received += got;
            lastActivity = millis();
        } else {
            if (millis() - lastActivity > OTA_TIMEOUT_MS) {
                return false;
            }
            delay(1);
        }
    }
    return true;
}

/// Compute SHA256(nonce || psk) for challenge-response authentication
static void computeAuthHash(const uint8_t *nonce, size_t nonceLen, const uint8_t *psk, size_t pskLen, uint8_t *hashOut)
{
    SHA256 sha;
    sha.reset();
    sha.update(nonce, nonceLen);
    sha.update(psk, pskLen);
    sha.finalize(hashOut, OTA_HASH_SIZE);
}

/// Challenge-response authentication. Returns true if client is authenticated.
static bool authenticateClient(EthernetClient &client)
{
    // Rate-limit after failed auth
    if (lastAuthFailure != 0 && (millis() - lastAuthFailure) < OTA_AUTH_COOLDOWN_MS) {
        LOG_WARN("ETH OTA: Auth cooldown active, rejecting connection");
        client.write(OTA_ERR_AUTH);
        return false;
    }

    // Generate random nonce
    uint8_t nonce[OTA_NONCE_SIZE];
    for (size_t i = 0; i < OTA_NONCE_SIZE; i += 4) {
        uint32_t r = random();
        size_t remaining = OTA_NONCE_SIZE - i;
        memcpy(nonce + i, &r, min((size_t)4, remaining));
    }

    // Send nonce to client
    client.write(nonce, OTA_NONCE_SIZE);

    // Read client's response: SHA256(nonce || PSK)
    uint8_t clientHash[OTA_HASH_SIZE];
    if (!readExact(client, clientHash, OTA_HASH_SIZE)) {
        LOG_WARN("ETH OTA: Timeout reading auth response");
        lastAuthFailure = millis();
        return false;
    }

    // Compute expected hash
    uint8_t expectedHash[OTA_HASH_SIZE];
    computeAuthHash(nonce, OTA_NONCE_SIZE, otaPSK, otaPSKSize, expectedHash);

    // Constant-time comparison to prevent timing attacks
    uint8_t diff = 0;
    for (size_t i = 0; i < OTA_HASH_SIZE; i++) {
        diff |= clientHash[i] ^ expectedHash[i];
    }

    if (diff != 0) {
        LOG_WARN("ETH OTA: Authentication failed");
        client.write(OTA_ERR_AUTH);
        lastAuthFailure = millis();
        return false;
    }

    // Auth success — send ACK
    client.write(OTA_ACK);
    LOG_INFO("ETH OTA: Authentication successful");
    return true;
}

static void handleOTAClient(EthernetClient &client)
{
    LOG_INFO("ETH OTA: Client connected from %u.%u.%u.%u", client.remoteIP()[0], client.remoteIP()[1],
             client.remoteIP()[2], client.remoteIP()[3]);

    // Step 1: Challenge-response authentication
    if (!authenticateClient(client)) {
        return;
    }

    // Step 2: Read 12-byte header
    OTAHeader hdr;
    if (!readExact(client, (uint8_t *)&hdr, sizeof(hdr))) {
        LOG_WARN("ETH OTA: Timeout reading header");
        return;
    }

    // Validate magic
    if (memcmp(hdr.magic, "MOTA", 4) != 0) {
        LOG_WARN("ETH OTA: Invalid magic");
        client.write(OTA_ERR_MAGIC);
        return;
    }

    LOG_INFO("ETH OTA: Firmware size=%u, CRC32=0x%08X", hdr.firmwareSize, hdr.crc32);

    // Sanity check on size (must be > 0 and fit in LittleFS)
    if (hdr.firmwareSize == 0 || hdr.firmwareSize > 1024 * 1024) {
        LOG_WARN("ETH OTA: Invalid firmware size");
        client.write(OTA_ERR_SIZE);
        return;
    }

    // Begin the update — this opens firmware.bin on LittleFS
    if (!Update.begin(hdr.firmwareSize)) {
        LOG_ERROR("ETH OTA: Update.begin() failed, error=%u", Update.getError());
        client.write(OTA_ERR_BEGIN);
        return;
    }

    // ACK the header — client can start sending firmware data
    client.write(OTA_ACK);

    // Receive firmware in chunks
    uint8_t buf[OTA_CHUNK_SIZE];
    size_t remaining = hdr.firmwareSize;
    uint32_t crc = CRC32_INITIAL;
    uint32_t lastActivity = millis();
    size_t totalReceived = 0;

    while (remaining > 0) {
        if (!client.connected()) {
            LOG_WARN("ETH OTA: Client disconnected during transfer");
            Update.end(false);
            return;
        }

        int avail = client.available();
        if (avail <= 0) {
            if (millis() - lastActivity > OTA_TIMEOUT_MS) {
                LOG_WARN("ETH OTA: Timeout during transfer (%u/%u bytes)", totalReceived, hdr.firmwareSize);
                client.write(OTA_ERR_TIMEOUT);
                Update.end(false);
                return;
            }
            delay(1);
            continue;
        }

        size_t toRead = min((size_t)avail, min(remaining, sizeof(buf)));
        size_t got = client.read(buf, toRead);
        if (got == 0)
            continue;

        // Write to Updater (LittleFS firmware.bin)
        size_t written = Update.write(buf, got);
        if (written != got) {
            LOG_ERROR("ETH OTA: Write failed (wrote %u of %u), error=%u", written, got, Update.getError());
            client.write(OTA_ERR_WRITE);
            Update.end(false);
            return;
        }

        crc = crc32Update(buf, got, crc);
        remaining -= got;
        totalReceived += got;
        lastActivity = millis();

        // Progress log every ~10%
        if (totalReceived % (hdr.firmwareSize / 10 + 1) < got) {
            LOG_INFO("ETH OTA: %u%% (%u/%u bytes)", (uint32_t)(100ULL * totalReceived / hdr.firmwareSize), totalReceived,
                     hdr.firmwareSize);
        }
    }

    // Verify CRC32
    uint32_t computedCRC = crc32Final(crc);
    if (computedCRC != hdr.crc32) {
        LOG_ERROR("ETH OTA: CRC mismatch (expected=0x%08X, computed=0x%08X)", hdr.crc32, computedCRC);
        client.write(OTA_ERR_CRC);
        Update.end(false);
        return;
    }

    // Finalize — this calls picoOTA.commit() which stages the update for the bootloader
    if (!Update.end(true)) {
        LOG_ERROR("ETH OTA: Update.end() failed, error=%u", Update.getError());
        client.write(OTA_ERR_WRITE);
        return;
    }

    LOG_INFO("ETH OTA: Update staged successfully (%u bytes). Rebooting...", hdr.firmwareSize);
    client.write(OTA_OK);
    client.flush();
    delay(500);

    // Reboot — the built-in bootloader will apply the update from LittleFS
    rp2040.reboot();
}

void initEthOTA()
{
    if (!otaServer) {
        otaServer = new EthernetServer(ETH_OTA_PORT);
        otaServer->begin();
        LOG_INFO("ETH OTA: Server listening on TCP port %d", ETH_OTA_PORT);
    }
}

void ethOTALoop()
{
    if (!otaServer)
        return;

    EthernetClient client = otaServer->accept();
    if (client) {
        handleOTAClient(client);
        client.stop();
    }
}

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
```

### eth-ota-upload.py (`bin/eth-ota-upload.py`)

```python
#!/usr/bin/env python3
"""
Meshtastic Ethernet OTA Upload Tool

Uploads firmware to RP2350-based Meshtastic devices via Ethernet (W5500).
Compresses firmware with GZIP and sends it over TCP using the MOTA protocol.
Authenticates using SHA256 challenge-response with a pre-shared key (PSK).

Usage:
    python bin/eth-ota-upload.py --host 192.168.1.100 firmware.bin
    python bin/eth-ota-upload.py --host 192.168.1.100 --psk mySecretKey firmware.bin
    python bin/eth-ota-upload.py --host 192.168.1.100 --psk-hex 6d65736874... firmware.bin
"""

import argparse
import gzip
import hashlib
import socket
import struct
import sys
import time

# Default PSK matching the firmware default: "meshtastic_ota_default_psk_v1!!!"
DEFAULT_PSK = b"meshtastic_ota_default_psk_v1!!!"


def crc32(data: bytes) -> int:
    """Compute CRC32 matching ErriezCRC32 (standard CRC32 with final XOR)."""
    import binascii

    return binascii.crc32(data) & 0xFFFFFFFF


def load_firmware(path: str) -> bytes:
    """Load firmware file, compressing with GZIP if not already compressed."""
    # Reject UF2 files — OTA requires raw .bin firmware
    if path.lower().endswith(".uf2"):
        bin_path = path.rsplit(".", 1)[0] + ".bin"
        print(f"ERROR: UF2 files cannot be used for OTA updates.")
        print(f"       The Updater/picoOTA expects raw .bin firmware.")
        print(f"       Try: {bin_path}")
        sys.exit(1)

    with open(path, "rb") as f:
        data = f.read()

    # Check if already GZIP compressed (magic bytes 1f 8b)
    if data[:2] == b"\x1f\x8b":
        print(f"Firmware already GZIP compressed: {len(data):,} bytes")
        return data

    print(f"Firmware raw size: {len(data):,} bytes")
    compressed = gzip.compress(data, compresslevel=9)
    ratio = len(compressed) / len(data) * 100
    print(f"GZIP compressed: {len(compressed):,} bytes ({ratio:.1f}%)")
    return compressed


def authenticate(sock: socket.socket, psk: bytes) -> bool:
    """Perform SHA256 challenge-response authentication with the device."""
    # Receive 32-byte nonce from server
    nonce = b""
    while len(nonce) < 32:
        chunk = sock.recv(32 - len(nonce))
        if not chunk:
            print("ERROR: Connection closed during authentication")
            return False
        nonce += chunk

    # Compute SHA256(nonce || PSK)
    h = hashlib.sha256()
    h.update(nonce)
    h.update(psk)
    response = h.digest()

    # Send 32-byte response
    sock.sendall(response)

    # Wait for auth result (1 byte)
    result = sock.recv(1)
    if not result:
        print("ERROR: No authentication response")
        return False

    if result[0] == 0x06:  # ACK
        print("Authentication successful.")
        return True
    elif result[0] == 0x07:  # OTA_ERR_AUTH
        print("ERROR: Authentication failed — wrong PSK")
        return False
    else:
        print(f"ERROR: Unexpected auth response 0x{result[0]:02X}")
        return False


def upload_firmware(host: str, port: int, firmware: bytes, psk: bytes, timeout: float) -> bool:
    """Upload firmware over TCP using the MOTA protocol with PSK authentication."""
    fw_crc = crc32(firmware)
    fw_size = len(firmware)

    print(f"Connecting to {host}:{port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)

    try:
        sock.connect((host, port))
        print("Connected.")

        # Step 1: Authenticate
        print("Authenticating...")
        if not authenticate(sock, psk):
            return False

        # Step 2: Send 12-byte MOTA header: magic(4) + size(4) + crc32(4)
        header = struct.pack("<4sII", b"MOTA", fw_size, fw_crc)
        sock.sendall(header)
        print(f"Header sent: size={fw_size:,}, CRC32=0x{fw_crc:08X}")

        # Wait for ACK (1 byte)
        ack = sock.recv(1)
        if not ack or ack[0] != 0x06:
            error_codes = {
                0x02: "Size error",
                0x04: "Invalid magic",
                0x05: "Update.begin() failed",
            }
            code = ack[0] if ack else 0xFF
            msg = error_codes.get(code, f"Unknown error 0x{code:02X}")
            print(f"ERROR: Server rejected header: {msg}")
            return False

        print("Header accepted. Uploading firmware...")

        # Send firmware in 1KB chunks
        chunk_size = 1024
        sent = 0
        start_time = time.time()

        while sent < fw_size:
            end = min(sent + chunk_size, fw_size)
            chunk = firmware[sent:end]
            sock.sendall(chunk)
            sent = end

            # Progress bar
            pct = sent * 100 // fw_size
            bar_len = 40
            filled = bar_len * sent // fw_size
            bar = "█" * filled + "░" * (bar_len - filled)
            elapsed = time.time() - start_time
            speed = sent / elapsed if elapsed > 0 else 0
            sys.stdout.write(f"\r  [{bar}] {pct:3d}% {sent:,}/{fw_size:,} ({speed/1024:.1f} KB/s)")
            sys.stdout.flush()

        elapsed = time.time() - start_time
        print(f"\n  Transfer complete in {elapsed:.1f}s")

        # Wait for final result (1 byte)
        print("Waiting for verification...")
        result = sock.recv(1)
        if not result:
            print("ERROR: No response from device")
            return False

        result_codes = {
            0x00: "OK — Update staged, device rebooting",
            0x01: "CRC mismatch",
            0x02: "Size error",
            0x03: "Write error",
            0x06: "Timeout",
        }
        code = result[0]
        msg = result_codes.get(code, f"Unknown result 0x{code:02X}")

        if code == 0x00:
            print(f"SUCCESS: {msg}")
            return True
        else:
            print(f"ERROR: {msg}")
            return False

    except socket.timeout:
        print("ERROR: Connection timed out")
        return False
    except ConnectionRefusedError:
        print(f"ERROR: Connection refused by {host}:{port}")
        return False
    except OSError as e:
        print(f"ERROR: {e}")
        return False
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(
        description="Upload firmware to Meshtastic RP2350 devices via Ethernet OTA"
    )
    parser.add_argument("firmware", help="Path to firmware .bin or .bin.gz file")
    parser.add_argument("--host", required=True, help="Device IP address")
    parser.add_argument(
        "--port", type=int, default=4243, help="OTA port (default: 4243)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Socket timeout in seconds (default: 60)",
    )
    psk_group = parser.add_mutually_exclusive_group()
    psk_group.add_argument(
        "--psk",
        type=str,
        help="Pre-shared key as UTF-8 string (default: meshtastic_ota_default_psk_v1!!!)",
    )
    psk_group.add_argument(
        "--psk-hex",
        type=str,
        help="Pre-shared key as hex string (e.g., 6d65736874...)",
    )
    args = parser.parse_args()

    # Resolve PSK
    if args.psk:
        psk = args.psk.encode("utf-8")
    elif args.psk_hex:
        try:
            psk = bytes.fromhex(args.psk_hex)
        except ValueError:
            print("ERROR: Invalid hex string for --psk-hex")
            sys.exit(1)
    else:
        psk = DEFAULT_PSK

    print("Meshtastic Ethernet OTA Upload")
    print("=" * 40)

    firmware = load_firmware(args.firmware)

    if upload_firmware(args.host, args.port, firmware, psk, args.timeout):
        print("\nDevice is rebooting with new firmware.")
        sys.exit(0)
    else:
        print("\nUpload failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
```
