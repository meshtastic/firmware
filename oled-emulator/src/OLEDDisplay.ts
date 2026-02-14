/**
 * OLEDDisplay.ts - TypeScript port of esp8266-oled-ssd1306 OLEDDisplay API
 *
 * This provides the same drawing API as the firmware uses, allowing
 * Meshtastic screen rendering code to be ported to the web.
 *
 * Buffer format: Vertical byte packing (8 vertical pixels per byte)
 * Same as SSD1306/SH1106 native format.
 */

export type OLEDDISPLAY_GEOMETRY =
  | "128x64"
  | "128x32"
  | "64x48"
  | "64x32"
  | "128x128"
  | "96x16";

export type OLEDDISPLAY_COLOR = "WHITE" | "BLACK" | "INVERSE";

export type OLEDDISPLAY_TEXT_ALIGNMENT =
  | "TEXT_ALIGN_LEFT"
  | "TEXT_ALIGN_CENTER"
  | "TEXT_ALIGN_RIGHT";

export interface OLEDFont {
  height: number;
  firstChar: number;
  charCount: number;
  widths: number[];
  data: Uint8Array;
}

// Resolution info based on geometry
const GEOMETRY_SIZES: Record<
  OLEDDISPLAY_GEOMETRY,
  { width: number; height: number }
> = {
  "128x64": { width: 128, height: 64 },
  "128x32": { width: 128, height: 32 },
  "64x48": { width: 64, height: 48 },
  "64x32": { width: 64, height: 32 },
  "128x128": { width: 128, height: 128 },
  "96x16": { width: 96, height: 16 },
};

export class OLEDDisplay {
  private buffer: Uint8Array;
  private _width: number;
  private _height: number;
  private color: OLEDDISPLAY_COLOR = "WHITE";
  private textAlignment: OLEDDISPLAY_TEXT_ALIGNMENT = "TEXT_ALIGN_LEFT";
  private font: OLEDFont | null = null;

  constructor(geometry: OLEDDISPLAY_GEOMETRY = "128x64") {
    const size = GEOMETRY_SIZES[geometry];
    this._width = size.width;
    this._height = size.height;
    // Buffer size: width * (height / 8) - vertical byte packing
    this.buffer = new Uint8Array((this._width * this._height) / 8);
  }

  get width(): number {
    return this._width;
  }

  get height(): number {
    return this._height;
  }

  getWidth(): number {
    return this._width;
  }

  getHeight(): number {
    return this._height;
  }

  /**
   * Get the raw framebuffer (vertical byte packing format)
   */
  getBuffer(): Uint8Array {
    return this.buffer;
  }

  getBufferSize(): number {
    return this.buffer.length;
  }

  /**
   * Clear the display buffer
   */
  clear(): void {
    this.buffer.fill(0);
  }

  /**
   * Set the drawing color
   */
  setColor(color: OLEDDISPLAY_COLOR): void {
    this.color = color;
  }

  /**
   * Set text alignment for drawString
   */
  setTextAlignment(align: OLEDDISPLAY_TEXT_ALIGNMENT): void {
    this.textAlignment = align;
  }

  /**
   * Set the current font
   */
  setFont(font: OLEDFont): void {
    this.font = font;
  }

  /**
   * Set a single pixel
   */
  setPixel(x: number, y: number): void {
    if (x < 0 || x >= this._width || y < 0 || y >= this._height) return;

    // Vertical byte packing: each byte represents 8 vertical pixels
    const byteIndex = x + Math.floor(y / 8) * this._width;
    const bitMask = 1 << (y & 7);

    switch (this.color) {
      case "WHITE":
        this.buffer[byteIndex] |= bitMask;
        break;
      case "BLACK":
        this.buffer[byteIndex] &= ~bitMask;
        break;
      case "INVERSE":
        this.buffer[byteIndex] ^= bitMask;
        break;
    }
  }

