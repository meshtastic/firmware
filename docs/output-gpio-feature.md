# Output GPIO controlado por parámetro de configuración

## Descripción

Esta feature añade un parámetro persistente (`output_gpio_enabled`) al `DeviceConfig` de Meshtastic que permite activar o desactivar un GPIO de salida **de forma remota** a través del mesh, con el estado guardado en flash.

El número de pin queda fijo en el firmware de cada placa (`#define OUTPUT_GPIO_PIN`). Solo el estado ON/OFF es configurable en tiempo de ejecución.

**Comportamiento de reboot:**
- **OFF → ON:** el nodo guarda el estado y **reinicia automáticamente** (~7 segundos). El GPIO **solo se activa tras el reboot**, en `GpioOutputModule::apply()` durante el arranque. Esto evita ciclos de alimentación innecesarios en el co-procesador (especialmente crítico si se conecta directamente al pin CHIP_PU/EN del ESP32).
- **ON → OFF:** el GPIO se desactiva inmediatamente, **sin reboot**.

---

## Caso de uso principal — Nodo OTA remoto

Diseñado para nodos ubicados en lugares de difícil acceso (montañas, torres, edificios) que normalmente **no tienen WiFi/BT/Ethernet**. Un co-procesador ESP32 (C5-B) permanece apagado la mayor parte del tiempo. Cuando se necesita actualizar el firmware del nodo remotamente:

1. El técnico sube el nuevo firmware a un ESP32 auxiliar (C5-A) desde el móvil
2. Desde cualquier nodo del mesh se envía `output_gpio_enabled = true` al nodo remoto
3. El nRF52 guarda el config y reinicia; al arrancar activa el GPIO → enciende el C5-B → lo detecta por I2C
4. El `OtaRequestModule` del nRF52 orquesta la descarga (C5-B ↔ C5-A por WiFi) y la transferencia del firmware al nRF52 mediante BLE DFU (Nordic Secure DFU)
5. El nRF52 arranca con el nuevo firmware; C5-B desactiva el GPIO y se apaga solo

Ver sección **Sistema OTA completo** más abajo para el flujo detallado.

Otros casos de uso:
- Alimentar/cortar energía a un relé, LED o cualquier periférico externo
- Activar un módulo de sensores de alto consumo solo cuando se necesita
- Control remoto de cualquier carga de baja potencia a través del mesh

---

## Arquitectura del firmware nRF52

```
App Meshtastic / CLI
        │
        │  AdminMessage (set_config → DeviceConfig)
        │  (canal admin, cifrado con clave de administración)
        ▼
┌─────────────────────┐
│   AdminModule       │  ← recibe el mensaje (remoto o local)
│   handleSetConfig() │  ← guarda config en flash
│                     │  ← OFF→ON: guarda config + reboot (GPIO solo tras boot)
│                     │  ← ON→OFF: desactiva GPIO, sin reboot
└─────────┬───────────┘
          │  aplica GPIO
          ▼
┌─────────────────────┐
│  GpioOutputModule   │  ← OSThread, apply() → digitalWrite(OUTPUT_GPIO_PIN)
└─────────────────────┘
          │
          ▼  (tras reboot, si GPIO quedó en HIGH)
┌─────────────────────┐
│  I2C scan           │  ← ScanI2CTwoWire detecta ESP32 en 0x42
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│  OtaRequestModule   │  ← ScanI2CConsumer + OSThread
│  i2cScanFinished()  │  ← detecta ESP32_OTA_COPROCESSOR
│  Máquina de estados │  ← orquesta descarga + BLE DFU + post-OTA
└─────────────────────┘
```

### Por qué no usa RemoteHardwareModule

| Aspecto | `output_gpio_enabled` (esta feature) | RemoteHardwareModule |
|---|---|---|
| Persistencia | Sí, sobrevive reboot | No |
| Seguridad | Canal admin (clave admin) | Solo cifrado de canal |
| Interfaz | `--set device.output_gpio_enabled` | `HardwareMessage` |
| Complejidad | Un bool en DeviceConfig | Máscara de bits, múltiples pines |
| Ideal para | Un pin de salida fijo y persistente | Control dinámico de múltiples pines |

---

## Archivos modificados / creados

### Firmware nRF52 (Meshtastic)

