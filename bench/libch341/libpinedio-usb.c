// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Copyright (C) 2024 Marek Kraus <gamelaster@outlook.com>
 *
 * This code is heavily based on ch341a_spi.c from the flashrom project.
 * The plan is to rework parts of code, but until that, original developers deserves to be mentioned.
 * Copyright (C) 2011 asbokid <ballymunboy@gmail.com>
 * Copyright (C) 2014 Pluto Yang <yangyj.ee@gmail.com>
 * Copyright (C) 2015-2016 Stefan Tauner
 * Copyright (C) 2015 Urja Rannikko <urjaman@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "libpinedio-usb.h"

#if 0
#define pinedio_mutex_lock(...) { printf("Locking %s\n", __func__); pthread_mutex_lock(__VA_ARGS__); }
#define pinedio_mutex_unlock(...) { printf("Unlocking %s\n", __func__); pthread_mutex_unlock(__VA_ARGS__); }
#else
#define pinedio_mutex_lock(...) pthread_mutex_lock(__VA_ARGS__);
#define pinedio_mutex_unlock(...) pthread_mutex_unlock(__VA_ARGS__);
#endif

#define CH341_USB_TIMEOUT 1000
#define CH341_WRITE_EP 0x02
#define CH341_READ_EP 0x82
#define	CH341_PACKET_LENGTH	0x20

#define CH341_CMD_SPI_STREAM 0xA8

#define CH341_CMD_UIO_STREAM 0xAB
#define CH341_CMD_UIO_STM_OUT 0x80
#define CH341_CMD_UIO_STM_DIR 0x40
#define CH341_CMD_UIO_STM_END 0x20

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

// store mode and state of d0-d7
uint16_t pinedio_d_mode = 0;
uint16_t pinedio_d_state = 0;

enum trans_state {TRANS_ACTIVE = -2, TRANS_ERR = -1, TRANS_IDLE = 0};

static void cb_common(const char* func, struct libusb_transfer *transfer) {
  int* transfer_cnt = (int *) transfer->user_data;

  if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
    // Silently ACK and exit.
    *transfer_cnt = TRANS_IDLE;
    return;
  }

  if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
    fprintf(stderr, "%s: error: %s\n", func, libusb_error_name(transfer->status));
    *transfer_cnt = TRANS_ERR;
  } else {
    *transfer_cnt = transfer->actual_length;
  }
}

// callback for bulk out async transfer
static void LIBUSB_CALL cb_out(struct libusb_transfer *transfer) {
  cb_common(__func__, transfer);
}

// callback for bulk in async transfer
static void LIBUSB_CALL cb_in(struct libusb_transfer *transfer) {
  cb_common(__func__, transfer);
}

