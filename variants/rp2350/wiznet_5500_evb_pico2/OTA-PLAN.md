# Plan de implementación: OTA via Ethernet para RP2350

## Contexto

| Aspecto | ESP32 (actual) | RP2350 (actual) | RP2350 (objetivo) |
|---------|----------------|-----------------|-------------------|
| OTA | WiFi/BLE via loader en ota_1 | Solo USB BOOTSEL | **Ethernet TCP** |
| Flash | Particiones dual (ota_0 + ota_1) | Flat (app + LittleFS) | Flat con zona de staging |
| Red | WiFi nativa | W5500 Ethernet (SPI0) | W5500 Ethernet |

## Por qué NO usar picowota directamente

1. **WiFi-only** — picowota depende del chip CYW43 (Pico W). Nuestras boards no tienen WiFi, tienen W5500 Ethernet.
2. **Build system incompatible** — picowota usa CMake + Pico SDK nativo. Meshtastic usa PlatformIO + arduino-pico (earlephilhower).
3. **Bootloader duplica stack de red** — picowota incluye un stack WiFi completo (~300-400KB) en el bootloader. Con Ethernet sería menor, pero sigue siendo desperdicio de flash.

## Enfoque: OTA in-app via TCP

El firmware Meshtastic **recibe el binario directamente** a través de un servidor TCP sobre la conexión Ethernet existente, lo valida, y lo escribe en flash. Es el enfoque más limpio y compatible con la arquitectura actual.

---

## Fase 0: Investigación y preparación

| Tarea | Detalle |
|-------|---------|
| 0.1 | Estudiar la API flash del RP2350: `flash_range_erase()`, `flash_range_program()` del ROM bootloader (hardware/flash.h en pico-sdk). Requiere deshabilitar interrupciones y ejecutar desde RAM. |
| 0.2 | Determinar el mapa de memoria flash exacto con arduino-pico + `filesystem_size = 0.5m`. Típicamente: 0x10000000–0x103FFFFF (4MB), LittleFS en los últimos 512KB. |
| 0.3 | Decidir formato del binario OTA: `.bin` crudo (más simple) o `.uf2` (tiene CRC por bloque, más robusto). Recomendación: **usar .bin con CRC32/SHA256 header**. |

---

## Fase 1: Módulo de escritura flash segura (low level)

**Nuevo archivo:** `src/platform/rp2xx0/RP2350OTA.h` / `.cpp`

| Tarea | Detalle |
|-------|---------|
| 1.1 | Implementar `RP2350OTA::begin(size_t firmwareSize)` — valida que el firmware cabe en flash (excluyendo LittleFS), calcula dirección de staging. |
| 1.2 | Implementar `RP2350OTA::writeChunk(const uint8_t* data, size_t len, size_t offset)` — escribe bloques de 4KB (tamaño de sector). Usa `flash_range_erase()` + `flash_range_program()` **con interrupciones deshabilitadas y SPI1 (LoRa) pausado**. |
| 1.3 | Implementar `RP2350OTA::verify(uint32_t expectedCRC)` — lee el firmware escrito y verifica CRC32. |
| 1.4 | Implementar `RP2350OTA::activate()` — si la verificación pasa, copia el firmware de la zona staging a la zona de app (o escribe directamente sobre la app actual si usamos flash lineal). Reinicia con `watchdog_reboot()`. |

### Decisión de arquitectura flash (dos opciones)

```
Opción A: Staging area (más seguro, requiere 2x espacio)
┌─────────────────┬──────────────────┬──────────┐
│  App actual      │  Staging (OTA)   │ LittleFS │
│  0x10000000      │  ~mitad flash    │ últimos  │
│  ~1.5 MB         │  ~1.5 MB         │ 512 KB   │
└─────────────────┴──────────────────┴──────────┘

Opción B: Escritura directa (menor seguridad, usa todo el flash)
┌────────────────────────────────┬──────────┐
│  App (se sobreescribe in-place) │ LittleFS │
│  0x10000000                     │ últimos  │
│  ~3.5 MB                        │ 512 KB   │
└────────────────────────────────┴──────────┘
```

**Recomendación: Opción A** — Si el OTA falla a mitad de escritura, la app actual sigue intacta. Solo se activa tras verificar CRC. Requiere que el firmware quepa en ~1.5MB (actualmente usa ~962KB, así que cabe bien).

---

## Fase 2: Servidor TCP para recepción OTA

**Nuevo archivo:** `src/mesh/eth/ethOTA.h` / `.cpp`

| Tarea | Detalle |
|-------|---------|
| 2.1 | Crear servidor TCP en un puerto dedicado (ej. **4243**, similar al 4242 de picowota). Usa la librería Ethernet ya integrada (`EthernetServer`). |
| 2.2 | Definir protocolo simple de transferencia (ver abajo). |
| 2.3 | Recibir chunks y pasarlos a `RP2350OTA::writeChunk()`. |
| 2.4 | Al completar, verificar CRC, responder al cliente, y si es válido llamar a `RP2350OTA::activate()`. |
| 2.5 | Timeout de inactividad (ej. 30s sin datos → abortar). |