| Archivo | Tipo | Descripción |
|---|---|---|
| `protobufs/meshtastic/config.proto` | Modificado | Campo `bool output_gpio_enabled = 14` en `DeviceConfig` |
| `src/mesh/generated/meshtastic/config.pb.h` | Modificado | Struct, init macros, tag define y FIELDLIST del campo nuevo |
| `src/modules/GpioOutputModule.h/.cpp` | **Nuevo** | Aplica el GPIO en init y por llamada desde AdminModule |
| `src/modules/OtaRequestModule.h/.cpp` | **Nuevo** | Orquesta el flujo OTA: I2C → WiFi download → BLE DFU |
| `src/modules/AdminModule.cpp` | Modificado | Condición no-reboot en ON→OFF + llamada a `apply()` |
| `src/modules/Modules.cpp` | Modificado | Instancia `GpioOutputModule` + `OtaRequestModule` si `OUTPUT_GPIO_PIN` está definido |
| `src/detect/ScanI2C.h` | Modificado | `ESP32_OTA_COPROCESSOR` añadido al enum `DeviceType` |
| `src/detect/ScanI2CTwoWire.cpp` | Modificado | Case `0x42` para detectar el co-procesador (compilado solo con `OUTPUT_GPIO_PIN`) |
| `scripts/patch_meshtastic_proto.py` | **Nuevo** | Parchea el paquete Python instalado para añadir el campo |
| `docs/output-gpio-feature.md` | **Nuevo** | Este documento |

### Firmware ESP32 co-procesador

| Directorio | Descripción |
|---|---|
| `esp32_ota_coprocessor/` | **C5-B** — I2C slave + WiFi downloader + BLE DFU client |
| `esp32_ota_coprocessor_A/` | **C5-A** — SoftAP + web UI para subir el .zip + servidor HTTP |

---

## Cómo configurar una placa

### Opción A — Build flag en `platformio.ini` *(recomendado)*

Añadir `-D OUTPUT_GPIO_PIN=<número>` en los `build_flags` del entorno:

```ini
; variants/nrf52840/diy/nrf52_promicro_diy_tcxo/platformio.ini
[env:nrf52_promicro_diy_tcxo]
build_flags = ${nrf52840_base.build_flags}
  -I variants/nrf52840/diy/nrf52_promicro_diy_tcxo
  -D NRF52_PROMICRO_DIY
  -D OUTPUT_GPIO_PIN=38        ← P1.06 en nRF52 Pro Micro
```

Esta opción es preferible cuando el pin no debe estar disponible en todos los entornos de la misma placa (ej. el entorno con E-Ink usa ese mismo pin para otra función).

### Opción B — Define en `variant.h`

```cpp
// variants/rp2350/wiznet_5500_evb_pico2/variant.h
#define OUTPUT_GPIO_PIN  21   // GP21, o el número que corresponda
```

**Sin este `#define` (por ninguna de las dos vías), el módulo compila pero no instancia nada** — ninguna placa existente se ve afectada.

### Compilar

```bash
~/.platformio/penv/Scripts/pio run -e nrf52_promicro_diy_tcxo
```

---

## Cómo usar el parámetro

### Desde el CLI de Meshtastic (Python)

> Requisito previo: ejecutar el script de parche una sola vez (ver sección más abajo).

```bash
# Activar el GPIO — enciende el co-procesador y lanza el OTA
meshtastic --dest !<nodeid> --set device.output_gpio_enabled true

# Desactivar el GPIO manualmente (normalmente lo hace el nodo solo al terminar el OTA)
meshtastic --dest !<nodeid> --set device.output_gpio_enabled false

# Leer el estado eléctrico REAL del pin (digitalRead, no la config cacheada)
meshtastic --dest !<nodeid> --get device.output_gpio_enabled

# Verificar versión del firmware tras OTA
meshtastic --dest !<nodeid> --info
```

### Desde la app móvil de Meshtastic

```
Radio Config → Device → Output GPIO Enabled (toggle)
```

### Comportamiento en el dispositivo

- **ON → OFF:** el GPIO se desactiva inmediatamente, **sin reboot**.
- **OFF → ON:** el nodo guarda el estado y hace reboot en ~7 s. El GPIO **no** se activa antes del reboot — se activa en el boot siguiente vía `GpioOutputModule::apply()`. Esto garantiza que el co-procesador arranque limpio una sola vez (crítico con CHIP_PU/EN).
- **Al reiniciar el dispositivo:** el estado se restaura automáticamente desde flash.
- **En el log serial** (nivel DEBUG):
  ```
  GpioOutputModule: OUTPUT_GPIO_PIN=38 state=HIGH
  OtaRequestModule: ESP32-C5B found at 0x42
  OtaRequestModule: ESP32-C5B ready, status = 0xA0
  OtaRequestModule: sending CMD_START to ESP32-C5B
  ```

