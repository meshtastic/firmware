// Axiometa Genesis Mini - ESP32-S3-MINI-1-N4R2 with four AX22 module ports.
// Pin numbers follow the upstream Arduino core variant (espressif/arduino-esp32
// variants/axiometa_genesis_mini).
//
// Every AX22 port carries the same I2C and SPI buses plus three per-port GPIOs, so which port a
// module occupies only changes those three pins. Override the two port numbers below at build time
// to match how a kit is actually assembled, e.g. -D AXIOMETA_LORA_PORT=3.

#define AX22_P1_IO0 4
#define AX22_P1_IO1 3
#define AX22_P1_IO2 2
#define AX22_P2_IO0 7
#define AX22_P2_IO1 6
#define AX22_P2_IO2 5
#define AX22_P3_IO0 9
#define AX22_P3_IO1 16
#define AX22_P3_IO2 15
#define AX22_P4_IO0 1
#define AX22_P4_IO1 17
#define AX22_P4_IO2 18

// Two levels so the port argument expands to its number before being pasted.
#define AX22_PIN_(port, io) AX22_P##port##io
#define AX22_PIN(port, io) AX22_PIN_(port, io)

#ifndef AXIOMETA_LORA_PORT
#define AXIOMETA_LORA_PORT 2
#endif
#ifndef AXIOMETA_DISPLAY_PORT
#define AXIOMETA_DISPLAY_PORT 1
#endif
#ifndef AXIOMETA_ENCODER_PORT
#define AXIOMETA_ENCODER_PORT 3 // 0 = no encoder module fitted
#endif

// Each peripheral claims exactly one port, so a port number becomes a one-hot bit and 0 ("not
// fitted") claims nothing. OR and SUM of the claims agree only when no two bits overlap, so a
// double-booked port is a compile error rather than two drivers quietly configuring the same
// three pins - which on this board would mean the radio's NSS doubling as an encoder channel,
// and a node that simply never transmits. Add a term per peripheral as ports get used.
#define AX22_PORT_BIT(p) ((p) ? (1u << (p)) : 0u) // preprocessor only: evaluates p twice
#define AX22_CLAIM_OR                                                                                                            \
    (AX22_PORT_BIT(AXIOMETA_LORA_PORT) | AX22_PORT_BIT(AXIOMETA_DISPLAY_PORT) | AX22_PORT_BIT(AXIOMETA_ENCODER_PORT))
#define AX22_CLAIM_SUM                                                                                                           \
    (AX22_PORT_BIT(AXIOMETA_LORA_PORT) + AX22_PORT_BIT(AXIOMETA_DISPLAY_PORT) + AX22_PORT_BIT(AXIOMETA_ENCODER_PORT))

#if AX22_CLAIM_OR != AX22_CLAIM_SUM
#error "Two AX22 peripherals claim the same port - check AXIOMETA_LORA_PORT / _DISPLAY_PORT / _ENCODER_PORT"
#endif
#if (AX22_CLAIM_OR & ~0x1Eu) != 0u // bits 1..4
#error "An AXIOMETA_*_PORT is out of range: use 1-4, or 0 for a peripheral that is not fitted"
#endif
#if !AXIOMETA_LORA_PORT || !AXIOMETA_DISPLAY_PORT
#error "AXIOMETA_LORA_PORT and AXIOMETA_DISPLAY_PORT are required (1-4); only the encoder may be 0"
#endif

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// Shared by the STEMMA QT connector and every AX22 port.
#define I2C_SDA 10
#define I2C_SCL 11

#define LED_POWER 37 // If defined we will blink this LED

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 21
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// Active low, per hardware. GPIO45 is also the VDD_SPI strapping pin, so don't hold
// the button through reset - that selects 1.8 V flash and the module won't boot.
#define BUTTON_PIN 45

#define CANCEL_BUTTON_PIN 0
#define CANCEL_BUTTON_ACTIVE_LOW true
#define CANCEL_BUTTON_ACTIVE_PULLUP true