static int32_t usb_transfer(struct pinedio_inst *inst, const char *func, unsigned int writecnt, unsigned int readcnt,
                            const uint8_t *writearr, uint8_t *readarr, bool lock)
{
  int state_out = TRANS_IDLE;

  if (lock) {
    pinedio_mutex_lock(&inst->usb_access_mutex);
  }

  inst->transfer_out->buffer = (uint8_t*)writearr;
  inst->transfer_out->length = writecnt;
  inst->transfer_out->user_data = &state_out;

  /* Schedule write first */
  if (writecnt > 0) {
    state_out = TRANS_ACTIVE;
    int ret = libusb_submit_transfer(inst->transfer_out);
    if (ret) {
      fprintf(stderr, "%s: failed to submit OUT transfer: %s\n", func, libusb_error_name(ret));
      state_out = TRANS_ERR;
      goto err;
    }
  }

  /* Handle all asynchronous packets as long as we have stuff to write or read. The write(s) simply need
   * to complete but we need to scheduling reads as long as we are not done. */
  unsigned int free_idx = 0; /* The IN transfer we expect to be free next. */
  unsigned int in_idx = 0; /* The IN transfer we expect to be completed next. */
  unsigned int in_done = 0;
  unsigned int in_active = 0;
  unsigned int out_done = 0;
  uint8_t *in_buf = readarr;
  int state_in[USB_IN_TRANSFERS] = {0};
  do {
    /* Schedule new reads as long as there are free transfers and unscheduled bytes to read. */
    while ((in_done + in_active) < readcnt && state_in[free_idx] == TRANS_IDLE) {
      unsigned int cur_todo = MIN(CH341_PACKET_LENGTH - 1, readcnt - in_done - in_active);
      inst->transfer_ins[free_idx]->length = cur_todo;
      inst->transfer_ins[free_idx]->buffer = in_buf;
      inst->transfer_ins[free_idx]->user_data = &state_in[free_idx];
      int ret = libusb_submit_transfer(inst->transfer_ins[free_idx]);
      if (ret) {
        state_in[free_idx] = TRANS_ERR;
        fprintf(stderr, "%s: failed to submit IN transfer: %s\n",
                 func, libusb_error_name(ret));
        goto err;
      }
      in_buf += cur_todo;
      in_active += cur_todo;
      state_in[free_idx] = TRANS_ACTIVE;
      free_idx = (free_idx + 1) % USB_IN_TRANSFERS; /* Increment (and wrap around). */
    }

    /* Actually get some work done. */
    libusb_handle_events_timeout(NULL, &(struct timeval){1, 0});

    /* Check for the write */
    if (out_done < writecnt) {
      if (state_out == TRANS_ERR) {
        goto err;
      } else if (state_out > 0) {
        out_done += state_out;
        state_out = TRANS_IDLE;
      }
    }
    /* Check for completed transfers. */
    while (state_in[in_idx] != TRANS_IDLE && state_in[in_idx] != TRANS_ACTIVE) {
      if (state_in[in_idx] == TRANS_ERR) {
        goto err;
      }
      /* If a transfer is done, record the number of bytes read and reuse it later. */
      in_done += state_in[in_idx];
      in_active -= state_in[in_idx];
      state_in[in_idx] = TRANS_IDLE;
      in_idx = (in_idx + 1) % USB_IN_TRANSFERS; /* Increment (and wrap around). */
    }
  } while ((out_done < writecnt) || (in_done < readcnt));

  if (lock) {
    pinedio_mutex_unlock(&inst->usb_access_mutex);
  }
  return 0;
err:
  /* Clean up on errors. */
  fprintf(stderr, "%s: Failed to %s %d bytes\n", func, (state_out == TRANS_ERR) ? "write" : "read",
           (state_out == TRANS_ERR) ? writecnt : readcnt);
  /* First, we must cancel any ongoing requests and wait for them to be canceled. */
  if ((writecnt > 0) && (state_out == TRANS_ACTIVE)) {
    if (libusb_cancel_transfer(inst->transfer_out) != 0)
      state_out = TRANS_ERR;
  }
  if (readcnt > 0) {
    unsigned int i;
    for (i = 0; i < USB_IN_TRANSFERS; i++) {
      if (state_in[i] == TRANS_ACTIVE)
        if (libusb_cancel_transfer(inst->transfer_ins[i]) != 0)
          state_in[i] = TRANS_ERR;
    }
  }

  /* Wait for cancellations to complete. */
  while (1) {
    bool finished = true;
    if ((writecnt > 0) && (state_out == TRANS_ACTIVE))
      finished = false;
    if (readcnt > 0) {
      unsigned int i;
      for (i = 0; i < USB_IN_TRANSFERS; i++) {
        if (state_in[i] == TRANS_ACTIVE)
          finished = false;
      }
    }
    if (finished)
      break;
    libusb_handle_events_timeout(NULL, &(struct timeval){1, 0});
  }
  if (lock) {
    pinedio_mutex_unlock(&inst->usb_access_mutex);
  }
  return -1;
}

static uint8_t reverse_byte(uint8_t x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);

  return x;
}

#define PIN_POLL_INTERVAL_DEFAULT_US ((1000 / 30) * 1000L)

/* Bench only: PINEDIO_POLL_INTERVAL_US in the environment sets the sleep between pin polls, 0 to 1000000
 * us, so one binary can run at the stock 30 Hz or poll faster. The interrupt latency is up to one
 * interval plus one USB read. Read once, in pinedio_init(). */
