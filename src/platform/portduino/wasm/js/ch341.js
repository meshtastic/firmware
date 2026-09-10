// WebUSB driver for the CH341 USB-to-SPI bridge.
//
// This is the browser-side transport. It mirrors the public surface that the
// firmware's Ch341Hal (src/platform/portduino/USBHal.h) needs from the
// libpinedio C API — transceive / digitalWrite / digitalRead / setPinMode /
// setCS — but backed by async WebUSB instead of libusb.
//
// Every method is async (WebUSB is Promise-only). The wasm build will call the
// same WebUSB primitives from C via Asyncify so a synchronous C SPI transfer
// can await these. Here, used directly, it's the pure-JS transport for the
// hardware checkpoint (web/probe.js).

import * as proto from "./protocol.js";

// Opt-in per-op USB tracing (node: DEBUG_USB=1). When a run wedges, the tail of
// the log shows the last USB ops + timing, so a stuck BUSY poll vs a stalled
// transfer is obvious.
let __usbOp = 0;
const __now = () =>
  typeof performance !== "undefined" ? performance.now() : Date.now();
const __dbg =
  typeof process !== "undefined" && process?.env?.DEBUG_USB
    ? (m) => console.error(`[usb#${++__usbOp} ${__now().toFixed(0)}ms] ${m}`)
    : null;
const __hex = (a, n = 6) =>
  Array.from(a.slice(0, n))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");

export class CH341 {
  /** @param {USBDevice} device */
  constructor(device) {
    this.device = device;
    this.dMode = 0; // D0..D7 direction bitfield (1 = output)
    this.dState = 0; // D0..D7 output level bitfield
    this.autoCS = true; // toggle CS around each transceive
    this.csPin = 0; // D0 is CS on the common CH341 LoRa adapters
    this.csActiveLow = true; // NSS is active-low: assert (select) = drive CS LOW
    this.interfaceNumber = 0;
  }

  // Prompt the user to pick a CH341 (must be called from a user gesture).
  static async request({ vid = proto.VID, pid = proto.PID } = {}) {
    if (!("usb" in navigator))
      throw new Error("WebUSB unavailable (use Chromium over https/localhost)");
    const device = await navigator.usb.requestDevice({
      filters: [{ vendorId: vid, productId: pid }],
    });
    return new CH341(device);
  }

  // Reconnect to an already-granted device without a prompt, if present.
  static async tryReconnect({ vid = proto.VID, pid = proto.PID } = {}) {
    if (!("usb" in navigator)) return null;
    const devices = await navigator.usb.getDevices();
    const device = devices.find(
      (d) => d.vendorId === vid && d.productId === pid,
    );
    return device ? new CH341(device) : null;
  }

  async open() {
    await this.device.open();
    // Don't read .configuration (the node-usb backend throws "device is not
    // configured" instead of returning null); just (re)select config 1, which is
    // idempotent in the browser if it's already active.
    try {
      await this.device.selectConfiguration(1);
    } catch (_) {}
    // The SPI bridge lives on interface 0 with bulk EP 0x02/0x82.
    await this.device.claimInterface(this.interfaceNumber);
    this.serial = this.device.serialNumber || "";
    this.product = this.device.productName || "";
    // CH341A SPI bus pins MUST be configured as outputs or the chip never
    // clocks SCK/MOSI and every MISO byte reads back 0xFF. D3=SCK, D5=MOSI
    // (D7=MISO stays input). This mirrors Ch341Hal's constructor
    // (USBHal.h: pinedio_set_pin_mode(&pinedio, 3, true) / (5, true)).
    this.setPinMode(3, true); // SCK (DCK)
    this.setPinMode(5, true); // MOSI (DOUT)
    this.setPinMode(this.csPin, true); // CS (NSS)
    await this.setCS(false); // transmits direction bits + idles CS deasserted
  }

