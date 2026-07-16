// libpinedio API implemented over WCH's CH341DLL, replacing the libusb backend
// on Windows. Mirrors what wasm/libpinedio_webusb.c does for the browser.
//
// WHY NOT LIBUSB: libusb on Windows can only talk to a device bound to
// WinUSB/libusbK/libusb0, so it needs Zadig to replace the driver by hand on
// every machine, and Zadig installs a self-signed CA into the trust store.
// CH341DLL ships with WCH's signed CH341PAR driver and a normal installer.
//
// The DLL is closed-source and not redistributable, so it is resolved at
// runtime: with no CH341PAR installed, pinedio_init() fails and the caller
// (Ch341Hal's constructor) throws, which is the same path a missing device
// takes. Nothing else in the build depends on it.
//
// Pin numbering is the CH341's D0-D7, as upstream. For the PineDio adapter the
// YAML gives CS: 0, IRQ: 6, Reset: 2 against D3=SCK, D5=MOSI, D7=MISO.

#if defined(ARCH_PORTDUINO) && defined(_WIN32)

#include "libpinedio-usb.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// CH341Set_D5_D0 only reaches D0-D5. That covers every output the adapter uses
// (CS/Reset/SCK/MOSI); D6 (IRQ) and D7 (MISO) are inputs, read via CH341GetInput.
#define CH341_MAX_OUTPUT_PIN 5

// iMode bit 7: SPI bit order, 1 = MSB first. The SX1262 is MSB-first, and doing
// it in hardware avoids upstream's per-byte reverse_byte() over the whole buffer.
#define CH341_STREAM_MODE_SPI_MSB_FIRST 0x80

// Matches upstream's poll interval, so IRQ latency is the same as on Linux.
#define PIN_POLL_INTERVAL_MS (1000 / 30)

typedef HANDLE(WINAPI *ch341_open_t)(ULONG);
typedef VOID(WINAPI *ch341_close_t)(ULONG);
typedef BOOL(WINAPI *ch341_set_stream_t)(ULONG, ULONG);
typedef BOOL(WINAPI *ch341_stream_spi4_t)(ULONG, ULONG, ULONG, PVOID);
typedef BOOL(WINAPI *ch341_set_d5_d0_t)(ULONG, ULONG, ULONG);
typedef BOOL(WINAPI *ch341_get_input_t)(ULONG, PULONG);
typedef PVOID(WINAPI *ch341_get_device_name_t)(ULONG);

static struct {
    HMODULE dll;
    ch341_open_t open;
    ch341_close_t close;
    ch341_set_stream_t set_stream;
    ch341_stream_spi4_t stream_spi4;
    ch341_set_d5_d0_t set_d5_d0;
    ch341_get_input_t get_input;
    ch341_get_device_name_t get_device_name;
} ch341;

// Single adapter, matching Ch341Hal's spiChannel of 0. Multiple sticks would
// need this and the device index threaded through pinedio_inst.
static ULONG ch341_index = 0;

// D0-D5 direction and output state, applied together by CH341Set_D5_D0.
static ULONG pin_dir_out = 0;
static ULONG pin_state = 0;

// Serializes DLL access between the caller and the poll thread.
static pthread_mutex_t usb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t poll_thread;
static volatile bool poll_thread_exit = false;
static int int_running_cnt = 0;

// CH341PAR installs the 64-bit library as CH341DLLA64.DLL in System32 and the
// 32-bit one as CH341DLL.DLL in SysWOW64, so a 64-bit process must ask for the
// A64 name; plain CH341DLL.DLL would either not be found or be the wrong
// architecture. Try the matching name first and fall back to the other.
static const char *const ch341_dll_names[] = {
#ifdef _WIN64
    "CH341DLLA64.DLL",
    "CH341DLL.DLL",
#else
    "CH341DLL.DLL",
    "CH341DLLA64.DLL",
#endif
};