static long pin_poll_interval_us = PIN_POLL_INTERVAL_DEFAULT_US;

static void pinedio_read_poll_interval(void) {
  const char *env = getenv("PINEDIO_POLL_INTERVAL_US");
  if (env == NULL || *env == '\0')
    return;
  char *end;
  errno = 0;
  long us = strtol(env, &end, 10);
  if (errno != 0 || *end != '\0' || us < 0 || us > 1000000L) {
    fprintf(stderr, "libch341: ignoring PINEDIO_POLL_INTERVAL_US=%s, keeping %ld us\n", env, pin_poll_interval_us);
    return;
  }
  pin_poll_interval_us = us;
  fprintf(stderr, "libch341: pin poll interval %ld us\n", pin_poll_interval_us);
}

int32_t pinedio_init(struct pinedio_inst *inst, void *driver) {
  int32_t ret;
  inst->int_running_cnt = 0;
  inst->pin_poll_thread_exit = false;
  for (int i = 0; i < PINEDIO_INT_PIN_MAX; i++) {
    inst->interrupts[i].callback = NULL;
  }

  inst->options[PINEDIO_OPTION_AUTO_CS] = 1;
  pinedio_read_poll_interval();

  ret = pthread_mutex_init(&inst->usb_access_mutex, NULL);
  if (ret != 0) {
    fprintf(stderr, "Failed to initialize mutex, res: %d.\n", ret);
    return -1;
  }

  inst->pin_poll_threads_alive = 0;
  inst->deinit_started = false;
  ret = pthread_cond_init(&inst->pin_poll_thread_gone, NULL);
  if (ret != 0) {
    fprintf(stderr, "Failed to initialize condition variable, res: %d.\n", ret);
    return -1;
  }
  /* The poll sleep is a timed wait on this, so time it on the monotonic clock where we can: a wall
   * clock stepped back by NTP would otherwise stall interrupt polling for the size of the step. */
  pthread_condattr_t wake_attr;
  pthread_condattr_init(&wake_attr);
#ifdef __linux__
  pthread_condattr_setclock(&wake_attr, CLOCK_MONOTONIC);
#endif
  ret = pthread_cond_init(&inst->pin_poll_wake, &wake_attr);
  pthread_condattr_destroy(&wake_attr);
  if (ret != 0) {
    fprintf(stderr, "Failed to initialize condition variable, res: %d.\n", ret);
    return -1;
  }

  ret = libusb_init(NULL);
  if (ret < 0) {
    fprintf(stderr, "Couldn't initialize libusb!\n");
    return -1;
  }

  libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
  if (inst->options[PINEDIO_OPTION_VID] == 0) {
    inst->options[PINEDIO_OPTION_VID] = 0x1A86;
  }
  if (inst->options[PINEDIO_OPTION_PID] == 0) {
    inst->options[PINEDIO_OPTION_PID] = 0x5512;
  }

  // discover devices
  libusb_device **list;
  libusb_device *found = NULL;
  ssize_t cnt = libusb_get_device_list(NULL, &list);
  ssize_t i = 0;

  for (i = 0; i < cnt; i++) {
    libusb_device *device = list[i];
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(device, &desc);

    if (desc.idVendor == inst->options[PINEDIO_OPTION_VID] && desc.idProduct == inst->options[PINEDIO_OPTION_PID] ) {
      found = device;
      ret = libusb_open(found, &inst->handle);
      if (inst->handle != NULL) {

#ifdef __linux__
        // On Windows, driver needs to be replaced manually by Zadig
        ret = libusb_detach_kernel_driver(inst->handle, 0);
        if (ret != 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
          fprintf(stderr, "Cannot detach the existing USB driver. Claiming the interface may fail: %s\n",
                libusb_error_name(ret));
        }
#endif
        ret = libusb_claim_interface(inst->handle, 0);
        if (ret != 0) {
          fprintf(stderr, "Failed to claim interface 0: %s\n", libusb_error_name(ret));
          libusb_close(inst->handle);
          inst->handle = NULL;
        } else {
          char _serial[9];
          libusb_get_string_descriptor_ascii(inst->handle, desc.iSerialNumber, _serial, 9);
          libusb_get_string_descriptor(inst->handle, desc.iProduct, 0, inst->product_string, 96);
          if (inst->options[PINEDIO_OPTION_SEARCH_SERIAL] && strncmp(_serial, inst->serial_number, 8) != 0) {
            libusb_close(inst->handle);
            inst->handle = NULL;
          } else {
            strncpy(inst->serial_number, _serial, 9);
            break;
          }
        }
      }
    }
  }
  libusb_free_device_list(list, 1);

  if (inst->handle == NULL) {
    // TODO: Rework this so we can receive error and print it.
    fprintf(stderr, "Couldn't open LoRa USB device.\n");
    return -2;
  }

  // Allocate and pre-fill transfer structures.
  inst->transfer_out = libusb_alloc_transfer(0);
  if (!inst->transfer_out) {
    fprintf(stderr, "Failed to alloc libusb OUT transfer.\n");
    goto deinit_on_error;
  }
  for (int i = 0; i < USB_IN_TRANSFERS; i++) {
    inst->transfer_ins[i] = libusb_alloc_transfer(0);
    if (inst->transfer_ins[i] == NULL) {
      fprintf(stderr, "Failed to alloc libusb IN transfer %d.\n", i);
      goto deinit_on_error;
    }
  }

  // We use these helpers but don't fill the actual buffer yet.
  libusb_fill_bulk_transfer(inst->transfer_out, inst->handle, CH341_WRITE_EP, NULL, 0, cb_out, NULL, CH341_USB_TIMEOUT);
  for (int i = 0; i < USB_IN_TRANSFERS; i++)
    libusb_fill_bulk_transfer(inst->transfer_ins[i], inst->handle, CH341_READ_EP, NULL, 0, cb_in, NULL,
                              CH341_USB_TIMEOUT);

  /**
   * We don't need to initialize SPI at all, as by default it's configured properly.
   * Only thing required is pinmux, what is anyway configured by CS change function.
   */

  pinedio_set_cs(inst, false);

  return 0;

deinit_on_error:
  pinedio_deinit(inst);
  return ret;
}

