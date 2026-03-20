# WIZnet W5500-EVB-Pico2 — Meshtastic Variant

Soporte de Meshtastic para la board **WIZnet W5500-EVB-Pico2** (RP2350 + Ethernet W5500 integrado) con módulo LoRa **EBYTE E22-900M30S** externo.

---

## Hardware

| Componente | Detalle                                              |
|------------|------------------------------------------------------|
| MCU        | RP2350 @ 150 MHz                                     |
| RAM        | 512 KB                                               |
| Flash      | 2 MB                                                 |
| Ethernet   | W5500 integrado (SPI0, pines fijos en PCB)           |
| LoRa       | EBYTE E22-900M30S externo (SX1262 + PA 30 dBm, SPI1) |
| GPS        | Módulo GPS externo (UART1 — GP8 TX, GP9 RX)           |

### Pines de sistema (fijos en PCB)

| GPIO  | Dirección | Función                                       |
|-------|-----------|-----------------------------------------------|
| GP24  | I         | VBUS sense — HIGH si USB presente, LOW si no  |
| GP25  | O         | LED de usuario (heartbeat)                    |
| GP29  | I (ADC3)  | VSYS/3 — mide tensión de alimentación         |

---

## Pinout W5500 (Ethernet — SPI0, fijos en PCB)

Estos pines están soldados en el PCB del W5500-EVB-Pico2. No requieren cableado.

| Señal W5500    | GPIO RP2350 |
|----------------|-------------|
| MISO (SPI0 RX) | GP16        |
| CSn            | GP17        |
| SCK (SPI0 CLK) | GP18        |
| MOSI (SPI0 TX) | GP19        |
| RST            | GP20        |
| INT            | GP21        |

---

## Pinout EBYTE E22-900M30S (SPI1, conexión externa)

Conectar el módulo según esta tabla. Los pines pueden cambiarse editando `variant.h`.

| Señal E22 | GPIO RP2350      | Notas                                   |
|-----------|------------------|-----------------------------------------|
| SCK       | GP10             | SPI1 clock                              |
| MOSI      | GP11             | SPI1 TX                                 |
| MISO      | GP12             | SPI1 RX                                 |
| NSS / CS  | GP13             | Chip select                             |
| RESET     | GP15             | Reset activo en LOW                     |
| DIO1      | GP14             | Interrupción IRQ                        |
| BUSY      | GP2              | Estado del módulo                       |
| RXEN      | GP3              | LNA enable — HIGH = RX (MCU-controlado) |
| TXEN      | ← DIO2 del E22   | Ver nota de cableado abajo              |
| VCC       | 3.3V             | Alimentación                            |
| GND       | GND              | —                                       |

> SPI0 está reservado para el W5500 (GP16-19), por eso el E22 usa SPI1.

### Cableado especial: DIO2 → TXEN en el módulo E22

El E22-900M30S **no conecta internamente** DIO2 al TXEN de su PA. Es necesario unirlos externamente con un cable o puente de soldadura en el propio módulo:

```
E22 pin DIO2  ──┐
                ├── cable corto / solder bridge en el módulo
E22 pin TXEN  ──┘
```

Con este puente, `SX126X_DIO2_AS_RF_SWITCH` hace que el SX1262 lleve DIO2 a HIGH automáticamente durante TX, activando el PA sin necesitar un GPIO del RP2350 para TXEN.

---

## Pinout GPS (UART1, conexión externa)

Módulo GPS conectado por UART1 (Serial2) a 38400 baud.

| Señal GPS | GPIO RP2350 | Notas                          |
|-----------|-------------|--------------------------------|
| TX        | GP9         | GPS transmite → RP2350 recibe  |
| RX        | GP8         | RP2350 transmite → GPS recibe  |
| VCC       | 3.3V        | Alimentación                   |
| GND       | GND         | —                              |

> **Nota:** Los pines se cruzan — TX del GPS va a GP9 (RX del RP2350) y RX del GPS va a GP8 (TX del RP2350).

---

## Compilar

```bash
pio run -e wiznet_5500_evb_pico2
```

### Flashear (modo BOOTSEL)

1. Mantén pulsado el botón **BOOTSEL** de la board.
2. Conecta USB al PC — aparece como unidad de almacenamiento.
3. Copia el `.uf2` generado:

```
.pio/build/wiznet_5500_evb_pico2/firmware-wiznet_5500_evb_pico2-*.uf2
```

O directamente con picotool:

```bash
pio run -e wiznet_5500_evb_pico2 -t upload
```

---

## Uso en red