static bool load_dll(void)
{
    if (ch341.dll)
        return true;
    for (size_t i = 0; i < sizeof(ch341_dll_names) / sizeof(ch341_dll_names[0]); i++) {
        ch341.dll = LoadLibraryA(ch341_dll_names[i]);
        if (ch341.dll)
            break;
    }
    if (!ch341.dll) {
        fprintf(stderr, "CH341DLL not found; install the WCH CH341PAR driver\n");
        return false;
    }
    ch341.open = (ch341_open_t)(void *)GetProcAddress(ch341.dll, "CH341OpenDevice");
    ch341.close = (ch341_close_t)(void *)GetProcAddress(ch341.dll, "CH341CloseDevice");
    ch341.set_stream = (ch341_set_stream_t)(void *)GetProcAddress(ch341.dll, "CH341SetStream");
    ch341.stream_spi4 = (ch341_stream_spi4_t)(void *)GetProcAddress(ch341.dll, "CH341StreamSPI4");
    ch341.set_d5_d0 = (ch341_set_d5_d0_t)(void *)GetProcAddress(ch341.dll, "CH341Set_D5_D0");
    ch341.get_input = (ch341_get_input_t)(void *)GetProcAddress(ch341.dll, "CH341GetInput");
    ch341.get_device_name = (ch341_get_device_name_t)(void *)GetProcAddress(ch341.dll, "CH341GetDeviceName");

    if (!ch341.open || !ch341.close || !ch341.set_stream || !ch341.stream_spi4 || !ch341.set_d5_d0 || !ch341.get_input) {
        fprintf(stderr, "CH341DLL is missing expected exports\n");
        FreeLibrary(ch341.dll);
        ch341.dll = NULL;
        return false;
    }
    return true;
}

static int32_t apply_pins(void)
{
    if (!ch341.set_d5_d0(ch341_index, pin_dir_out, pin_state))
        return -1;
    return 0;
}

int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value)
{
    if (option < PINEDIO_OPTION_MAX)
        inst->options[option] = value;
    return 0;
}

int32_t pinedio_init(struct pinedio_inst *inst, void *driver)
{
    (void)driver;
    inst->in_error = false;
    for (int i = 0; i < PINEDIO_INT_PIN_MAX; i++)
        inst->interrupts[i].callback = NULL;

    if (!load_dll())
        return -1;

    if (ch341.open(ch341_index) == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CH341OpenDevice(%lu) failed; is the adapter plugged in?\n", ch341_index);
        return -2;
    }

    // Default speed (bits 0-1 = 01) plus MSB-first.
    if (!ch341.set_stream(ch341_index, 0x01 | CH341_STREAM_MODE_SPI_MSB_FIRST)) {
        fprintf(stderr, "CH341SetStream failed\n");
        ch341.close(ch341_index);
        return -3;
    }

    pin_dir_out = 0;
    pin_state = 0;

    // CH341DLL exposes no USB serial string. Ch341Hal only uses serial_number to
    // pick between multiple adapters and to derive a MAC; on Windows getMacAddr()
    // falls back to the host adapter, so leaving it empty is safe. The device name
    // is the closest stand-in for the product string.
    inst->serial_number[0] = '\0';
    inst->product_string[0] = '\0';
    if (ch341.get_device_name) {
        const char *name = (const char *)ch341.get_device_name(ch341_index);
        if (name) {
            strncpy(inst->product_string, name, sizeof(inst->product_string) - 1);
            inst->product_string[sizeof(inst->product_string) - 1] = '\0';
        }
    }
    return 0;
}

int32_t pinedio_set_pin_mode(struct pinedio_inst *inst, uint32_t pin, uint32_t mode)
{
    (void)inst;
    if (pin > CH341_MAX_OUTPUT_PIN)
        return 0; // D6/D7 are input-only; nothing to configure

    // Under the lock: the poll thread's callback can drive GPIO concurrently.
    pthread_mutex_lock(&usb_mutex);
    if (mode)
        pin_dir_out |= (1u << pin);
    else
        pin_dir_out &= ~(1u << pin);
    pthread_mutex_unlock(&usb_mutex);
    // Upstream defers the device write to the next digital_write; do the same so
    // the direction and level land together.
    return 0;
}