int32_t pinedio_set_option(struct pinedio_inst *inst, enum pinedio_option option, uint32_t value) {
  inst->options[option] = value;
}

int32_t pinedio_set_pin_mode(struct pinedio_inst *inst, uint32_t pin, uint32_t mode) {
  /* Same shadow state as pinedio_digital_write(), same lock. The transfer below
   * is disabled, so the direction only reaches the chip on the next
   * digital_write() -- but the bitmask must still not be torn. */
  pinedio_mutex_lock(&inst->usb_access_mutex);
    if (mode == 1) { // output
    pinedio_d_mode |= (1 << pin);
  } else {
    pinedio_d_mode &= ~(1 << pin);
  }
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  uint8_t buf[] = {
          CH341_CMD_UIO_STREAM,
          CH341_CMD_UIO_STM_DIR | pinedio_d_mode, // enable output on d0-d5
          CH341_CMD_UIO_STM_END
  };

  int32_t ret = 0; //usb_transfer(inst, __func__, sizeof(buf), 0, buf, NULL, true);
  if (ret < 0) {
    printf("Failed to set CS pin.\n");
  }
  return ret;
}

int32_t pinedio_digital_write(struct pinedio_inst *inst, uint32_t pin, bool active) {
  /* Hold usb_access_mutex across the read-modify-write, the snapshot of the
   * shadow state into buf, and the transfer itself, so concurrent GPIO writes
   * cannot lose each other's bit or apply their packets out of order. The poll
   * thread runs the interrupt callback with the mutex released, and consumers
   * drive GPIO from that callback while the main thread does too, so this is
   * reachable. usb_transfer() is told not to take the lock again: it is a plain
   * mutex, not a recursive one. */
  pinedio_mutex_lock(&inst->usb_access_mutex);

  if (active) {
    pinedio_d_state |= (1 << pin);
  } else {
    pinedio_d_state &= ~(1 << pin);
  }
    uint8_t buf[] = {
          CH341_CMD_UIO_STREAM,
          CH341_CMD_UIO_STM_OUT | pinedio_d_state,  // bitfield controlling value of d0-d7 where the rightmost bit is d0
          CH341_CMD_UIO_STM_DIR | pinedio_d_mode,
          CH341_CMD_UIO_STM_END
  };

  int32_t ret = usb_transfer(inst, __func__, sizeof(buf), 0, buf, NULL, false);
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  if (ret < 0) {
    printf("Failed to set CS pin.\n");
  }
  return ret;

}

