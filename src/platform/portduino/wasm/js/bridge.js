// JS side of the wasm WebUSB bridge. The C backend (libpinedio_webusb.c) calls
// Module.ch341.<method> from EM_ASYNC_JS/EM_JS. This wraps the same CH341
// transport used by the pure-JS probe, marshalling buffers in/out of the wasm
// heap.
//
// Wiring (with MODULARIZE'd Emscripten output):
//   const device = (await CH341.request()).device;        // user gesture
//   const Module = await createModule({ noInitialRun: true });
//   Module.ch341 = createCH341Bridge(Module, device);
//   Module.callMain([]);                                  // runs C main()
//
// IMPORTANT: a wasm heap can grow across an `await`, so always re-read
// Module.HEAPU8 *after* awaiting, never cache it across a suspension point.

import { CH341 } from "./ch341.js";

export function createCH341Bridge(Module, device) {
  let ch = null;

  function writeCString(str, ptr, max) {
    const bytes = new TextEncoder().encode(str);
    const n = Math.min(bytes.length, max - 1);
    const heap = Module.HEAPU8;
    heap.set(bytes.subarray(0, n), ptr);
    heap[ptr + n] = 0;
  }

  return {
    // vid/pid/serial come from the C side; if a device was already handed in
    // (typical — selected via a user gesture), just open that one.
    async open(vid, pid, serial) {
      let dev = device;
      if (!dev) {
        dev = await CH341.tryReconnect({ vid, pid }).then((c) =>
          c ? c.device : null,
        );
        if (!dev) return -2; // no granted device; page must requestDevice first
      }
      // First-connect is flaky: the WebUSB interface can be momentarily
      // unclaimable right after the grant, or still held by a prior session
      // (yields the transient "Could not open SPI: -1"). Retry with a short
      // backoff, resetting the device between attempts so claimInterface doesn't
      // trip over a half-open device.
      const attempts = 4;
      for (let i = 0; i < attempts; i++) {
        ch = new CH341(dev);
        try {
          await ch.open();
          return 0;
        } catch (e) {
          console.warn(`CH341 open attempt ${i + 1}/${attempts} failed:`, e);
          try {
            await dev.close(); // release/close so the next attempt starts clean
          } catch (_) {}
          if (i < attempts - 1)
            await new Promise((r) => setTimeout(r, 200 * (i + 1)));
        }
      }
      console.error("CH341 open failed after", attempts, "attempts");
      return -1;
    },

    // Read `count` bytes from the heap at writePtr, full-duplex transfer, write
    // the result back at readPtr. Returns 0 / negative.
    async transceive(writePtr, readPtr, count) {
      try {
        const out = Module.HEAPU8.slice(writePtr, writePtr + count); // copy before await
        const inBuf = await ch.transceive(out);
        Module.HEAPU8.set(inBuf, readPtr); // re-read HEAPU8 (may have grown)
        return 0;
      } catch (e) {
        console.error("transceive failed:", e);
        return -1;
      }
    },

    async digitalWrite(pin, value) {
      try {
        await ch.digitalWrite(pin, value);
        return 0;
      } catch (e) {
        return -1;
      }
    },

    async digitalRead(pin) {
      try {
        return await ch.digitalRead(pin);
      } catch (e) {
        return -1;
      }
    },

    setPinMode(pin, output) {
      ch.setPinMode(pin, output);
    },

    setAutoCS(enabled) {
      if (ch) ch.autoCS = enabled;
    },

    getSerial(ptr, max) {
      writeCString(ch?.serial || "", ptr, max);
    },

    getProduct(ptr, max) {
      writeCString(ch?.product || "", ptr, max);
    },

    async close() {
      if (ch) await ch.close();
      ch = null;
    },
  };
}