int32_t pinedio_digital_write(struct pinedio_inst *inst, uint32_t pin, bool active)
{
    if (pin > CH341_MAX_OUTPUT_PIN)
        return -1;

    // Mutate and apply as one critical section, or a concurrent write loses its
    // update when apply_pins() reads a half-updated pin_state.
    pthread_mutex_lock(&usb_mutex);
    if (active)
        pin_state |= (1u << pin);
    else
        pin_state &= ~(1u << pin);
    int32_t ret = apply_pins();
    pthread_mutex_unlock(&usb_mutex);
    if (ret < 0)
        inst->in_error = true;
    return ret;
}

int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active)
{
    return pinedio_digital_write(inst, 0, active); // D0 is CS
}

static int32_t read_input(uint32_t *out)
{
    ULONG status = 0;
    if (!ch341.get_input(ch341_index, &status))
        return -1;
    *out = (uint32_t)status;
    return 0;
}

int32_t pinedio_digital_read(struct pinedio_inst *inst, uint32_t pin)
{
    uint32_t status = 0;
    pthread_mutex_lock(&usb_mutex);
    int32_t ret = read_input(&status);
    pthread_mutex_unlock(&usb_mutex);
    if (ret < 0) {
        inst->in_error = true;
        return ret;
    }
    return (status & (1u << pin)) ? 1 : 0; // bits 7-0 are D7-D0
}

int32_t pinedio_get_irq_state(struct pinedio_inst *inst, uint32_t pin)
{
    return pinedio_digital_read(inst, pin);
}

int32_t pinedio_transceive(struct pinedio_inst *inst, uint8_t *write_buf, uint8_t *read_buf, uint32_t count)
{
    if (count == 0)
        return 0;

    // CH341StreamSPI4 is full duplex over one in/out buffer.
    uint8_t stack_buf[64];
    uint8_t *buf = count <= sizeof(stack_buf) ? stack_buf : (uint8_t *)malloc(count);
    if (!buf)
        return -1;
    memcpy(buf, write_buf, count);

    // Bit 7 clear: leave chip select alone. Ch341Hal sets PINEDIO_OPTION_AUTO_CS
    // to 0 and lets RadioLib drive NSS through digitalWrite.
    ULONG cs = 0;
    if (inst->options[PINEDIO_OPTION_AUTO_CS])
        cs = 0x80; // enable, D0 active low

    pthread_mutex_lock(&usb_mutex);
    BOOL ok = ch341.stream_spi4(ch341_index, cs, count, buf);
    pthread_mutex_unlock(&usb_mutex);

    if (ok && read_buf)
        memcpy(read_buf, buf, count);
    if (buf != stack_buf)
        free(buf);

    if (!ok) {
        inst->in_error = true;
        return -1;
    }
    return 0;
}

int32_t pinedio_write_read(struct pinedio_inst *inst, uint8_t *writearr, uint32_t writecnt, uint8_t *readarr, uint32_t readcnt)
{
    uint32_t total = writecnt + readcnt;
    uint8_t stack_buf[64];
    uint8_t *buf = total <= sizeof(stack_buf) ? stack_buf : (uint8_t *)malloc(total);
    if (!buf)
        return -1;
    memcpy(buf, writearr, writecnt);
    memset(buf + writecnt, 0, readcnt);

    int32_t ret = pinedio_transceive(inst, buf, buf, total);
    if (ret == 0)
        memcpy(readarr, buf + writecnt, readcnt);
    if (buf != stack_buf)
        free(buf);
    return ret;
}