---

## Sistema OTA completo

### Hardware necesario

```
                    ┌─ WiFi AP "OTA_C5A" ──────────────────────┐
                    │                                           │
              [ESP32 C5-A]                              [ESP32 C5-B]
               Cualquier lugar                    En el nodo remoto
               con acceso físico                  alimentado por GPIO
                    │                                           │
                    │                                     I2C (0x42)
                    │                                     BLE DFU
                    │                                           │
                    └──────────────────────────────── [nRF52840]
                                                       Meshtastic
                                                       OUTPUT_GPIO_PIN → CHIP_PU/EN del ESP32 C5-B
```

**Conexiones I2C (nRF52 Pro Micro ↔ C5-B):**

| nRF52840 Pro Micro | ESP32 C5-B |
|---|---|
| P1.04 — SDA (pin 36) | GPIO6 |
| P0.11 — SCL (pin 11) | GPIO7 |
| GND | GND |

**Control de alimentación — conexión directa a CHIP_PU/EN:**
```
nRF52 P1.06 (OUTPUT_GPIO_PIN=38) ────────────► CHIP_PU / EN (ESP32 C5-B)
                                               (sin MOSFET ni transistor)
```
Consumo en reposo: **~0.002 mA** (ESP32 completamente apagado con CHIP_PU en LOW).
No se necesita MOSFET — nRF52 y ESP32 operan a 3.3 V; el GPIO puede alimentar CHIP_PU directamente.

### Paso 0 — Preparar C5-A (una sola vez)

1. Flashear `esp32_ota_coprocessor_A/` en el ESP32 C5-A
2. Conectar el móvil al WiFi `OTA_C5A` (pass: `meshtastic_ota`)
3. Abrir `http://192.168.4.1` → subir el `.zip` oficial de Meshtastic
4. C5-A extrae `fw.dat` + `fw.bin` y los sirve por HTTP indefinidamente

### Flujo OTA completo

```
TÉCNICO                    nRF52 (Meshtastic)        ESP32 C5-B           ESP32 C5-A
   │                              │                       │                    │
   │──set output_gpio=true────────►│                       │                    │
   │                       AdminModule                     │                    │
   │                       save config                     │                    │ (siempre encendido,
   │                       reboot (7s)                     │                    │  sirviendo archivos)
   │                    [GPIO HIGH al boot]                │◄──alimentación
   │                              │                        │
   │                        I2C scan                       │
   │                        detecta 0x42                   │
   │                              │                        │
   │                       OtaRequestModule                │
   │                       CMD_START ──────────────────────►│
   │                              │                        │ WiFi → "OTA_C5A"
   │                              │                        │ GET /firmware.dat
   │                              │                        │◄──────────────────► C5-A
   │                              │                        │ GET /firmware.bin
   │                              │                        │◄──────────────────► C5-A
   │                              │                        │ WiFi.disconnect()
   │                              │◄──STATUS_READY─────────│
   │                              │                        │
   │                       CMD_ENTER_DFU ──────────────────►│ (C5-B: empieza scan BLE)
   │                       GPREGRET=0xA8                   │
   │                       SystemReset()                   │
   │                              │                        │
   │                     [DFU Bootloader]                  │
   │                     anuncia "DfuTarg"                 │
   │                              │◄──BLE connect──────────│
   │                              │◄──Nordic DFU───────────│ (fw.dat + fw.bin)
   │                              │  valida + flashea      │
   │                              │  reboot                │
   │                              │                        │
   │                     [nuevo firmware]                  │
   │                        I2C scan                       │
   │                        detecta 0x42                   │
   │                              │◄──STATUS_DFU_DONE──────│
   │                       CMD_DONE ───────────────────────►│ (C5-B: limpia archivos)
   │                       output_gpio=false               │
   │                       GPIO LOW                        │──pierde alimentación
   │                              │
   │──meshtastic --info───────────►│
   │◄──nueva versión───────────────│  ✓ OTA verificado
```

### Protocolo I2C (nRF52 master ↔ C5-B slave 0x42)

**Comandos nRF52 → C5-B:**