int32_t pinedio_set_cs(struct pinedio_inst *inst, bool active) {
  return pinedio_digital_write(inst, 0, active);
  
}

int32_t pinedio_write_read(struct pinedio_inst* inst, uint8_t *writearr, uint32_t writecnt, uint8_t* readarr, uint32_t readcnt) {
  /* How many packets ... */
  const size_t packets = (writecnt + readcnt + CH341_PACKET_LENGTH - 2) / (CH341_PACKET_LENGTH - 1);

  /* We pluck CS/timeout handling into the first packet thus we need to allocate one extra package. */
  uint8_t wbuf[packets*CH341_PACKET_LENGTH];
  uint8_t rbuf[writecnt + readcnt];
  /* Initialize the write buffer to zero to prevent writing random stack contents to device. */
  memset(wbuf, 0, CH341_PACKET_LENGTH);

  uint8_t *ptr = wbuf;
  /* CS usage is optimized by doing both transitions in one packet.
   * Final transition to deselected state is in the pin disable. */
//  pluck_cs(ptr, &data->stored_delay_us);
  if (inst->options[PINEDIO_OPTION_AUTO_CS]) {
    pinedio_set_cs(inst, true);
  }
  unsigned int write_left = writecnt;
  unsigned int read_left = readcnt;
  unsigned int p;
  for (p = 0; p < packets; p++) {
    unsigned int write_now = MIN(CH341_PACKET_LENGTH - 1, write_left);
    unsigned int read_now = MIN((CH341_PACKET_LENGTH - 1) - write_now, read_left);
    ptr = &wbuf[p*CH341_PACKET_LENGTH];
    *ptr++ = CH341_CMD_SPI_STREAM;
    unsigned int i;
    for (i = 0; i < write_now; ++i)
      *ptr++ = reverse_byte(*writearr++);
    if (read_now) {
      memset(ptr, 0xFF, read_now);
      read_left -= read_now;
    }
    write_left -= write_now;
  }

  int32_t ret = usb_transfer(inst, __func__, packets + writecnt + readcnt,
                             writecnt + readcnt, wbuf, rbuf, true);
  if (inst->options[PINEDIO_OPTION_AUTO_CS]) {
    pinedio_set_cs(inst, false);
  }
  if (ret < 0)
    return -1;

  unsigned int i;
  for (i = 0; i < readcnt; i++) {
    *readarr++ = reverse_byte(rbuf[writecnt + i]);
  }

  return 0;
}

int32_t pinedio_transceive(struct pinedio_inst* inst, uint8_t *write_buf, uint8_t* read_buf, uint32_t count) {
  const size_t packets = (count + CH341_PACKET_LENGTH - 2) / (CH341_PACKET_LENGTH - 1);

  uint8_t wbuf[packets*CH341_PACKET_LENGTH];


  uint8_t *ptr = wbuf;
  if (inst->options[PINEDIO_OPTION_AUTO_CS]) {
    pinedio_set_cs(inst, true);
  }
  unsigned int write_left = count;
  unsigned int read_left = count;
  unsigned int p;
  for (p = 0; p < packets; p++) {
    unsigned int write_now = MIN(CH341_PACKET_LENGTH - 1, write_left);
    unsigned int read_now = MIN((CH341_PACKET_LENGTH - 1) - write_now, read_left);
    ptr = &wbuf[p*CH341_PACKET_LENGTH];
    *ptr++ = CH341_CMD_SPI_STREAM;
    unsigned int i;
    for (i = 0; i < write_now; ++i)
      *ptr++ = reverse_byte(*write_buf++);
    if (read_now) {
      memset(ptr, 0xFF, read_now);
      read_left -= read_now;
    }
    write_left -= write_now;
  }

  int32_t ret = usb_transfer(inst, __func__, packets + count,
                             count, wbuf, read_buf, true);
  if (inst->options[PINEDIO_OPTION_AUTO_CS]) {
    pinedio_set_cs(inst, false);
  }
  if (ret < 0)
    return -1;

  unsigned int i;
  for (i = 0; i < count; i++) {
    *read_buf++ = reverse_byte(*read_buf);
  }

  return 0;
}

