// libpinedio API implemented over WebUSB (Emscripten), replacing the libusb
// backend for the wasm build. Each SPI/GPIO operation forwards to a JS bridge
// (Module.ch341, see wasm/bridge.js) that owns the actual USBDevice and reuses
// the framing in src/protocol.js.
//
// KEY DESIGN POINTS
//  * EM_ASYNC_JS makes a *synchronous-looking* C call await a WebUSB Promise.
//    This requires linking with Asyncify (or JSPI). One async suspend per SPI
//    transfer (not per packet) keeps the Asyncify cost down.
//  * The "single outstanding USB op" invariant is automatic: every C call here
//    awaits a complete JS operation before returning, and the node is
//    single-threaded-cooperative, so no transfer overlaps another.
//  * attachInterrupt does NOT spawn a thread (the upstream lib polls a pthread
//    over USB). We record the callback only and rely on the firmware's
//    pollMissedIrqs()/IRQ-flag polling from the main loop instead.
//  * After any `await`, the wasm heap may have grown (ALLOW_MEMORY_GROWTH), so
//    the JS bridge MUST re-read Module.HEAPU8 when writing results back.

#include "libpinedio-usb.h"
#include <emscripten.h>
#include <string.h>

// ---- JS bridge imports ------------------------------------------------------
// Async (suspend) operations:

EM_ASYNC_JS(int, webusb_open, (int vid, int pid, int serialPtr), {
    const serial = serialPtr ? UTF8ToString(serialPtr) : "";
    return await Module.ch341.open(vid, pid, serial);
});

EM_ASYNC_JS(int, webusb_transceive, (int writePtr, int readPtr, int count),
            { return await Module.ch341.transceive(writePtr, readPtr, count); });

EM_ASYNC_JS(int, webusb_digital_write, (int pin, int value), { return await Module.ch341.digitalWrite(pin, value); });

EM_ASYNC_JS(int, webusb_digital_read, (int pin), { return await Module.ch341.digitalRead(pin); });

EM_ASYNC_JS(void, webusb_close, (void), {
    if (Module.ch341)
        await Module.ch341.close();
});

// Synchronous (no USB) operations:

EM_JS(void, webusb_set_pin_mode, (int pin, int output), { Module.ch341.setPinMode(pin, !!output); });
EM_JS(void, webusb_set_auto_cs, (int enabled), { Module.ch341.setAutoCS(!!enabled); });
EM_JS(void, webusb_get_serial, (int ptr, int max), { Module.ch341.getSerial(ptr, max); });
EM_JS(void, webusb_get_product, (int ptr, int max), { Module.ch341.getProduct(ptr, max); });

// ---- libpinedio API ---------------------------------------------------------

int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value)
{
    if (option < PINEDIO_OPTION_MAX)
        inst->options[option] = value;
    if (option == PINEDIO_OPTION_AUTO_CS)
        webusb_set_auto_cs((int)value);
    return 0;
}

int32_t pinedio_init(struct pinedio_inst *inst, void *driver)
{
    (void)driver;
    inst->in_error = false;
    for (int i = 0; i < PINEDIO_INT_PIN_MAX; i++)
        inst->interrupts[i].callback = NULL;

    uint32_t vid = inst->options[PINEDIO_OPTION_VID] ? inst->options[PINEDIO_OPTION_VID] : 0x1A86;
    uint32_t pid = inst->options[PINEDIO_OPTION_PID] ? inst->options[PINEDIO_OPTION_PID] : 0x5512;
    int serialPtr = inst->options[PINEDIO_OPTION_SEARCH_SERIAL] ? (int)inst->serial_number : 0;

    int ret = webusb_open((int)vid, (int)pid, serialPtr);
    if (ret != 0) {
        inst->in_error = true;
        return ret < 0 ? ret : -2;
    }
    // Honor the configured Auto-CS. Ch341Hal sets PINEDIO_OPTION_AUTO_CS=0 (CS is
    // left to RadioLib's NSS, which drives it active-low correctly); it has not
    // been set yet at init, so this defaults off until Ch341Hal applies it.
    webusb_set_auto_cs(inst->options[PINEDIO_OPTION_AUTO_CS] ? 1 : 0);
    webusb_get_serial((int)inst->serial_number, sizeof(inst->serial_number));
    webusb_get_product((int)inst->product_string, sizeof(inst->product_string));
    return 0;
}

int32_t pinedio_set_pin_mode(struct pinedio_inst *inst, uint32_t pin, uint32_t mode)
{
    (void)inst;
    webusb_set_pin_mode((int)pin, (int)mode);
    return 0;
}

int32_t pinedio_digital_write(struct pinedio_inst *inst, uint32_t pin, bool active)
{
    int ret = webusb_digital_write((int)pin, active ? 1 : 0);
    if (ret < 0)
        inst->in_error = true;
    return ret;
}

int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active)
{
    return pinedio_digital_write(inst, 0, active); // D0 is CS
}

int32_t pinedio_transceive(struct pinedio_inst *inst, uint8_t *write_buf, uint8_t *read_buf, uint32_t count)
{
    int ret = webusb_transceive((int)write_buf, (int)read_buf, (int)count);
    if (ret < 0) {
        inst->in_error = true;
        return -1;
    }
    return 0;
}

int32_t pinedio_write_read(struct pinedio_inst *inst, uint8_t *writearr, uint32_t writecnt, uint8_t *readarr, uint32_t readcnt)
{
    // Not on Ch341Hal's hot path; emulate with a single full-duplex transfer.
    uint32_t total = writecnt + readcnt;
    uint8_t buf[total]; // VLA; transfers here are small (register reads)
    memcpy(buf, writearr, writecnt);
    memset(buf + writecnt, 0, readcnt);
    int ret = webusb_transceive((int)buf, (int)buf, (int)total);
    if (ret < 0) {
        inst->in_error = true;
        return -1;
    }
    memcpy(readarr, buf + writecnt, readcnt);
    return 0;
}

int32_t pinedio_digital_read(struct pinedio_inst *inst, uint32_t pin)
{
    int ret = webusb_digital_read((int)pin);
    if (ret < 0) {
        inst->in_error = true;
        return ret;
    }
    return ret;
}

int32_t pinedio_get_irq_state(struct pinedio_inst *inst, uint32_t pin)
{
    return pinedio_digital_read(inst, pin);
}

// No poll thread: record the callback so enable/disableInterrupt bookkeeping in
// RadioLib works; actual RX/TX detection is by polling IRQ flags in the loop.
int32_t pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                                 void (*callback)(void))
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;
    inst->interrupts[int_pin].previous_state = 255;
    inst->interrupts[int_pin].mode = int_mode;
    inst->interrupts[int_pin].callback = callback;
    return 0;
}

int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin)
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;
    inst->interrupts[int_pin].callback = NULL;
    return 0;
}

void pinedio_deinit(struct pinedio_inst *inst)
{
    webusb_close();
    inst->in_error = false;
}