| Byte | Comando | Acción en C5-B |
|------|---------|----------------|
| `0x01` | `CMD_STATUS` | Responde con 1 byte de estado |
| `0x02` | `CMD_START` | Conecta WiFi + descarga firmware |
| `0x03` | `CMD_ENTER_DFU` | Espera 3 s + escanea BLE + Nordic DFU |
| `0x04` | `CMD_DONE` | Limpia archivos, vuelve a IDLE |

**Estados C5-B → nRF52 (respuesta a CMD_STATUS):**

| Byte | Estado | Significado |
|------|--------|-------------|
| `0xA0` | `IDLE` | Esperando CMD_START |
| `0xA1` | `CONNECTING` | Conectando al WiFi de C5-A |
| `0xA2` | `DOWNLOADING` | Descargando fw.dat + fw.bin |
| `0xA3` | `READY` | Firmware listo para DFU |
| `0xA4` | `BLE_DFU` | Transferencia BLE en progreso |
| `0xA5` | `DFU_DONE` | DFU completado (boot post-OTA) |
| `0xAF` | `ERROR` | Error no recuperable |

---

## Script de parche para el CLI Python

El paquete oficial de Meshtastic no incluye el campo `output_gpio_enabled`. El script `scripts/patch_meshtastic_proto.py` lo añade al paquete instalado localmente.

### Cuándo ejecutarlo

```bash
# Primera vez, tras instalar meshtastic
pip install meshtastic
python scripts/patch_meshtastic_proto.py

# Tras actualizar meshtastic
pip install --upgrade meshtastic
python scripts/patch_meshtastic_proto.py
```

### Cómo funciona el script

1. Localiza el paquete meshtastic instalado (`importlib.util.find_spec`)
2. Lee `config_pb2.py` y extrae el `FileDescriptorProto` serializado
3. Deserializa el descriptor con `google.protobuf.descriptor_pb2`
4. Añade el campo `output_gpio_enabled` (tag 14, tipo bool) a `Config.DeviceConfig`
5. Re-serializa y reemplaza el blob en `config_pb2.py`
6. Actualiza el stub de tipos `config_pb2.pyi` para autocompletado

El script es **idempotente**: si el campo ya existe, no hace nada.

### Verificar que el parche funcionó

```bash
python -c "
from meshtastic.protobuf import config_pb2
d = config_pb2.Config.DeviceConfig()
d.output_gpio_enabled = True
print('OK — output_gpio_enabled =', d.output_gpio_enabled)
"
```

---

## Detalles técnicos del campo protobuf

```protobuf
// En protobufs/meshtastic/config.proto → Config.DeviceConfig
bool output_gpio_enabled = 14;
```

| Propiedad | Valor |
|---|---|
| Tipo | `bool` |
| Tag protobuf | `14` |
| Mensaje | `Config.DeviceConfig` |
| Valor por defecto | `false` (pin en LOW al arrancar) |
| Persistencia | Flash (NVS / LittleFS según plataforma) |
| Requiere reboot para aplicar | No (pero OFF→ON provoca reboot para I2C scan) |

---

## Flujos de operación detallados

### SET ON — encender el co-procesador (OFF → ON)

```
1. meshtastic --dest !<nodeid> --set device.output_gpio_enabled true
   → AdminMessage cifrado → viaja por el mesh

2. Nodo destino: AdminModule::handleSetConfig()
   → detecta OFF→ON → requiresReboot = true
   → config.device.output_gpio_enabled = true → guardado en flash
   → gpioOutputModule->apply() NO se llama antes del reboot
     (evita ciclo de alimentación en CHIP_PU durante el reboot del nRF52)

3. Reboot automático tras ~7 segundos

4. En el boot:
   → GpioOutputModule::apply() → GPIO HIGH → C5-B arranca por primera vez (limpio)
   → I2C scan → detecta ESP32 en 0x42 → tipo ESP32_OTA_COPROCESSOR
     Si C5-B aún no responde: STATE_PROBE_RETRY — sondea 0x42 cada 2 s hasta 60 s
   → OtaRequestModule::i2cScanFinished() → queryStatus() → STATUS_IDLE
   → Máquina de estados: CMD_START → descarga → CMD_ENTER_DFU → BLE DFU → CMD_DONE

5. Tras OTA exitoso:
   → OtaRequestModule::powerOffCoprocessor()
   → config.device.output_gpio_enabled = false → guardado en flash
   → GPIO LOW → C5-B pierde alimentación
```