int32_t pinedio_digital_read(struct pinedio_inst *inst, uint32_t pin) {
  uint8_t buf[] = {
    0xA0,
  };
  uint8_t output[6];

  int32_t ret = usb_transfer(inst, __func__, sizeof(buf), sizeof(output), buf, output, true);
  if (ret < 0) {
    fprintf(stderr, "Could not get input pins.\n");
    return ret;
  }
  // *input = ((output[2] & 0x80) << 16) | ((output[1] & 0xef) << 8) | output[0];
  return output[0] & 1 << pin; // maybe?
}

static int32_t pinedio_get_input(struct pinedio_inst *inst, uint32_t* input)
{
  uint8_t buf[] = {
    0xA0,
  };
  uint8_t output[6];

  int32_t ret = usb_transfer(inst, __func__, sizeof(buf), sizeof(output), buf, output, true);
  if (ret < 0) {
    fprintf(stderr, "Could not get input pins.\n");
  }
  *input = ((output[2] & 0x80) << 16) | ((output[1] & 0xef) << 8) | output[0];
  return ret;
}

int32_t pinedio_get_irq_state(struct pinedio_inst *inst, uint32_t pin) {
  uint32_t input;
  int32_t ret = pinedio_get_input(inst, &input);
  if (ret != 0) {
    return ret;
  }
  return (input & (1 << pin)) != 0 ? 1 : 0;
}

/* True on any poll thread, superseded ones included: a successor overwrites pin_poll_thread, so
 * the handle cannot answer that. */
static __thread bool this_is_pin_poll_thread = false;

/* Sleep one poll interval, or less if this thread is told to stop. A plain sleep made every detach of
 * the last pin wait out the rest of it in pthread_join(): 0-33 ms, on a path the radio calls before
 * every channel scan. Returns true if the thread should exit. */
static bool pinedio_pin_poll_wait(struct pinedio_inst *inst) {
  struct timespec deadline;
#ifdef __linux__
  clock_gettime(CLOCK_MONOTONIC, &deadline); /* the clock pin_poll_wake was initialised with */
#else
  clock_gettime(CLOCK_REALTIME, &deadline);
#endif
  deadline.tv_nsec += pin_poll_interval_us * 1000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }
  bool should_exit;
  pinedio_mutex_lock(&inst->usb_access_mutex);
  for (;;) {
    should_exit = inst->pin_poll_thread_exit || !pthread_equal(inst->pin_poll_thread, pthread_self());
    if (should_exit || pthread_cond_timedwait(&inst->pin_poll_wake, &inst->usb_access_mutex, &deadline) == ETIMEDOUT)
      break;
  }
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  return should_exit;
}