  async close() {
    try {
      await this.device.releaseInterface(this.interfaceNumber);
    } catch (_) {}
    try {
      await this.device.close();
    } catch (_) {}
  }

  async _out(bytes) {
    const r = await this.device.transferOut(proto.WRITE_EP, bytes);
    if (r.status !== "ok") throw new Error(`USB OUT ${r.status}`);
    return r.bytesWritten;
  }

  async _in(len) {
    const r = await this.device.transferIn(proto.READ_EP, len);
    if (r.status !== "ok") throw new Error(`USB IN ${r.status}`);
    return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
  }

  // GPIO direction is recorded here and applied on the next digitalWrite,
  // exactly as libpinedio does (its set_pin_mode does not transmit on its own).
  setPinMode(pin, output) {
    if (output) this.dMode |= 1 << pin;
    else this.dMode &= ~(1 << pin);
  }

  async digitalWrite(pin, value) {
    if (value) this.dState |= 1 << pin;
    else this.dState &= ~(1 << pin);
    await this._out(proto.buildUioOut(this.dState, this.dMode));
  }

  async setCS(active) {
    // active === "chip selected". On active-low NSS (the usual case) that means
    // driving the CS line LOW. libpinedio's upstream AUTO_CS drove it HIGH,
    // which leaves the radio deselected on these adapters (MISO reads 0xFF).
    const level = this.csActiveLow ? (active ? 0 : 1) : active ? 1 : 0;
    return this.digitalWrite(this.csPin, level);
  }

  async digitalRead(pin) {
    __dbg && __dbg(`dR D${pin} start`);
    await this._out(proto.buildGetInput());
    const reply = await this._in(6);
    const v = proto.inputPin(reply, pin);
    __dbg && __dbg(`dR D${pin}=${v} (in ${reply.length}B)`);
    return v;
  }

  // Full-duplex SPI transfer: returns a Uint8Array of the same length as the
  // write buffer (MISO sampled for every byte clocked out).
  async transceive(writeBytes) {
    const data =
      writeBytes instanceof Uint8Array
        ? writeBytes
        : Uint8Array.from(writeBytes);
    __dbg && __dbg(`tx ${data.length}B out=${__hex(data)} start`);
    if (this.autoCS) await this.setCS(true);
    try {
      const packets = proto.buildSpiStreamPackets(data);
      const read = new Uint8Array(data.length);
      let off = 0;
      for (const pkt of packets) {
        const dataLen = pkt.length - 1;
        await this._out(pkt);
        // WebUSB transferIn can return FEWER bytes than requested; accumulate
        // until we have all dataLen MISO bytes, or the device returns nothing.
        // Treating a short read's missing bytes as 0 corrupts the SPI response
        // and puts the radio in a bad state (intermittent hangs during init).
        let got = 0;
        while (got < dataLen) {
          const r = await this._in(dataLen - got);
          // A zero-length read means the device gave us nothing while MISO bytes
          // were still outstanding. Leaving them 0 would silently corrupt the SPI
          // response, so fail loudly instead of returning a partial buffer.
          if (r.length === 0)
            throw new Error(
              `CH341 SPI short read: ${got}/${dataLen} MISO bytes (device returned 0)`,
            );
          for (let i = 0; i < r.length; i++)
            read[off + got + i] = proto.reverseByte(r[i]);
          got += r.length;
        }
        off += dataLen;
      }
      __dbg && __dbg(`tx ${data.length}B in=${__hex(read)} done`);
      return read;
    } finally {
      if (this.autoCS) await this.setCS(false);
    }
  }

  // Convenience: write-then-read with CS held across the whole exchange (used
  // for register reads where the read phase follows command+address bytes).
  async writeRead(writeBytes, readLen) {
    const tx = new Uint8Array(writeBytes.length + readLen);
    tx.set(writeBytes, 0); // read phase clocks 0x00
    const rx = await this.transceive(tx);
    return rx.slice(writeBytes.length);
  }
}
