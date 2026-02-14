<script setup lang="ts">
/**
 * OLEDEmulator.vue - Vue 3 component for OLED display emulation
 * 
 * Renders an OLEDDisplay framebuffer with realistic OLED visual effects
 * including pixel glow, color tinting, and various display sizes.
 */

import { ref, computed, watch, onMounted, onUnmounted, type PropType } from 'vue';
import { OLEDDisplay } from './OLEDDisplay';
import { DISPLAY_PRESETS, type DisplayPreset } from './DisplayPresets';

const props = defineProps({
  /**
   * The OLEDDisplay instance to render
   */
  display: {
    type: Object as PropType<OLEDDisplay>,
    required: true,
  },
  /**
   * Pixel scale factor (how many canvas pixels per OLED pixel)
   */
  pixelScale: {
    type: Number,
    default: 4,
  },
  /**
   * Display preset name or custom preset object
   */
  preset: {
    type: [String, Object] as PropType<string | DisplayPreset>,
    default: 'ssd1306-blue',
  },
  /**
   * Enable pixel glow effect (slight bloom around lit pixels)
   */
  enableGlow: {
    type: Boolean,
    default: true,
  },
  /**
   * Enable sub-pixel rendering for more realistic look
   */
  enableSubPixel: {
    type: Boolean,
    default: false,
  },
  /**
   * Show pixel grid lines
   */
  showGrid: {
    type: Boolean,
    default: false,
  },
  /**
   * Auto-refresh interval in ms (0 = manual refresh only)
   */
  refreshInterval: {
    type: Number,
    default: 50,
  },
  /**
   * Yellow/blue split mode for dual-color OLEDs (rows 0-15 yellow)
   */
  dualColor: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits<{
  (e: 'click', x: number, y: number): void;
  (e: 'refresh'): void;
}>();

const canvas = ref<HTMLCanvasElement | null>(null);
const refreshTimer = ref<number | null>(null);

// Get the current display preset
const currentPreset = computed<DisplayPreset>(() => {
  if (typeof props.preset === 'string') {
    return DISPLAY_PRESETS[props.preset] || DISPLAY_PRESETS['ssd1306-blue'];
  }
  return props.preset;
});

// Canvas dimensions
const canvasWidth = computed(() => props.display.width * props.pixelScale);
const canvasHeight = computed(() => props.display.height * props.pixelScale);

/**
 * Render the framebuffer to canvas with OLED effects
 */
function render(): void {
  const cvs = canvas.value;
  if (!cvs) return;

  const ctx = cvs.getContext('2d');
  if (!ctx) return;

  const buffer = props.display.getBuffer();
  const width = props.display.width;
  const height = props.display.height;
  const scale = props.pixelScale;
  const preset = currentPreset.value;

  // Create image data
  const imageData = ctx.createImageData(canvasWidth.value, canvasHeight.value);
  const data = imageData.data;

  // Yellow color for dual-color mode (top 16 rows)
  const yellowOn = { r: 255, g: 220, b: 50 };
  const blueOn = { r: 100, g: 180, b: 255 };

  // Render each OLED pixel
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      // Get pixel from framebuffer (vertical byte packing)
      const byteIndex = x + Math.floor(y / 8) * width;
      const bitMask = 1 << (y & 7);
      const isOn = (buffer[byteIndex] & bitMask) !== 0;

      // Determine pixel color
      let pixelColor: { r: number; g: number; b: number };
      if (isOn) {
        if (props.dualColor && y < 16) {
          pixelColor = yellowOn;
        } else if (props.dualColor) {
          pixelColor = blueOn;
        } else {
          pixelColor = preset.pixelOn;
        }
      } else {
        pixelColor = preset.pixelOff;
      }

      // Apply contrast
      pixelColor = {
        r: Math.min(255, pixelColor.r * preset.contrast),
        g: Math.min(255, pixelColor.g * preset.contrast),
        b: Math.min(255, pixelColor.b * preset.contrast),
      };

      // Scale up pixel to canvas
      for (let sy = 0; sy < scale; sy++) {
        for (let sx = 0; sx < scale; sx++) {
          const px = x * scale + sx;
          const py = y * scale + sy;
          const idx = (py * canvasWidth.value + px) * 4;

          // Apply glow effect (brighter center, dimmer edges)
          let intensity = 1.0;
          if (props.enableGlow && isOn) {
            const centerX = scale / 2;
            const centerY = scale / 2;
            const dist = Math.sqrt(
              Math.pow(sx - centerX, 2) + Math.pow(sy - centerY, 2)
            );
            const maxDist = Math.sqrt(2) * (scale / 2);
            intensity = 1.0 - (dist / maxDist) * preset.glow;
          }

          // Apply sub-pixel rendering (RGB stripes)
          if (props.enableSubPixel && isOn) {
            const subPixelPos = sx % 3;
            if (subPixelPos === 0) {
              data[idx] = pixelColor.r * intensity * 1.2;
              data[idx + 1] = pixelColor.g * intensity * 0.8;
              data[idx + 2] = pixelColor.b * intensity * 0.8;
            } else if (subPixelPos === 1) {
              data[idx] = pixelColor.r * intensity * 0.8;
              data[idx + 1] = pixelColor.g * intensity * 1.2;
              data[idx + 2] = pixelColor.b * intensity * 0.8;
            } else {
              data[idx] = pixelColor.r * intensity * 0.8;
              data[idx + 1] = pixelColor.g * intensity * 0.8;
              data[idx + 2] = pixelColor.b * intensity * 1.2;
            }
          } else {
            data[idx] = pixelColor.r * intensity;
            data[idx + 1] = pixelColor.g * intensity;
            data[idx + 2] = pixelColor.b * intensity;
          }
          data[idx + 3] = 255; // Alpha
        }
      }
    }
  }

  // Draw grid lines if enabled
  if (props.showGrid) {
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        // Draw right and bottom edge of each pixel
        // Right edge
        const rx = (x + 1) * scale - 1;
        for (let sy = 0; sy < scale; sy++) {
          const py = y * scale + sy;
          const idx = (py * canvasWidth.value + rx) * 4;
          data[idx] = Math.min(255, data[idx] + 20);
          data[idx + 1] = Math.min(255, data[idx + 1] + 20);
          data[idx + 2] = Math.min(255, data[idx + 2] + 20);
        }
        // Bottom edge
        const by = (y + 1) * scale - 1;
        for (let sx = 0; sx < scale; sx++) {
          const px = x * scale + sx;
          const idx = (by * canvasWidth.value + px) * 4;
          data[idx] = Math.min(255, data[idx] + 20);
          data[idx + 1] = Math.min(255, data[idx + 1] + 20);
          data[idx + 2] = Math.min(255, data[idx + 2] + 20);
        }
      }
    }
  }

  ctx.putImageData(imageData, 0, 0);
  emit('refresh');
}