  /**
   * Clear a single pixel (set to black)
   */
  clearPixel(x: number, y: number): void {
    if (x < 0 || x >= this._width || y < 0 || y >= this._height) return;
    const byteIndex = x + Math.floor(y / 8) * this._width;
    const bitMask = 1 << (y & 7);
    this.buffer[byteIndex] &= ~bitMask;
  }

  /**
   * Get a pixel value (true = on, false = off)
   */
  getPixel(x: number, y: number): boolean {
    if (x < 0 || x >= this._width || y < 0 || y >= this._height) return false;
    const byteIndex = x + Math.floor(y / 8) * this._width;
    const bitMask = 1 << (y & 7);
    return (this.buffer[byteIndex] & bitMask) !== 0;
  }

  /**
   * Draw a line using Bresenham's algorithm
   */
  drawLine(x0: number, y0: number, x1: number, y1: number): void {
    const dx = Math.abs(x1 - x0);
    const dy = Math.abs(y1 - y0);
    const sx = x0 < x1 ? 1 : -1;
    const sy = y0 < y1 ? 1 : -1;
    let err = dx - dy;

    while (true) {
      this.setPixel(x0, y0);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  /**
   * Draw horizontal line (optimized)
   */
  drawHorizontalLine(x: number, y: number, length: number): void {
    if (y < 0 || y >= this._height) return;
    for (let i = 0; i < length; i++) {
      this.setPixel(x + i, y);
    }
  }

  /**
   * Draw vertical line (optimized)
   */
  drawVerticalLine(x: number, y: number, length: number): void {
    if (x < 0 || x >= this._width) return;
    for (let i = 0; i < length; i++) {
      this.setPixel(x, y + i);
    }
  }

  /**
   * Draw a rectangle outline
   */
  drawRect(x: number, y: number, width: number, height: number): void {
    this.drawHorizontalLine(x, y, width);
    this.drawHorizontalLine(x, y + height - 1, width);
    this.drawVerticalLine(x, y, height);
    this.drawVerticalLine(x + width - 1, y, height);
  }

  /**
   * Draw a filled rectangle
   */
  fillRect(x: number, y: number, width: number, height: number): void {
    for (let i = 0; i < width; i++) {
      for (let j = 0; j < height; j++) {
        this.setPixel(x + i, y + j);
      }
    }
  }

  /**
   * Draw a circle outline using midpoint algorithm
   */
  drawCircle(x0: number, y0: number, radius: number): void {
    let x = radius;
    let y = 0;
    let err = 0;

    while (x >= y) {
      this.setPixel(x0 + x, y0 + y);
      this.setPixel(x0 + y, y0 + x);
      this.setPixel(x0 - y, y0 + x);
      this.setPixel(x0 - x, y0 + y);
      this.setPixel(x0 - x, y0 - y);
      this.setPixel(x0 - y, y0 - x);
      this.setPixel(x0 + y, y0 - x);
      this.setPixel(x0 + x, y0 - y);

      y++;
      if (err <= 0) {
        err += 2 * y + 1;
      }
      if (err > 0) {
        x--;
        err -= 2 * x + 1;
      }
    }
  }

  /**
   * Draw a filled circle
   */
  fillCircle(x0: number, y0: number, radius: number): void {
    let x = radius;
    let y = 0;
    let err = 0;

    while (x >= y) {
      this.drawHorizontalLine(x0 - x, y0 + y, 2 * x + 1);
      this.drawHorizontalLine(x0 - y, y0 + x, 2 * y + 1);
      this.drawHorizontalLine(x0 - x, y0 - y, 2 * x + 1);
      this.drawHorizontalLine(x0 - y, y0 - x, 2 * y + 1);

      y++;
      if (err <= 0) {
        err += 2 * y + 1;
      }
      if (err > 0) {
        x--;
        err -= 2 * x + 1;
      }
    }
  }

  /**
   * Draw a quarter circle (quadrant 0-3)
   */
  drawCircleQuads(x0: number, y0: number, radius: number, quads: number): void {
    let x = radius;
    let y = 0;
    let err = 0;

    while (x >= y) {
      if (quads & 0x1) {
        this.setPixel(x0 + x, y0 - y);
        this.setPixel(x0 + y, y0 - x);
      }
      if (quads & 0x2) {
        this.setPixel(x0 - y, y0 - x);
        this.setPixel(x0 - x, y0 - y);
      }
      if (quads & 0x4) {
        this.setPixel(x0 - x, y0 + y);
        this.setPixel(x0 - y, y0 + x);
      }
      if (quads & 0x8) {
        this.setPixel(x0 + y, y0 + x);
        this.setPixel(x0 + x, y0 + y);
      }

      y++;
      if (err <= 0) {
        err += 2 * y + 1;
      }
      if (err > 0) {
        x--;
        err -= 2 * x + 1;
      }
    }
  }

  /**
   * Draw an XBM image (like firmware's drawXbm)
   * XBM format: LSB first, horizontal
   */
  drawXbm(
    x: number,
    y: number,
    width: number,
    height: number,
    xbm: Uint8Array | number[],
  ): void {
    const widthBytes = Math.ceil(width / 8);
    for (let j = 0; j < height; j++) {
      for (let i = 0; i < width; i++) {
        const byteIndex = j * widthBytes + Math.floor(i / 8);
        const bitIndex = i % 8;
        if (xbm[byteIndex] & (1 << bitIndex)) {
          this.setPixel(x + i, y + j);
        }
      }
    }
  }

  /**
   * Draw a PROGMEM-style image (vertical byte format, like some firmware icons)
   */
  drawFastImage(
    x: number,
    y: number,
    width: number,
    height: number,
    image: Uint8Array | number[],
  ): void {
    const heightInBytes = Math.ceil(height / 8);
    for (let i = 0; i < width; i++) {
      for (let j = 0; j < heightInBytes; j++) {
        const imageByte = image[i + j * width];
        for (let bit = 0; bit < 8; bit++) {
          if (imageByte & (1 << bit)) {
            this.setPixel(x + i, y + j * 8 + bit);
          }
        }
      }
    }
  }

  /**
   * Get the width of a string with the current font
   */
  getStringWidth(text: string): number {
    if (!this.font) return 0;
    let width = 0;
    for (const char of text) {
      const charCode = char.charCodeAt(0);
      if (
        charCode >= this.font.firstChar &&
        charCode < this.font.firstChar + this.font.charCount
      ) {
        width += this.font.widths[charCode - this.font.firstChar];
      } else {
        // Unknown char - use space width or default
        width += this.font.widths[0] || 4;
      }
    }
    return width;
  }

  /**
   * Draw a string at the given position
   */
  drawString(x: number, y: number, text: string): void {
    if (!this.font) return;

    // Adjust x based on alignment
    let startX = x;
    if (this.textAlignment === "TEXT_ALIGN_CENTER") {
      startX = x - Math.floor(this.getStringWidth(text) / 2);
    } else if (this.textAlignment === "TEXT_ALIGN_RIGHT") {
      startX = x - this.getStringWidth(text);
    }

    let cursorX = startX;
    for (const char of text) {
      const charCode = char.charCodeAt(0);
      if (
        charCode >= this.font.firstChar &&
        charCode < this.font.firstChar + this.font.charCount
      ) {
        const charIndex = charCode - this.font.firstChar;
        const charWidth = this.font.widths[charIndex];

        // Find the offset into the font data
        let offset = 0;
        const bytesPerColumn = Math.ceil(this.font.height / 8);
        for (let i = 0; i < charIndex; i++) {
          offset += this.font.widths[i] * bytesPerColumn;
        }

        // Draw the character
        this.drawFontChar(
          cursorX,
          y,
          charWidth,
          this.font.height,
          this.font.data,
          offset,
        );
        cursorX += charWidth;
      } else {
        // Unknown character - advance by space
        cursorX += this.font.widths[0] || 4;
      }
    }
  }

  /**
   * Draw a single font character
   */
  private drawFontChar(
    x: number,
    y: number,
    width: number,
    height: number,
    data: Uint8Array,
    offset: number,
  ): void {
    const bytesPerColumn = Math.ceil(height / 8);
    for (let col = 0; col < width; col++) {
      for (let byteRow = 0; byteRow < bytesPerColumn; byteRow++) {
        const dataByte = data[offset + col * bytesPerColumn + byteRow];
        for (let bit = 0; bit < 8; bit++) {
          const pixelY = byteRow * 8 + bit;
          if (pixelY < height && dataByte & (1 << bit)) {
            this.setPixel(x + col, y + pixelY);
          }
        }
      }
    }
  }

  /**
   * Draw a string that wraps within maxWidth
   */
  drawStringMaxWidth(
    x: number,
    y: number,
    maxWidth: number,
    text: string,
  ): void {
    if (!this.font) return;

    const words = text.split(" ");
    let line = "";
    let lineY = y;

    for (const word of words) {
      const testLine = line + (line ? " " : "") + word;
      const testWidth = this.getStringWidth(testLine);

      if (testWidth > maxWidth && line) {
        this.drawString(x, lineY, line);
        line = word;
        lineY += this.font.height;
      } else {
        line = testLine;
      }
    }

    if (line) {
      this.drawString(x, lineY, line);
    }
  }

  /**
   * Draw a progress bar
   */
  drawProgressBar(
    x: number,
    y: number,
    width: number,
    height: number,
    progress: number,
  ): void {
    // Clamp progress 0-100
    progress = Math.max(0, Math.min(100, progress));

    // Draw outline
    this.drawRect(x, y, width, height);

    // Draw fill
    const fillWidth = Math.floor(((width - 4) * progress) / 100);
    this.fillRect(x + 2, y + 2, fillWidth, height - 4);
  }

  /**
   * Display - no-op for emulator (would send buffer to hardware)
   */
  display(): void {
    // In real hardware this sends the buffer to the display
    // For emulator, this is a no-op - we render from getBuffer()
  }

  /**
   * Flip the display (useful for some mounting orientations)
   */
  flipScreenVertically(): void {
    // Rotate buffer 180 degrees
    const newBuffer = new Uint8Array(this.buffer.length);
    const heightBytes = this._height / 8;

    for (let x = 0; x < this._width; x++) {
      for (let yByte = 0; yByte < heightBytes; yByte++) {
        let srcByte = this.buffer[x + yByte * this._width];
        // Reverse the bits
        let reversed = 0;
        for (let i = 0; i < 8; i++) {
          if (srcByte & (1 << i)) {
            reversed |= 1 << (7 - i);
          }
        }
        // Place in mirrored position
        const newX = this._width - 1 - x;
        const newYByte = heightBytes - 1 - yByte;
        newBuffer[newX + newYByte * this._width] = reversed;
      }
    }

    this.buffer = newBuffer;
  }
}

// Color constants for compatibility with firmware code style
export const WHITE: OLEDDISPLAY_COLOR = "WHITE";
export const BLACK: OLEDDISPLAY_COLOR = "BLACK";
export const INVERSE: OLEDDISPLAY_COLOR = "INVERSE";

export const TEXT_ALIGN_LEFT: OLEDDISPLAY_TEXT_ALIGNMENT = "TEXT_ALIGN_LEFT";
export const TEXT_ALIGN_CENTER: OLEDDISPLAY_TEXT_ALIGNMENT =
  "TEXT_ALIGN_CENTER";
export const TEXT_ALIGN_RIGHT: OLEDDISPLAY_TEXT_ALIGNMENT = "TEXT_ALIGN_RIGHT";
