// WebUSB/wasm-compatible drop-in for libch341-spi-userspace's public header.
//
// Same API surface as the upstream libpinedio-usb.h that the firmware's
// Ch341Hal (src/platform/portduino/USBHal.h) compiles against, but WITHOUT the
// libusb / pthread dependencies - the implementation (libpinedio_webusb.c)
// forwards to a JS WebUSB bridge via Emscripten. The struct keeps only the
// fields Ch341Hal actually touches (serial_number, product_string, in_error,
// options[]); GPIO/CS state now lives on the JS side.
#ifndef PINEDIO_USB_WEBUSB_H
#define PINEDIO_USB_WEBUSB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

enum pinedio_int_pin {
    PINEDIO_PIN_D0,
    PINEDIO_PIN_D1,
    PINEDIO_PIN_D2,
    PINEDIO_PIN_D3,
    PINEDIO_PIN_D4,
    PINEDIO_PIN_D5,
    PINEDIO_PIN_D6,
    PINEDIO_PIN_D7,
    PINEDIO_PIN_ERR,
    PINEDIO_PIN_PEMP,
    PINEDIO_PIN_INT,
    PINEDIO_INT_PIN_MAX
};

enum pinedio_int_mode {
    PINEDIO_INT_MODE_RISING = 0x01,
    PINEDIO_INT_MODE_FALLING = 0x02,
};

enum pinedio_option {
    PINEDIO_OPTION_AUTO_CS,
    PINEDIO_OPTION_SEARCH_SERIAL,
    PINEDIO_OPTION_VID,
    PINEDIO_OPTION_PID,
    PINEDIO_OPTION_MAX
};

struct pinedio_inst_int {
    uint8_t previous_state;
    enum pinedio_int_mode mode;
    void (*callback)(void);
};

struct pinedio_inst {
    bool in_error;
    struct pinedio_inst_int interrupts[PINEDIO_INT_PIN_MAX];
    uint32_t options[PINEDIO_OPTION_MAX];
    char serial_number[9];
    char product_string[97];
};

typedef struct pinedio_inst pinedio_inst;

int32_t pinedio_init(struct pinedio_inst *inst, void *driver);
int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value);
int32_t pinedio_set_pin_mode(struct pinedio_inst *inst, uint32_t pin, uint32_t mode);
int32_t pinedio_digital_write(struct pinedio_inst *inst, uint32_t pin, bool active);
int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active);
int32_t pinedio_write_read(struct pinedio_inst *inst, uint8_t *writearr, uint32_t writecnt, uint8_t *readarr, uint32_t readcnt);
int32_t pinedio_transceive(struct pinedio_inst *inst, uint8_t *write_buf, uint8_t *read_buf, uint32_t count);
int32_t pinedio_digital_read(struct pinedio_inst *inst, uint32_t pin);
int32_t pinedio_get_irq_state(struct pinedio_inst *inst, uint32_t pin);
int32_t pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                                 void (*callback)(void));
int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin);
void pinedio_deinit(struct pinedio_inst *inst);

#ifdef __cplusplus
}
#endif
#endif // PINEDIO_USB_WEBUSB_H
