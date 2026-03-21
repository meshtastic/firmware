# Raspberry Pi Pico 2 + W5500 + E22-900M30S — Meshtastic Variant ("-2")

Soporte de Meshtastic para una **Raspberry Pi Pico 2** (RP2350, 4 MB flash) con módulo Ethernet **W5500** externo y módulo LoRa **EBYTE E22-900M30S** externo.

> **Variante "-2"**: los pines SPI0 del W5500 han sido reubicados de GP16-19 a GP20-23 para evitar un puerto SPI0 dañado. El reset del W5500 se mueve a GP7.
>
> **Diferencia con `pico2_w5500_e22`**: misma funcionalidad, solo cambian los pines del W5500.
> **Diferencia con `wiznet_5500_evb_pico2`**: en el EVB-Pico2 el W5500 está soldado en la PCB con pines fijos y la flash es de 2 MB. Aquí el W5500 es un módulo externo y la Pico 2 tiene 4 MB de flash.

---

## Hardware necesario

| Componente | Modelo                        | Detalle                               |
|------------|-------------------------------|---------------------------------------|
| MCU        | Raspberry Pi Pico 2           | RP2350 @ 150 MHz, 512 KB RAM, 4 MB flash |
| Ethernet   | Módulo W5500                  | SPI (WIZnet W5500, breakout cualquiera) |
| LoRa       | EBYTE E22-900M30S             | SX1262 + PA 30 dBm, 868/915 MHz      |

---

## Pinout

### Pines de sistema (Pico 2, fijos)

| GPIO  | Función                                       |
|-------|-----------------------------------------------|
| GP24  | VBUS sense — HIGH si USB presente             |
| GP25  | LED de usuario (heartbeat)                    |
| GP29  | ADC3 — VSYS/3, mide tensión de alimentación  |

### W5500 Ethernet (SPI0 — pines reubicados)

Conectar el módulo W5500 a estos pines (**distintos a la variante original**):

| Señal W5500 | GPIO Pico 2 | Anterior | Cable  |
|-------------|-------------|----------|--------|
| MISO        | **GP20**    | GP16     | amarillo |
| CS / SCS    | **GP21**    | GP17     | naranja  |
| SCK         | **GP22**    | GP18     | verde    |
| MOSI        | **GP7**     | GP19     | azul     |
| RST         | **GP6**     | GP20     | gris     |
| INT         | —           | —        | no conectar (nc) |
| VCC         | 3.3V        | —        | rojo     |
| GND         | GND         | —        | negro    |

> SPI0 queda reservado para el W5500. GP20/22 son pines SPI0 RX/SCK
> alternativos en el RP2350. GP7 es SPI0 TX alternativo (cada función SPI
> se repite cada 4 GPIOs). GP21 y GP6 son GPIOs genéricos (CS y RST).
>
> **Nota:** GP23/GP24 son pines internos en la Pico 2 (control SMPS/VBUS),
> no están expuestos en el header — por eso MOSI usa GP7 en lugar de GP23.

### E22-900M30S LoRa (SPI1)

| Señal E22 | GPIO Pico 2 | Notas                                     |
|-----------|-------------|-------------------------------------------|
| SCK       | GP10        | SPI1 clock                                |
| MOSI      | GP11        | SPI1 TX                                   |
| MISO      | GP12        | SPI1 RX                                   |
| NSS / CS  | GP13        | Chip select                               |
| RESET     | GP15        | Reset activo en LOW                       |
| DIO1      | GP14        | Interrupción IRQ                          |
| BUSY      | GP2         | Estado del módulo                         |
| RXEN      | GP3         | LNA enable — se mantiene HIGH permanentemente |
| TXEN      | ← DIO2      | Ver nota de cableado abajo                |
| VCC       | 3.3V        | Alimentación (añadir cap 100 µF cerca)    |
| GND       | GND         | —                                         |

> Ver `wiring.svg` en este directorio para el diagrama completo.

---

## Cableado especial: DIO2 → TXEN en el módulo E22

El E22-900M30S **no conecta internamente** DIO2 al pin TXEN de su PA. Hay que unirlos con un cable corto o puente de soldadura **en el propio módulo**:

```
Pin DIO2 del E22  ──┐
                    ├── cable / solder bridge en el módulo
Pin TXEN del E22  ──┘
```

Con este puente activo, `SX126X_DIO2_AS_RF_SWITCH` hace que el SX1262 lleve DIO2 a HIGH automáticamente durante TX, activando el PA sin necesitar un GPIO del RP2350 para TXEN.

**Sin este puente el módulo no transmite.**

---

## Compilar

```bash
pio run -e pico2_w5500_e22-2
```

### Flashear — modo BOOTSEL

1. Mantén pulsado el botón **BOOTSEL** de la Pico 2.
2. Conecta USB al PC — aparece como unidad de almacenamiento `RPI-RP2`.
3. Copia el `.uf2`:

```
.pio/build/pico2_w5500_e22-2/firmware-pico2_w5500_e22-2-*.uf2
```

O directamente con picotool:

```bash
pio run -e pico2_w5500_e22-2 -t upload
```

### Flashear — OTA por Ethernet