static void* pinedio_pin_poll_thread(void* arg) {
  struct pinedio_inst *inst = arg;
  int32_t ret = 0;
  this_is_pin_poll_thread = true;
  bool should_exit = false;

  uint32_t input;
  while (!should_exit) {
    ret = pinedio_get_input(inst, &input);
    if (ret < 0) {
      should_exit = true;
      inst->in_error = true;
      fprintf(stderr, "Failed to get input, res: %d\n", ret);
      continue;
    }
    if (ret != 0) continue;
    inst->in_error = false;
    pinedio_mutex_lock(&inst->usb_access_mutex);
    for (uint8_t int_pin = 0; int_pin < PINEDIO_INT_PIN_MAX; int_pin++) {
      struct pinedio_inst_int* inst_int = &inst->interrupts[int_pin];
      /* Copy the callback while holding the lock. Reading inst_int->callback
       * again after the unlock below would race with pinedio_deattach_interrupt()
       * setting it to NULL, and we would jump to NULL. */
      void (*callback)(void) = inst_int->callback;
      if (callback == NULL) continue;
      uint8_t state = (input & ( 1 << int_pin)) != 0;
      if (inst_int->previous_state != 255 && inst_int->previous_state != state) {
        enum pinedio_int_mode mode =
                inst_int->previous_state == false && state == true ? PINEDIO_INT_MODE_RISING : PINEDIO_INT_MODE_FALLING;
        if (inst_int->mode & mode) {
          pinedio_mutex_unlock(&inst->usb_access_mutex);
          callback();
          pinedio_mutex_lock(&inst->usb_access_mutex);
          /* A re-arm during the callback hands the pin to a successor with previous_state reset to
           * 255; our pre-callback sample must not become its baseline. */
          if (inst->pin_poll_thread_exit || !pthread_equal(inst->pin_poll_thread, pthread_self()))
            break;
          /* Same thread, same sentinel: it was not 255 when this branch was entered. */
          if (inst_int->previous_state == 255)
            continue;
        }
      }
      inst_int->previous_state = state;
    }

    /* A re-attach can start the successor and clear the exit flag before we read it, so the
     * handle is the tiebreak: stand down rather than poll alongside it. */
    should_exit = inst->pin_poll_thread_exit || !pthread_equal(inst->pin_poll_thread, pthread_self());
    pinedio_mutex_unlock(&inst->usb_access_mutex);
    if (should_exit)
      break; /* no point sleeping on the way out, and it keeps a deinit wait short */
    should_exit = pinedio_pin_poll_wait(inst);
  }

  /* Last touch of inst: after this a deinit may free it and close the device. */
  pinedio_mutex_lock(&inst->usb_access_mutex);
  inst->pin_poll_threads_alive--;
  pthread_cond_broadcast(&inst->pin_poll_thread_gone);
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  return NULL;
}

int32_t
pinedio_attach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin, enum pinedio_int_mode int_mode,
                         void (*callback)(void)) {
  int32_t res = 0;
  // TODO: Add check if int_pin is correct
  pinedio_mutex_lock(&inst->usb_access_mutex);
  if (inst->deinit_started) {
    pinedio_mutex_unlock(&inst->usb_access_mutex);
    return -1;
  }
  bool was_attached = inst->interrupts[int_pin].callback != NULL;
  inst->interrupts[int_pin].previous_state = 255;
  inst->interrupts[int_pin].mode = int_mode;
  inst->interrupts[int_pin].callback = callback;

  /* Only a new attachment touches the refcount. Re-arming a pin that is already
   * attached must not drop the count to 0, because that made the check below
   * spawn a second poll thread beside the running one: both then polled the same
   * device and the first handle was overwritten and leaked. Consumers do re-arm
   * without detaching first (meshtastic's RadioLib does), so this was reached in
   * practice, not just in theory. */
  if (!was_attached) {
    if (inst->int_running_cnt == 0) {
      inst->pin_poll_thread_exit = false;
      res = pthread_create(&inst->pin_poll_thread, NULL, pinedio_pin_poll_thread, inst);
      pthread_cond_broadcast(&inst->pin_poll_wake); /* a superseded thread stands down now, not after its sleep */
      if (res != 0) {
        fprintf(stderr, "Failed to create thread, res: %d\n", res);
        inst->interrupts[int_pin].callback = NULL;
        goto unlock;
      }
      inst->pin_poll_threads_alive++;
    }
    inst->int_running_cnt++;
  }

unlock:
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  return res;
}