### SET OFF — apagar manualmente (ON → OFF)

```
1. meshtastic --dest !<nodeid> --set device.output_gpio_enabled false
   → config guardado en flash → GPIO LOW inmediato → NO reboot
   → C5-B pierde alimentación
```

### GET — leer el estado real del pin

```
1. meshtastic --dest !<nodeid> --get device.output_gpio_enabled

2. Nodo destino: AdminModule::handleGetConfig(DEVICE_CONFIG)
   → digitalRead(OUTPUT_GPIO_PIN)        ← estado eléctrico real del pin
   → actualiza output_gpio_enabled en memoria (NO en flash)
   → responde con config.device

3. El valor devuelto refleja el estado hardware, no la config guardada.
   Útil para detectar discrepancias (ej. CHIP_PU en LOW cuando debería estar HIGH).
```

---

## Limitaciones conocidas

- **Un solo pin por placa.** Para múltiples pines, usar `RemoteHardwareModule`.
- **Solo nivel digital.** No hay soporte para PWM ni salidas analógicas.
- **Sin feedback C5-B → C5-A.** La UI del C5-A no recibe notificación de que el DFU terminó. Verificar el OTA con `meshtastic --dest !<nodeid> --info`.
- **Parche manual del CLI.** Al actualizar el paquete `meshtastic` con pip hay que re-ejecutar el script de parche.
- **C5-B no reintenta en caso de error WiFi/BLE.** Si `OtaRequestModule` recibe `STATUS_ERROR`, aborta. Para reintentar hay que volver a enviar `output_gpio_enabled = false` seguido de `true`.

---

## Checklist de prueba completa

### 1 — Compilar y flashear

```bash
# C5-A (servidor de firmware — cualquier ESP32 accesible)
cd esp32_ota_coprocessor_A
~/.platformio/penv/Scripts/pio run -e esp32c6 --target upload

# C5-B (co-procesador en el nodo remoto)
cd esp32_ota_coprocessor
~/.platformio/penv/Scripts/pio run -e esp32c6 --target upload

# nRF52 (Meshtastic con OUTPUT_GPIO_PIN=38)
cd ../firmware
~/.platformio/penv/Scripts/pio run -e nrf52_promicro_diy_tcxo --target upload
```

### 2 — Cableado

| nRF52 Pro Micro | ESP32 C5-B | Función |
|---|---|---|
| P1.04 (pin 36) | GPIO6 | I2C SDA |
| P0.11 (pin 11) | GPIO7 | I2C SCL |
| P1.06 (pin 38) | CHIP_PU / EN | Control de alimentación |
| GND | GND | Masa común |

### 3 — Preparar C5-A (una sola vez por versión de firmware)

```bash
# Conectar móvil o PC al WiFi "OTA_C5A" (pass: meshtastic_ota)
# Abrir http://192.168.4.1 → subir el .zip oficial de Meshtastic para nRF52
# C5-A extrae fw.dat + fw.bin automáticamente
```

### 4 — Ejecutar OTA

```bash
# Aplicar parche al CLI (si no está hecho)
pip install meshtastic
python scripts/patch_meshtastic_proto.py

# Activar co-procesador — dispara reboot + OTA
meshtastic --dest !<nodeid> --set device.output_gpio_enabled true

# Esperar ~3-5 minutos (descarga WiFi + BLE DFU)

# Verificar versión del firmware tras OTA
meshtastic --dest !<nodeid> --info
```

### 5 — Logs esperados (monitor serie nRF52)

```
GpioOutputModule: OUTPUT_GPIO_PIN=38 state=HIGH
OtaRequestModule: ESP32-C5B found at 0x42         ← o bien:
OtaRequestModule: C5-B not in scan but GPIO is HIGH — will retry for up to 60s
OtaRequestModule: C5-B responded on retry N
OtaRequestModule: ESP32-C5B ready, status = 0xA0
OtaRequestModule: sending CMD_START to ESP32-C5B
OtaRequestModule: poll N/60 status=0xA2           ← descargando
OtaRequestModule: firmware ready, entering DFU bootloader
[DFU Bootloader advertising as "DfuTarg"]
[BLE DFU transfer... reboot]
OtaRequestModule: OTA confirmed, sending CMD_DONE, powering off co-processor
GpioOutputModule: OUTPUT_GPIO_PIN=38 state=LOW
```