// The CH341's own interrupt (CH341SetIntRoutine) only fires on its INT# pin, but
// the adapter wires the radio's DIO1 to D6, so it can't be used here. Poll D6
// instead, at upstream's rate and with upstream's edge semantics.
static void *pin_poll_thread_fn(void *arg)
{
    struct pinedio_inst *inst = (struct pinedio_inst *)arg;
    bool should_exit = false;

    while (!should_exit) {
        uint32_t input = 0;
        pthread_mutex_lock(&usb_mutex);
        int32_t ret = read_input(&input);
        pthread_mutex_unlock(&usb_mutex);
        if (ret < 0) {
            inst->in_error = true;
            fprintf(stderr, "CH341 poll: failed to read input\n");
            break;
        }
        inst->in_error = false;

        pthread_mutex_lock(&usb_mutex);
        for (uint8_t int_pin = 0; int_pin < PINEDIO_INT_PIN_MAX; int_pin++) {
            struct pinedio_inst_int *inst_int = &inst->interrupts[int_pin];
            // Copy the pointer while holding the lock: a concurrent
            // pinedio_deattach_interrupt() could NULL it once we drop the lock
            // to make the call.
            void (*cb)(void) = inst_int->callback;
            if (cb == NULL)
                continue;
            uint8_t state = (input & (1u << int_pin)) != 0;
            if (inst_int->previous_state != 255 && inst_int->previous_state != state) {
                enum pinedio_int_mode mode =
                    (!inst_int->previous_state && state) ? PINEDIO_INT_MODE_RISING : PINEDIO_INT_MODE_FALLING;
                if (inst_int->mode & mode) {
                    // Callback re-enters this library, so drop the lock first.
                    pthread_mutex_unlock(&usb_mutex);
                    cb();
                    pthread_mutex_lock(&usb_mutex);
                }
            }
            inst_int->previous_state = state;
        }
        should_exit = poll_thread_exit;
        pthread_mutex_unlock(&usb_mutex);
        Sleep(PIN_POLL_INTERVAL_MS);
    }
    return NULL;
}

int32_t pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                                 void (*callback)(void))
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;

    int32_t res = 0;
    pthread_mutex_lock(&usb_mutex);
    bool was_attached = inst->interrupts[int_pin].callback != NULL;
    inst->interrupts[int_pin].previous_state = 255;
    inst->interrupts[int_pin].mode = int_mode;
    inst->interrupts[int_pin].callback = callback;

    // Only a new attachment changes the refcount. Re-attaching an already-armed
    // pin (RadioLib does this) must not drop the count to 0 and spawn a second
    // poll thread alongside the running one.
    if (!was_attached) {
        if (int_running_cnt == 0) {
            poll_thread_exit = false;
            res = pthread_create(&poll_thread, NULL, pin_poll_thread_fn, inst);
            if (res != 0) {
                fprintf(stderr, "CH341: failed to start poll thread: %d\n", res);
                inst->interrupts[int_pin].callback = NULL;
                pthread_mutex_unlock(&usb_mutex);
                return res;
            }
        }
        int_running_cnt++;
    }
    pthread_mutex_unlock(&usb_mutex);
    return res;
}

int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin)
{
    if (int_pin >= PINEDIO_INT_PIN_MAX)
        return -1;

    pthread_t thread_to_join;
    pthread_mutex_lock(&usb_mutex);
    bool was_attached = inst->interrupts[int_pin].callback != NULL;
    inst->interrupts[int_pin].callback = NULL;
    if (was_attached)
        int_running_cnt--;
    bool stop = was_attached && int_running_cnt == 0;
    if (stop) {
        poll_thread_exit = true;
        // Copy the handle: a concurrent attach could overwrite poll_thread once
        // the lock is dropped, and we would join the wrong thread.
        thread_to_join = poll_thread;
    }
    pthread_mutex_unlock(&usb_mutex);

    // Joining under the lock would deadlock against the poll thread taking it.
    if (stop && !pthread_equal(thread_to_join, pthread_self()))
        pthread_join(thread_to_join, NULL);
    return 0;
}

void pinedio_deinit(struct pinedio_inst *inst)
{
    pthread_t thread_to_join;
    pthread_mutex_lock(&usb_mutex);
    bool stop = int_running_cnt > 0;
    poll_thread_exit = true;
    int_running_cnt = 0;
    if (stop)
        thread_to_join = poll_thread; // copy before dropping the lock, as above
    pthread_mutex_unlock(&usb_mutex);

    if (stop && !pthread_equal(thread_to_join, pthread_self()))
        pthread_join(thread_to_join, NULL);

    if (ch341.dll)
        ch341.close(ch341_index);
    inst->in_error = false;
}

#endif // ARCH_PORTDUINO && _WIN32