La board usa Ethernet (no WiFi). En la app Meshtastic o mediante la API:

- **Activar Ethernet** en `Config → Network → Ethernet Enabled`
- **DHCP** por defecto, o configura IP estática según necesidad

Servicios habilitados una vez conectado:
- NTP (sincronización de hora automática)
- MQTT
- API socket (puerto 4403)
- Syslog (opcional)

---

## Archivos modificados / creados

### Archivos nuevos

| Archivo | Descripción                                                                       |
|--------------------------------------------------------|------------------------------------|
| `variants/rp2350/wiznet_5500_evb_pico2/variant.h`      | Definición de pines del hardware   |
| `variants/rp2350/wiznet_5500_evb_pico2/platformio.ini` | Entorno PlatformIO para esta board |
| `variants/rp2350/wiznet_5500_evb_pico2/README.md`      | Este archivo                       |

### Archivos modificados

| Archivo                              | Cambio                                                                                     |
|--------------------------------------|--------------------------------------------------------------------------------------------|
| `src/platform/rp2xx0/architecture.h` | Añadido `WIZNET_5500_EVB_PICO2` → `HW_VENDOR = PRIVATE_HW`                                 |
| `src/DebugConfiguration.h`           | Include condicional `<Ethernet.h>` para esta board (antes de intentar `RAK13800_W5100S.h`) |
| `src/mesh/api/ethServerAPI.h`        | Idem — permite compilar sin la librería RAK                                                |
| `src/mesh/eth/ethClient.cpp`         | Inicialización SPI0 explícita y `Ethernet.init(PIN_ETHERNET_SS)` para W5500                |

---

## LED heartbeat (GP25)

El LED de usuario en GP25 muestra el estado de la placa:

| Patrón                     | Estado                                    |
|----------------------------|-------------------------------------------|
| Parpadeo lento ~0.5 Hz     | Normal — USB conectado, operación activa  |
| Siempre encendido           | Batería completamente cargada (≥ 100%)    |
| Destello breve cada 1 s    | Descargando batería externa               |
| Parpadeo rápido (250 ms)   | Tensión crítica (< 5%)                   |
| Encendido fijo al arrancar | Inicio normal del firmware                |

### Monitoreo de energía

El firmware detecta la presencia de USB mediante dos señales del hardware:

- **GP24 (VBUS sense)**: leído digitalmente por `EXT_PWR_DETECT`. HIGH → USB presente.
- **GP29 (ADC3, VSYS/3)**: leído mediante ADC. Multiplier `3.0` reconstruye la tensión VSYS. Si VSYS > 4.2 V el firmware activa el modo carga; por eso se define `NO_BATTERY_LEVEL_ON_CHARGE` para evitar reportar falso "100% batería" cuando lo que hay es alimentación USB por VSYS.

---

## Notas técnicas

### Ethernet
- Librería: `arduino-libraries/Ethernet@^2.0.2` — soporta W5100/W5200/W5500 con auto-detección del chip.
- Inicialización: SPI0 se configura explícitamente (pines GP16/18/19) antes de llamar a `Ethernet.init()`.
- Los cambios en `src/` son retrocompatibles: las boards existentes (RAK11310, RAK4631, etc.) no se ven afectadas.

### LoRa — EBYTE E22-900M30S
- **`SX126X_DIO2_AS_RF_SWITCH` activo**: el SX1262 lleva DIO2 a HIGH durante TX; ese DIO2 se puentea externamente al TXEN del E22 (ver sección de cableado).
- **RXEN (GP3) obligatorio**: activa el LNA para RX. Controlado por MCU.
- **TXEN en NC (MCU)**: el PA se controla por el puente DIO2→TXEN en el módulo, no por un GPIO del RP2350.
- **TCXO**: el E22-900M30S incluye TCXO controlado por DIO3 (`SX126X_DIO3_TCXO_VOLTAGE 1.8`). Activado mediante `-D EBYTE_E22` en build flags.
- **Potencia TX**: configurada mediante `-D EBYTE_E22_900M30S` → `TX_GAIN_LORA=10`, `SX126X_MAX_POWER=22` (equivalente a 30 dBm en antena).

### HW_VENDOR
- Mapeado a `meshtastic_HardwareModel_PRIVATE_HW` — no existe modelo dedicado en el protobuf de Meshtastic para esta board aún.

---

## Uso de memoria (build de referencia)

| Recurso | Usado  | Total  | %   |
|---------|--------|--------|-----|
| RAM     | 94 KB  | 512 KB | 18% |
| Flash   | 962 KB | 1.5 MB | 61% |