#define BATTERY_PIN 8             // 1/2 divider off the 3x AA/AAA pack
#define ADC_CHANNEL ADC_CHANNEL_7 // GPIO8 is ADC1_CH7
#define ADC_MULTIPLIER 2.0
#define ADC_CTRL 46 // BAT_ENABLE; gates the divider so it doesn't drain the pack. Polarity unverified.
#define ADC_CTRL_ENABLED HIGH

// The AA pack is primary cells and there is no charger on the board, so USB is power but never a
// charge. There is no VBUS sense pin either - the AX22 ports use every free GPIO - so external
// power is inferred from the ESP32-S3's native USB port. That only sees a USB host, not a charger.
#define BATTERY_NOT_RECHARGEABLE
#define USB_HOST_PWR_DETECT

// Three alkaline AA/AAA cells, not one Li-ion cell, so the stock OCV curve reads the pack against
// entirely the wrong chemistry. Alkalines slope steadily from ~1.6 V to ~1.05 V instead of holding
// a plateau. 0% is anchored at 1050 mV/cell (3150 mV pack): the TLV62569 is a buck, so once the
// pack falls under ~3.4 V it runs at 100% duty and the rail tracks the pack down toward the S3's
// 3.0 V floor, and by then an alkaline is ~95% spent anyway. That anchor is also the low-voltage
// deep-sleep threshold, which lands just above where LoRa TX current would brown the node out.
#define NUM_CELLS 3
#define OCV_ARRAY 1600, 1450, 1400, 1350, 1310, 1270, 1230, 1190, 1150, 1110, 1050

// Elecrow LR1262 (SX1262) carrier: IO0 = DIO1, IO1 = NSS, IO2 = BUSY.
#define USE_SX1262

#define LORA_SCK 14
#define LORA_MISO 13
#define LORA_MOSI 12
#define LORA_CS AX22_PIN(AXIOMETA_LORA_PORT, _IO1)

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 AX22_PIN(AXIOMETA_LORA_PORT, _IO0)
#define LORA_RESET RADIOLIB_NC // AX22 exposes only 3 GPIO per port; NRST is tied off on-module

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY AX22_PIN(AXIOMETA_LORA_PORT, _IO2)
#define SX126X_RESET LORA_RESET

// LR1262 wires SX1262 DIO2 to the module's TX/RX_EN antenna switch, and DIO3 supplies
// the 32 MHz TCXO (datasheet allows 1.8-3.3 V).
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// AX22-0034 ST7735S 160x80 IPS panel: IO0 = CS, IO1 = RST, IO2 = DC. The module has no MISO, and
// its backlight pad isn't on the AX22 connector, so the backlight is always on.
#define HAS_SPI_TFT 1
#define USE_TFTDISPLAY 1
#define ST7735S 1
#define ST7735_CS AX22_PIN(AXIOMETA_DISPLAY_PORT, _IO0)
#define ST7735_RESET AX22_PIN(AXIOMETA_DISPLAY_PORT, _IO1)
#define ST7735_RS AX22_PIN(AXIOMETA_DISPLAY_PORT, _IO2) // DC
#define ST7735_SDA LORA_MOSI
#define ST7735_SCK LORA_SCK
// The panel has no MISO, but this must still name the radio's MISO pin: LovyanGFX's bus init takes
// IDF's GPIO-matrix path, and with -1 it never wires spiq_in, so RadioLib reads a constant 0.
#define ST7735_MISO LORA_MISO
#define ST7735_BUSY -1
#define TFT_INVERT false

// The backlight pad isn't on the AX22 connector, so there is no light to switch off: screen-off
// has to be done entirely by the controller (blank frame memory, display off, sleep in).
#define TFT_BLANK_ON_DISPLAY_OFF

// Must be the host Arduino's SPI object already uses (SPI2_HOST == FSPI on ESP32-S3). The panel
// shares SCK/MOSI with the radio, and LovyanGFX binds pins to its host for good at init - pointing
// it at a second host would steal the pads from RadioLib via the GPIO matrix.
#define ST7735_SPI_HOST SPI2_HOST
#define TFT_DMA_CHANNEL 0      // DMA on the shared host breaks RadioLib's CPU-driven transfers
#define SPI_FREQUENCY 10000000 // module is rated ~10 MHz
#define SPI_READ_FREQUENCY 6000000