### Protocolo propuesto (inspirado en picowota/serial-flash)

```
Cliente → Servidor:
  [4 bytes] Magic: "MOTA" (Meshtastic OTA)
  [4 bytes] Firmware size (little-endian)
  [4 bytes] CRC32 del firmware
  [N bytes] Firmware binary data (chunks de 1KB)

Servidor → Cliente:
  [1 byte] ACK (0x06) después de cada chunk recibido
  [1 byte] resultado final: 0x00=OK, 0x01=CRC fail, 0x02=size error
```

---

## Fase 3: Integración con AdminModule

| Tarea | Detalle |
|-------|---------|
| 3.1 | Añadir manejo de `AdminMessage.reboot_ota_seconds` o nuevo campo protobuf para iniciar modo OTA via Ethernet. Alternativa: usar un comando admin nuevo `enter_ota_mode`. |
| 3.2 | En AdminModule, si el dispositivo es RP2350 con Ethernet, habilitar el servidor TCP OTA y opcionalmente reiniciar en modo "solo OTA" (sin LoRa activo, para liberar SPI1 y evitar conflictos con flash). |
| 3.3 | Opcionalmente: permitir iniciar OTA directamente sin reinicio, solo pausando LoRa temporalmente durante escritura flash. |

---

## Fase 4: Herramienta de upload (script Python)

**Nuevo archivo:** `bin/eth-ota-upload.py`

| Tarea | Detalle |
|-------|---------|
| 4.1 | Script Python que toma IP + puerto + archivo `.bin` y ejecuta el protocolo de upload. |
| 4.2 | Calcula CRC32, envía header, streams chunks, espera ACKs. |
| 4.3 | Barra de progreso y verificación final. |

```bash
# Uso:
python bin/eth-ota-upload.py --host 192.168.1.100 --port 4243 firmware.bin
```

---

## Fase 5: Build system

| Tarea | Detalle |
|-------|---------|
| 5.1 | Añadir build flag `-D HAS_ETHERNET_OTA` en `platformio.ini` de las variantes con Ethernet. |
| 5.2 | Generar `.bin` además de `.uf2` en el build (PlatformIO ya genera ambos). |
| 5.3 | Asegurar que el linker script coloca la app en la primera mitad de flash, dejando espacio para staging. Esto requiere ajustar `board_build.filesystem_size` o añadir un custom linker section. |

---

## Fase 6: Seguridad y robustez

| Tarea | Detalle |
|-------|---------|
| 6.1 | **Autenticación**: Requerir un token/PSK para iniciar OTA (evitar que cualquiera en la red flashee el dispositivo). Puede reutilizar el admin key de Meshtastic. |
| 6.2 | **Anti-brick**: Si el firmware nuevo no arranca (watchdog timeout), revertir automáticamente al anterior. Esto requiere un mini-bootloader o flag en flash. |
| 6.3 | **Validación de firmware**: Verificar que el binario es un firmware Meshtastic válido (magic bytes del header RP2350). |
| 6.4 | **Rate limiting**: Solo permitir una conexión OTA a la vez. |

---

## Resumen de archivos a crear/modificar

| Archivo | Acción |
|---------|--------|
| `src/platform/rp2xx0/RP2350OTA.h` | **Nuevo** — API de escritura flash |
| `src/platform/rp2xx0/RP2350OTA.cpp` | **Nuevo** — Implementación flash write/verify |
| `src/mesh/eth/ethOTA.h` | **Nuevo** — Servidor TCP OTA |
| `src/mesh/eth/ethOTA.cpp` | **Nuevo** — Implementación protocolo OTA |
| `bin/eth-ota-upload.py` | **Nuevo** — Script de upload |
| `src/modules/AdminModule.cpp` | **Modificar** — Handler para iniciar OTA Ethernet |
| `variants/rp2350/wiznet_5500_evb_pico2/platformio.ini` | **Modificar** — Flag HAS_ETHERNET_OTA |
| `variants/rp2350/pico2_w5500_e22/platformio.ini` | **Modificar** — Flag HAS_ETHERNET_OTA |

---

## Orden de implementación recomendado

```
Fase 0 (investigación)  →  Fase 1 (flash write)  →  Fase 2 (TCP server)
         ↓
    Fase 4 (script)  →  Fase 3 (admin integration)  →  Fase 5 (build)
                                                            ↓
                                                      Fase 6 (seguridad)
```

---

## Riesgos principales

| Riesgo | Mitigación |
|--------|-----------|
| `flash_range_erase` requiere ejecutar desde RAM con interrupciones off | arduino-pico ya maneja esto internamente; usar su API `rp2040.idleOtherCore()` + `noInterrupts()` |
| Corte de energía durante escritura = brick | Staging area (Opción A) + CRC validation antes de activar |
| SPI0 (W5500) comparte bus con flash en EVB-Pico2 | Las funciones flash del ROM de RP2350 manejan el XIP cache. Hay que pausar tráfico Ethernet durante escritura de cada sector. |
| Tamaño limitado de flash (4MB con 2MB en algunas boards) | Verificar tamaño de firmware vs espacio staging antes de empezar |