/**
 * Handle canvas click
 */
function handleClick(event: MouseEvent): void {
  const cvs = canvas.value;
  if (!cvs) return;

  const rect = cvs.getBoundingClientRect();
  const scaleX = cvs.width / rect.width;
  const scaleY = cvs.height / rect.height;

  const canvasX = (event.clientX - rect.left) * scaleX;
  const canvasY = (event.clientY - rect.top) * scaleY;

  const oledX = Math.floor(canvasX / props.pixelScale);
  const oledY = Math.floor(canvasY / props.pixelScale);

  emit('click', oledX, oledY);
}

/**
 * Start auto-refresh timer
 */
function startRefreshTimer(): void {
  stopRefreshTimer();
  if (props.refreshInterval > 0) {
    refreshTimer.value = window.setInterval(render, props.refreshInterval);
  }
}

/**
 * Stop auto-refresh timer
 */
function stopRefreshTimer(): void {
  if (refreshTimer.value !== null) {
    clearInterval(refreshTimer.value);
    refreshTimer.value = null;
  }
}

// Lifecycle
onMounted(() => {
  render();
  startRefreshTimer();
});

onUnmounted(() => {
  stopRefreshTimer();
});

// Watch for changes
watch(() => props.refreshInterval, startRefreshTimer);
watch(() => props.display, render, { deep: true });
watch(() => props.pixelScale, render);
watch(() => props.preset, render);
watch(() => props.enableGlow, render);
watch(() => props.showGrid, render);
watch(() => props.dualColor, render);

// Expose render method for manual refresh
defineExpose({ render });
</script>

<template>
  <div class="oled-emulator-wrapper">
    <canvas
      ref="canvas"
      :width="canvasWidth"
      :height="canvasHeight"
      class="oled-canvas"
      @click="handleClick"
    />
    <div class="oled-bezel" v-if="$slots.bezel">
      <slot name="bezel" />
    </div>
  </div>
</template>

<style scoped>
.oled-emulator-wrapper {
  display: inline-block;
  position: relative;
  background: #1a1a1a;
  padding: 8px;
  border-radius: 6px;
  box-shadow: 
    0 2px 8px rgba(0, 0, 0, 0.4),
    inset 0 1px 0 rgba(255, 255, 255, 0.05);
}

.oled-canvas {
  display: block;
  border-radius: 2px;
  image-rendering: pixelated;
  image-rendering: crisp-edges;
}

.oled-bezel {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  pointer-events: none;
}
</style>
