// CH341 USB-to-SPI wire protocol — pure framing functions, no WebUSB/DOM.
//
// Faithful port of the framing in libch341-spi-userspace (libpinedio-usb.c):
//   - SPI is streamed with command 0xA8; each USB packet is <=32 bytes
//     (1 command byte + up to 31 data bytes). The CH341 is bit-reversed on the
//     wire, so every SPI byte (TX and RX) is bit-reversed.
//   - GPIO (D0..D7) is driven with UIO stream command 0xAB. D0 is wired to CS.
//   - Inputs are read with the 0xA0 status command.
//
// These functions are deliberately side-effect free so they can be unit-tested
// under node without any hardware, and reused verbatim by both the browser
// driver (src/ch341.js) and the wasm C backend's JS glue.

export const VID = 0x1a86;
export const PID = 0x5512;

// WebUSB endpoint *numbers* (direction is implied by transferIn/transferOut).
// libpinedio uses EP 0x02 (OUT) and 0x82 (IN) — both endpoint number 2.
export const WRITE_EP = 2;
export const READ_EP = 2;

export const CMD_SPI_STREAM = 0xa8;
export const CMD_UIO_STREAM = 0xab;
export const UIO_STM_OUT = 0x80;
export const UIO_STM_DIR = 0x40;
export const UIO_STM_END = 0x20;
export const CMD_GET_STATUS = 0xa0;

export const PACKET_LENGTH = 0x20; // 32
export const DATA_PER_PACKET = PACKET_LENGTH - 1; // 31

// Reverse the bit order of a byte (CH341 clocks SPI MSB/LSB swapped).
export function reverseByte(x) {
  x &= 0xff;
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x & 0xff;
}

// Split an SPI write buffer into bit-reversed 0xA8 stream packets.
// Returns an array of Uint8Array, each [0xA8, rev(b0), rev(b1), ...] with at
// most 31 data bytes. The number of SPI data bytes equals writeBytes.length
// (1:1 full-duplex), so the expected total read length is writeBytes.length.
export function buildSpiStreamPackets(writeBytes) {
  const data =
    writeBytes instanceof Uint8Array ? writeBytes : Uint8Array.from(writeBytes);
  const packets = [];
  for (let off = 0; off < data.length; off += DATA_PER_PACKET) {
    const n = Math.min(DATA_PER_PACKET, data.length - off);
    const pkt = new Uint8Array(1 + n);
    pkt[0] = CMD_SPI_STREAM;
    for (let i = 0; i < n; i++) pkt[1 + i] = reverseByte(data[off + i]);
    packets.push(pkt);
  }
  return packets;
}

// Un-reverse a buffer of bytes read back from the device (in place on a copy).
export function unreverseBytes(bytes) {
  const out = new Uint8Array(bytes.length);
  for (let i = 0; i < bytes.length; i++) out[i] = reverseByte(bytes[i]);
  return out;
}

// UIO packet that drives output levels (D0..D5) and sets direction.
// Mirrors pinedio_digital_write: [0xAB, 0x80|state, 0x40|dir, 0x20].
// state/dir are masked to 6 bits so they don't collide with the command bits.
export function buildUioOut(stateBits, modeBits) {
  return new Uint8Array([
    CMD_UIO_STREAM,
    UIO_STM_OUT | (stateBits & 0x3f),
    UIO_STM_DIR | (modeBits & 0x3f),
    UIO_STM_END,
  ]);
}

// UIO packet that sets direction only. Mirrors pinedio_set_pin_mode:
// [0xAB, 0x40|dir, 0x20].
export function buildUioDir(modeBits) {
  return new Uint8Array([
    CMD_UIO_STREAM,
    UIO_STM_DIR | (modeBits & 0x3f),
    UIO_STM_END,
  ]);
}

// Status/input request. Device replies with up to 6 bytes; D0..D7 live in [0].
export function buildGetInput() {
  return new Uint8Array([CMD_GET_STATUS]);
}

// Read a single D0..D7 input level from a status reply (byte 0 holds D0..D7).
export function inputPin(statusReply, pin) {
  return (statusReply[0] >> pin) & 1;
}