#define SCREEN_ROTATE
#define TFT_WIDTH 80
#define TFT_HEIGHT 160
#define TFT_OFFSET_X 24
#define TFT_OFFSET_Y 0
#define SCREEN_TRANSITION_FRAMERATE 3 // fps

// 160x80 is too small for the big-TFT UI tier. Without this, HAS_SPI_TFT promotes FONT_SMALL to the
// 16pt face (19px tall) and determineScreenResolution() classifies the panel High - the T-Deck /
// T-Lora Pager tier - which overruns the System screen's usage bars and pushes body rows off the
// bottom. Same setting the other 160x80 ST7735S boards use.
#define DISPLAY_FORCE_SMALL_FONTS

// AX22-0003 rotary encoder (ALPS EC11L1525G01): IO0 = push switch, IO1/IO2 = the two channels.
// There is no RC network on the module - the driver's transition table is what rejects bounce.
//
// The switch idles LOW and goes high when pressed, opposite to the two channels. Measured on
// hardware, and it agrees with the vendor's own Arduino example (digitalRead(PIN_BT) == HIGH for
// a press); the schematic was read the other way round and was wrong. Channel polarity is not
// worth chasing: inverting both channels together preserves direction and count through the
// decoder's transition table, so it is unobservable either way.
//
// Slot 3 by default. Its GPIO15/16 are the XTAL_32K pads (no crystal is fitted on this module)
// and the default-matrix U0RTS/U0CTS (flow control unused, UART0 is on 43/44), so both are
// ordinary GPIOs here.
#if AXIOMETA_ENCODER_PORT
#define INPUTDRIVER_ENCODER_TYPE 4
#define INPUTDRIVER_ENCODER_BTN AX22_PIN(AXIOMETA_ENCODER_PORT, _IO0)
#define INPUTDRIVER_ENCODER_A AX22_PIN(AXIOMETA_ENCODER_PORT, _IO1)
#define INPUTDRIVER_ENCODER_B AX22_PIN(AXIOMETA_ENCODER_PORT, _IO2)
#define INPUTDRIVER_ENCODER_BTN_ACTIVE_LOW 0 // switch idles low, unlike the channels

// The vendor's Arduino example uses RotaryEncoder::LatchMode::TWO03 - half a quadrature cycle
// per detent - rather than the usual FOUR3, and the EC11L1525G01 datasheet could not be
// retrieved to confirm it. If one click moves the menu twice, this wants 4; if it takes two
// clicks per move, 1.
#define INPUTDRIVER_ENCODER_STEPS_PER_DETENT 2

// Which of IO1/IO2 is channel A is undocumented and only sets the direction of rotation.
// Uncomment if turning clockwise scrolls the wrong way.
// #define INPUTDRIVER_ENCODER_INVERT 1

// BaseUI: ALT_PRESS/USER_PRESS step between frames at the top level (Screen.cpp) and are also
// accepted as up/down inside menus (NotificationRenderer.cpp), whereas plain UP/DOWN on the home
// frame launches canned messages - surprising for a knob. Same choice UpDownInterruptImpl1 makes.
#define INPUTDRIVER_ENCODER_EVENT_CW INPUT_BROKER_USER_PRESS
#define INPUTDRIVER_ENCODER_EVENT_CCW INPUT_BROKER_ALT_PRESS

// BUTTON_PIN (GPIO45) is not an RTC IO, so esp_sleep_enable_ext1_wakeup() rejects it and a user
// shutdown would leave the board unwakeable short of a reset. The encoder's push switch is
// RTC-capable, so wake on that instead. GPIO45 keeps every other role, long-press shutdown included.
#define DEEP_SLEEP_WAKE_PIN INPUTDRIVER_ENCODER_BTN
// ext1 defaults to ANY_LOW, but this switch idles low - that would mean "already awake" and the
// node could never stay shut down. Wake on the rising edge of a press instead.
#define ESP32S3_WAKE_TYPE ESP_EXT1_WAKEUP_ANY_HIGH
#endif