int32_t pinedio_deattach_interrupt(struct pinedio_inst *inst, enum pinedio_int_pin int_pin) {
  // TODO: Add check if int_pin is correct
  pinedio_mutex_lock(&inst->usb_access_mutex);
  int32_t res;
  if (inst->int_running_cnt == 0 || inst->interrupts[int_pin].callback == NULL) {
    res = -1;
    goto unlock;
  }
  inst->interrupts[int_pin].callback = NULL;
  inst->int_running_cnt--;
  if (inst->int_running_cnt == 0) {
    inst->pin_poll_thread_exit = true;
    pthread_cond_broadcast(&inst->pin_poll_wake);
    /* Copy the handle before releasing the lock: a concurrent attach would
     * overwrite inst->pin_poll_thread with a newly created thread, and we would
     * join that one instead. Joining under the lock is not an option, since the
     * poll thread takes the same mutex. */
    pthread_t thread_to_join = inst->pin_poll_thread;
    pinedio_mutex_unlock(&inst->usb_access_mutex);
    if (!pthread_equal(thread_to_join, pthread_self()))
      pthread_join(thread_to_join, NULL);
    else
      /* Called from within the poll thread itself (e.g. an interrupt
       * callback detaching its own interrupt). We cannot join ourselves,
       * but skipping reclamation entirely strands this thread's stack:
       * nothing ever joins it, and the pthread_create() in the next
       * pinedio_attach_interrupt() overwrites the only handle. Each
       * strand permanently leaks the thread's ~8 MB stack mapping;
       * callers that detach from the callback on every radio interrupt
       * leak at interrupt rate (meshtastic/firmware#10468 — hundreds of
       * GB of VSZ within days). Detaching ourselves instead lets glibc
       * reclaim the stack when the thread exits. */
      pthread_detach(pthread_self());
    return 0;
  }
unlock:
  pinedio_mutex_unlock(&inst->usb_access_mutex);
  return res;
}

void pinedio_deinit(struct pinedio_inst *inst) {
  pinedio_mutex_lock(&inst->usb_access_mutex);
  /* Whoever drops the count to 0 under the lock owns the thread, so zeroing it here keeps a
   * concurrent pinedio_deattach_interrupt() from claiming the same one. */
  bool stop = inst->int_running_cnt != 0;
  pthread_t thread_to_join = inst->pin_poll_thread; /* copy before unlocking, as above */
  inst->int_running_cnt = 0;
  inst->pin_poll_thread_exit = true;
  pthread_cond_broadcast(&inst->pin_poll_wake);
  /* pthread_cond_wait() below drops the mutex: an attach getting in would start a poll thread we
   * then wait on forever, or tear the device out from under. */
  inst->deinit_started = true;
  /* A self-detached thread is not joinable but still reads inst, so wait every live one out.
   * Waiting when we are one would deadlock: only it can decrement the count. */
  bool self_is_poll = this_is_pin_poll_thread;
  while (inst->pin_poll_threads_alive > 0 && !self_is_poll)
    pthread_cond_wait(&inst->pin_poll_thread_gone, &inst->usb_access_mutex);
  pinedio_mutex_unlock(&inst->usb_access_mutex);

  /* Keyed on the handle, not on self_is_poll: a superseded thread calling this is not the one
   * named by pin_poll_thread, and has already detached itself. */
  if (stop) {
    if (!pthread_equal(thread_to_join, pthread_self()))
      pthread_join(thread_to_join, NULL);
    else
      pthread_detach(pthread_self()); /* same self-call strand as above */
  }

  for (int i = 0; i < USB_IN_TRANSFERS; i++) {
    if (inst->transfer_ins[i] != NULL) {
      libusb_free_transfer(inst->transfer_ins[i]);
    }
  }
  if (inst->transfer_out != NULL) {
    libusb_free_transfer(inst->transfer_out);
  }
  
  if (inst->handle != NULL) {
    // We don't know if claim of interface was successful, but libusb handles this.
    libusb_release_interface(inst->handle, 0);
#ifdef __linux__
    libusb_attach_kernel_driver(inst->handle, 0);
#endif
    libusb_close(inst->handle);
    inst->handle = NULL;
  }

  /* Only once no poll thread can reach it: a self-teardown leaves one running, and it still
   * broadcasts on its way out. Skipping this would leave a re-init of the same static instance
   * re-initializing a live condition variable. */
  if (!self_is_poll) {
    pthread_cond_destroy(&inst->pin_poll_thread_gone);
    pthread_cond_destroy(&inst->pin_poll_wake);
  }
}
