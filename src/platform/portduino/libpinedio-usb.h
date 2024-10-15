// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2024 Marek Kraus (@gamelaster)

#ifndef PINEDIO_USB_LORA_ADAPTER_SDK_LIBPINEDIO_USB_H
#define PINEDIO_USB_LORA_ADAPTER_SDK_LIBPINEDIO_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define USB_IN_TRANSFERS 32

enum pinedio_int_pin {
    PINEDIO_INT_PIN_IRQ,
    //  PINEDIO_INT_PIN_BUSY, // not implemented yet
    PINEDIO_INT_PIN_MAX
};

enum pinedio_int_mode {
    PINEDIO_INT_MODE_RISING = 0x01,
    PINEDIO_INT_MODE_FALLING = 0x02,
};

enum pinedio_option { PINEDIO_OPTION_AUTO_CS, PINEDIO_OPTION_MAX };

struct pinedio_inst_int {
    uint8_t previous_state;
    enum pinedio_int_mode mode;
    void (*callback)(void);
};

/* Even this will work mostly on desktop (so malloc is available), I still prefer static allocation.
 * That's why structure definition is in header. */

struct pinedio_inst {
    struct libusb_device_handle *handle;

    /* We need to use many queued IN transfers for any resemblance of performance (especially on Windows)
     * because USB spec says that transfers end on non-full packets and the device sends the 31 reply
     * data bytes to each 32-byte packet with command + 31 bytes of data... */
    struct libusb_transfer *transfer_out;
    struct libusb_transfer *transfer_ins[USB_IN_TRANSFERS];
    uint8_t int_running_cnt;
    pthread_mutex_t usb_access_mutex;
    pthread_t pin_poll_thread;
    bool pin_poll_thread_exit;
    struct pinedio_inst_int interrupts[PINEDIO_INT_PIN_MAX];
    uint32_t options[PINEDIO_OPTION_MAX];
};

int32_t pinedio_init(struct pinedio_inst *inst, void *driver);
int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value);
int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active);
int32_t pinedio_write_read(struct pinedio_inst *inst, uint8_t *writearr, uint32_t writecnt, uint8_t *readarr, uint32_t readcnt);
int32_t pinedio_transceive(struct pinedio_inst *inst, uint8_t *write_buf, uint8_t *read_buf, uint32_t count);
int32_t pinedio_get_irq_state(struct pinedio_inst *inst);
int32_t pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                                 void (*callback)(void));
int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin);
void pinedio_deinit(struct pinedio_inst *inst);

#ifdef __cplusplus
}
#endif

#endif // PINEDIO_USB_LORA_ADAPTER_SDK_LIBPINEDIO_USB_H