Si el nodo ya está conectado a la red con el W5500, puedes actualizar el firmware sin USB usando el script `eth-ota-upload.py`. El nodo escucha en el **puerto TCP 4243**.

1. Activa el entorno virtual de Python del proyecto:

```powershell
# Windows (PowerShell)
.venv\Scripts\Activate.ps1

# Linux / macOS
source .venv/bin/activate
```

2. Compila el firmware (si no lo has hecho):

```bash
pio run -e pico2_w5500_e22-2
```

3. Sube el `.bin` (no el `.uf2`) indicando la IP del nodo:

```bash
python bin/eth-ota-upload.py --host <IP_DEL_NODO> .pio/build/pico2_w5500_e22-2/firmware-pico2_w5500_e22-2-*.bin
```

Ejemplo con IP fija:

```bash
python bin/eth-ota-upload.py --host 192.168.1.165 .pio/build/pico2_w5500_e22-2/firmware-pico2_w5500_e22-2-*.bin
```

> **PSK por defecto:** `meshtastic_ota_default_psk_v1!!!`. Para usar un PSK personalizado, añade `--psk <clave>`.
>
> El script comprime el firmware con GZIP automáticamente (~62% del tamaño original). La transferencia toma ~10 segundos en LAN. El nodo se reinicia solo al terminar.

---

## Uso en red

La board usa Ethernet (no WiFi). Desde la app Meshtastic:

- **Activar Ethernet** en `Config → Network → Ethernet Enabled`
- **DHCP** por defecto; o configura IP estática

Servicios habilitados una vez conectado:

| Servicio | Detalle                     |
|----------|-----------------------------|
| NTP      | Sincronización de hora      |
| MQTT     | Mensajes a broker externo   |
| API      | Socket TCP puerto 4403      |
| Syslog   | Logs remotos (opcional)     |

---

## Notas técnicas

### LoRa — control de RF

| Define                       | Efecto                                                   |
|------------------------------|----------------------------------------------------------|
| `SX126X_ANT_SW 3`            | GP3 (RXEN) se pone HIGH al init y no se vuelve a tocar  |
| `SX126X_DIO2_AS_RF_SWITCH`   | SX1262 lleva DIO2 HIGH en TX → activa TXEN por el bridge |
| `SX126X_DIO3_TCXO_VOLTAGE 1.8` | TCXO del E22 controlado por DIO3                       |
| `-D EBYTE_E22`               | Activa soporte TCXO en el firmware                       |
| `-D EBYTE_E22_900M30S`       | `TX_GAIN_LORA=10`, potencia máxima 22 dBm configurada     |

> Tanto RXEN como TXEN pueden estar en HIGH simultáneamente durante TX — es seguro para el switch RF del E22.

### Ethernet

- Librería: `arduino-libraries/Ethernet@^2.0.2` (soporta W5100/W5200/W5500 con auto-detección).
- SPI0 se inicializa explícitamente con pines GP20/22/7 antes de `Ethernet.init()` (reubicados desde GP16/18/19).
- Timeout DHCP configurado a 10 s (en lugar de 60 s del default) para no bloquear el inicio del LoRa.

### HW_VENDOR

Mapeado a `meshtastic_HardwareModel_PRIVATE_HW` — no existe modelo dedicado en el protobuf de Meshtastic para esta combinación.

---

## Diferencias respecto a `wiznet_5500_evb_pico2`

| Aspecto        | `wiznet_5500_evb_pico2`           | `pico2_w5500_e22` (este)           |
|----------------|-----------------------------------|------------------------------------|
| Board          | WIZnet W5500-EVB-Pico2            | Raspberry Pi Pico 2                |
| Flash          | 2 MB                              | **4 MB**                           |
| W5500          | Soldado en PCB (pines fijos)      | Módulo externo (mismo pinout)      |
| `board =`      | `wiznet_5500_evb_pico2`           | `rpipico2`                         |
| `variant.h`    | Idéntico en pines                 | Idéntico en pines                  |

---

## Uso de memoria (build de referencia)

| Recurso | Usado   | Total    | %     |
|---------|---------|----------|-------|
| RAM     | 94 KB   | 512 KB   | 18%   |
| Flash   | 964 KB  | 3.58 MB  | 26.3% |

---
Perfecto, gracias por confirmar. El log lo dice claro:

No SX1262 radio + init result -2 (CHIP_NOT_FOUND) → el RP2350 no puede hablar con el SX1262 por SPI.

Para mañana, cosas a verificar en orden:

Alimentación del E22 — ¿3.3V presente en el pin VCC del módulo?
GND común — ¿el GND del E22 está conectado al GND del Pico2?
SPI1 pins — revisa cada cable con continuidad:
GP10 → SCK del E22
GP11 → MOSI (TXD) del E22
GP12 → MISO (RXD) del E22
GP13 → CS/NSS del E22
BUSY (GP2) — ¿conectado al pin BUSY del E22? Si está flotando puede bloquear el init
RESET (GP15) — ¿conectado al pin NRST del E22?
El error CHIP_NOT_FOUND casi siempre es un cable mal puesto, GND flotante, o VCC ausente. Con un multímetro en 30 segundos se descarta lo eléctrico.
