<script setup lang="ts">
/**
 * Demo.vue - Interactive demo of the OLED emulator
 * 
 * Shows multiple display configurations and screen types
 */

import { ref, computed, watch, onMounted } from 'vue';
import OLEDEmulator from './OLEDEmulator.vue';
import { DISPLAY_PRESETS, type DisplayPreset } from './DisplayPresets';
import { OLEDDisplay, type OLEDDISPLAY_GEOMETRY } from './OLEDDisplay';
import { Font_6x8 } from './SimpleFonts';
import * as Screens from './ScreenRenderers';

// Available screen demos
const SCREEN_OPTIONS = [
  { id: 'boot', name: 'Boot Screen' },
  { id: 'nodeInfo', name: 'Node Info' },
  { id: 'message', name: 'Message' },
  { id: 'gps', name: 'GPS' },
  { id: 'nodeList', name: 'Node List' },
  { id: 'system', name: 'System' },
  { id: 'compass', name: 'Compass' },
  { id: 'progress', name: 'Progress' },
  { id: 'custom', name: 'Custom Drawing' },
];

// Geometry options
const GEOMETRY_OPTIONS: OLEDDISPLAY_GEOMETRY[] = [
  '128x64',
  '128x32',
  '64x48',
  '128x128',
];

// State
const selectedPreset = ref<string>('ssd1306-blue');
const selectedGeometry = ref<OLEDDISPLAY_GEOMETRY>('128x64');
const selectedScreen = ref<string>('boot');
const pixelScale = ref<number>(4);
const enableGlow = ref<boolean>(true);
const showGrid = ref<boolean>(false);
const dualColor = ref<boolean>(false);

// Create display instance
const display = ref<OLEDDisplay>(new OLEDDisplay(selectedGeometry.value));

// Emulator ref for manual refresh
const emulatorRef = ref<InstanceType<typeof OLEDEmulator> | null>(null);

// Mock data for demos
const mockNodeInfo = {
  shortName: 'MESH',
  longName: 'Meshtastic Node',
  nodeId: '!abcd1234',
  batteryLevel: 85,
  lastHeard: '2m ago',
  snr: 12.5,
  hopsAway: 1,
};

const mockMessage = {
  from: 'Alice',
  text: 'Hello from the mesh network! How is the signal over there?',
  time: '14:32',
  channel: 'LongFast',
};

const mockGPS = {
  latitude: 37.7749,
  longitude: -122.4194,
  altitude: 42,
  satellites: 8,
  hasLock: true,
  speed: 5.2,
};

const mockNodes = [
  { shortName: 'ALICE', longName: 'Alice Node', lastHeard: '1m', snr: 15 },
  { shortName: 'BOB', longName: 'Bob Node', lastHeard: '3m', snr: 8 },
  { shortName: 'CAROL', longName: 'Carol Node', lastHeard: '5m', snr: -2 },
  { shortName: 'DAVE', longName: 'Dave Node', lastHeard: '12m', snr: 4 },
  { shortName: 'EVE', longName: 'Eve Node', lastHeard: '1h', snr: -5 },
];

const mockSystem = {
  uptime: '2d 14h 32m',
  channelUtil: 12.4,
  airUtil: 3.2,
  batteryVoltage: 4.12,
  nodes: 15,
  freeMemory: 142000,
};

const mockCompass = ref({
  heading: 45,
  bearing: 120,
  distance: 1250,
  targetName: 'Alice',
});

const mockProgress = ref({
  progress: 0,
  title: 'Updating Firmware',
  status: 'Downloading...',
});

// Code playground state - example from firmware's drawNodeInfo pattern
const playgroundCode = ref(`// Example: Node Info Screen (based on UIRenderer.cpp)
// Real firmware pattern from src/graphics/draw/UIRenderer.cpp

display.clear();
display.setFont(Font_6x8);
display.setColor('WHITE');

const SCREEN_WIDTH = display.getWidth();
const FONT_HEIGHT_SMALL = 8;

// === Header bar (inverted style from SharedUIDisplay.cpp) ===
const titleStr = "*MESH*";  // Short name for favorite node
display.fillRect(0, 0, SCREEN_WIDTH, FONT_HEIGHT_SMALL + 2);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(SCREEN_WIDTH / 2, 1, titleStr);
display.setColor('WHITE');

// === Dynamic row positioning (firmware pattern) ===
let line = 1;  // Start after header
const lineHeight = FONT_HEIGHT_SMALL + 2;

// Long name row
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(0, line * lineHeight, "Meshtastic Node");
line++;

// Signal and Hops row (firmware combines these)
// In real firmware: snprintf(signalHopsStr, sizeof(signalHopsStr), " Sig:%s", qualityLabel);
display.drawString(0, line * lineHeight, " Sig:Good [2 hops]");
line++;

// Last heard
const lastHeard = "Heard: 2m ago";
display.drawString(0, line * lineHeight, lastHeard);
line++;

// Distance (if GPS available)
const distance = "1.2 km away";
display.drawString(0, line * lineHeight, distance);
line++;

// === Battery icon (from images.h pattern) ===
// Simplified version of batteryBitmap_h drawing
const battX = SCREEN_WIDTH - 20;
display.drawRect(battX, 2, 16, 6);  // Battery outline
display.fillRect(battX + 16, 3, 2, 4);  // Battery tip
display.fillRect(battX + 2, 3, 10, 4);  // Fill level ~75%`);

const playgroundError = ref<string | null>(null);
const playgroundExpanded = ref(true);

// Example code snippets from firmware
const codeExamples = [
  {
    name: 'Node Info Screen',
    code: `// Node Info Screen (UIRenderer::drawNodeInfo pattern)
display.clear();
display.setFont(Font_6x8);

const SCREEN_WIDTH = display.getWidth();
const FONT_HEIGHT = 8;

// Inverted header (SharedUIDisplay.cpp:drawCommonHeader)
display.fillRect(0, 0, SCREEN_WIDTH, FONT_HEIGHT + 2);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(SCREEN_WIDTH / 2, 1, "*MESH*");
display.setColor('WHITE');

// Node info rows
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(0, 12, "Meshtastic Node");
display.drawString(0, 22, " Sig:Good [2 hops]");
display.drawString(0, 32, "Heard: 2m ago");
display.drawString(0, 42, "1.2 km away");

// Battery (simplified)
display.drawRect(108, 2, 16, 6);
display.fillRect(124, 3, 2, 4);
display.fillRect(110, 3, 10, 4);`
  },
  {
    name: 'WiFi Screen',
    code: `// WiFi Screen (DebugRenderer::drawFrameWiFi pattern)
display.clear();
display.setFont(Font_6x8);

const SCREEN_WIDTH = display.getWidth();

// Header
display.fillRect(0, 0, SCREEN_WIDTH, 10);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(SCREEN_WIDTH / 2, 1, "WiFi");
display.setColor('WHITE');

// Status lines (real firmware uses getTextPositions)
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(0, 12, "WiFi: Connected");
display.drawString(0, 22, "RSSI: -42");
display.drawString(0, 32, "IP: 192.168.1.42");
display.drawString(0, 42, "SSID: MyNetwork");
display.drawString(0, 52, "URL: meshtastic.local");`
  },
  {
    name: 'Message Screen',
    code: `// Message Screen pattern
display.clear();
display.setFont(Font_6x8);

const w = display.getWidth();

// Header with channel name
display.fillRect(0, 0, w, 10);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(w / 2, 1, "LongFast");
display.setColor('WHITE');

// Message content
display.setTextAlignment('TEXT_ALIGN_LEFT');

// Sender and time
display.drawString(0, 12, "From: Alice");
display.drawString(w - 30, 12, "14:32");

// Message text (firmware wraps long messages)
display.drawStringMaxWidth(0, 24, w, "Hello from the mesh! How is the signal?");

// Mail icon (from images.h: icon_mail)
display.drawRect(2, 54, 10, 7);
display.drawLine(2, 54, 7, 58);
display.drawLine(12, 54, 7, 58);`
  },
  {
    name: 'System Info',
    code: `// System Screen (DebugRenderer pattern)
display.clear();
display.setFont(Font_6x8);

const w = display.getWidth();

// Header
display.fillRect(0, 0, w, 10);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(w / 2, 1, "System");
display.setColor('WHITE');

// System stats (firmware format)
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(0, 12, "Uptime: 2d 14h");
display.drawString(0, 22, "ChUtil: 12.4%");
display.drawString(0, 32, "AirUtil: 3.2%");
display.drawString(0, 42, "Nodes: 15");

// Right-aligned values pattern
display.setTextAlignment('TEXT_ALIGN_RIGHT');
display.drawString(w, 52, "4.12V");`
  },
  {
    name: 'GPS Screen',
    code: `// GPS Screen pattern
display.clear();
display.setFont(Font_6x8);

const w = display.getWidth();
const h = display.getHeight();

// Header
display.fillRect(0, 0, w, 10);
display.setColor('BLACK');
display.setTextAlignment('TEXT_ALIGN_CENTER');
display.drawString(w / 2, 1, "GPS");
display.setColor('WHITE');

// GPS data
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(0, 12, "Lat:  37.7749");
display.drawString(0, 22, "Lon: -122.4194");
display.drawString(0, 32, "Alt: 42m");
display.drawString(0, 42, "Sats: 8");
display.drawString(0, 52, "Speed: 5.2 km/h");

// Satellite icon hint (from SATELLITE_IMAGE)
display.drawCircle(w - 12, 20, 6);
display.fillCircle(w - 12, 20, 2);`
  },
  {
    name: 'Custom Drawing',
    code: `// Custom drawing demo - all primitives
display.clear();
display.setFont(Font_6x8);
display.setColor('WHITE');

const w = display.getWidth();
const h = display.getHeight();

// Border
display.drawRect(0, 0, w, h);

// Diagonal cross
display.drawLine(0, 0, w-1, h-1);
display.drawLine(w-1, 0, 0, h-1);

// Circles
display.drawCircle(w/2, h/2, 20);
display.fillCircle(w/2, h/2, 8);

// Text at corners
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(2, 2, 'TL');
display.setTextAlignment('TEXT_ALIGN_RIGHT');
display.drawString(w-2, 2, 'TR');
display.drawString(w-2, h-10, 'BR');
display.setTextAlignment('TEXT_ALIGN_LEFT');
display.drawString(2, h-10, 'BL');`
  }
];

const selectedExample = ref(0);

function loadExample(index: number): void {
  selectedExample.value = index;
  playgroundCode.value = codeExamples[index].code;
  runPlaygroundCode();
}

// Run playground code
function runPlaygroundCode(): void {
  playgroundError.value = null;
  try {
    display.value.clear();
    // Create a function with display and Font_6x8 in scope
    const fn = new Function(
      'display', 'Font_6x8', 'WHITE', 'BLACK', 'INVERSE',
      playgroundCode.value
    );
    fn(display.value, Font_6x8, 'WHITE', 'BLACK', 'INVERSE');
    // Force refresh
    emulatorRef.value?.render?.();
  } catch (e: unknown) {
    playgroundError.value = e instanceof Error ? e.message : String(e);
  }
}

// Animation state
let compassAnimFrame: number | null = null;
let progressAnimFrame: number | null = null;

// Render the selected screen
function renderScreen(): void {
  display.value.setFont(Font_6x8);
  
  switch (selectedScreen.value) {
    case 'boot':
      Screens.drawBootScreen(display.value, '2.5.19');
      break;
    case 'nodeInfo':
      Screens.drawNodeInfoScreen(display.value, mockNodeInfo);
      break;
    case 'message':
      Screens.drawMessageScreen(display.value, mockMessage);
      break;
    case 'gps':
      Screens.drawGPSScreen(display.value, mockGPS);
      break;
    case 'nodeList':
      Screens.drawNodeListScreen(display.value, mockNodes, 1);
      break;
    case 'system':
      Screens.drawSystemScreen(display.value, mockSystem);
      break;
    case 'compass':
      Screens.drawCompassScreen(display.value, mockCompass.value);
      break;
    case 'progress':
      Screens.drawProgressScreen(
        display.value, 
        mockProgress.value.title, 
        mockProgress.value.progress,
        mockProgress.value.status
      );
      break;
    case 'custom':
      runPlaygroundCode();
      break;
  }
}

// Custom drawing demo (default playground code)
function drawCustomDemo(): void {
  display.value.clear();
  display.value.setFont(Font_6x8);
  display.value.setColor('WHITE');
  
  const w = display.value.getWidth();
  const h = display.value.getHeight();
  
  // Border
  display.value.drawRect(0, 0, w, h);
  
  // Diagonal lines
  display.value.drawLine(0, 0, w, h);
  display.value.drawLine(w, 0, 0, h);
  
  // Center circle
  display.value.drawCircle(w / 2, h / 2, 15);
  display.value.fillCircle(w / 2, h / 2, 5);
  
  // Text
  display.value.setTextAlignment('TEXT_ALIGN_CENTER');
  display.value.drawString(w / 2, 4, 'Custom Drawing');
  display.value.drawString(w / 2, h - 14, 'OLED Emulator');
}

// Start compass animation
function startCompassAnimation(): void {
  stopCompassAnimation();
  
  const animate = () => {
    mockCompass.value.heading = (mockCompass.value.heading + 1) % 360;
    renderScreen();
    compassAnimFrame = requestAnimationFrame(animate);
  };
  
  compassAnimFrame = requestAnimationFrame(animate);
}

function stopCompassAnimation(): void {
  if (compassAnimFrame !== null) {
    cancelAnimationFrame(compassAnimFrame);
    compassAnimFrame = null;
  }
}

// Start progress animation
function startProgressAnimation(): void {
  stopProgressAnimation();
  mockProgress.value.progress = 0;
  
  const animate = () => {
    mockProgress.value.progress += 0.5;
    if (mockProgress.value.progress >= 100) {
      mockProgress.value.progress = 0;
      mockProgress.value.status = 'Starting over...';
    } else if (mockProgress.value.progress < 30) {
      mockProgress.value.status = 'Downloading...';
    } else if (mockProgress.value.progress < 70) {
      mockProgress.value.status = 'Installing...';
    } else {
      mockProgress.value.status = 'Finishing up...';
    }
    renderScreen();
    progressAnimFrame = requestAnimationFrame(animate);
  };
  
  progressAnimFrame = requestAnimationFrame(animate);
}

function stopProgressAnimation(): void {
  if (progressAnimFrame !== null) {
    cancelAnimationFrame(progressAnimFrame);
    progressAnimFrame = null;
  }
}

// Watch for geometry changes
watch(selectedGeometry, (newGeometry) => {
  display.value = new OLEDDisplay(newGeometry);
  renderScreen();
});

// Watch for screen changes
watch(selectedScreen, (newScreen) => {
  stopCompassAnimation();
  stopProgressAnimation();
  
  if (newScreen === 'compass') {
    startCompassAnimation();
  } else if (newScreen === 'progress') {
    startProgressAnimation();
  }
  
  renderScreen();
});

// Initial render
onMounted(() => {
  renderScreen();
});
</script>

<template>
  <div class="demo-container">
    <h1>Meshtastic OLED Emulator</h1>
    <p class="subtitle">Vue.js component for rendering firmware UI screens</p>
    
    <div class="demo-layout">
      <!-- Controls Panel -->
      <div class="controls-panel">
        <h3>Display Settings</h3>
        
        <div class="control-group">
          <label>Display Type:</label>
          <select v-model="selectedPreset">
            <option v-for="(preset, key) in DISPLAY_PRESETS" :key="key" :value="key">
              {{ preset.name }}
            </option>
          </select>
        </div>
        
        <div class="control-group">
          <label>Resolution:</label>
          <select v-model="selectedGeometry">
            <option v-for="geo in GEOMETRY_OPTIONS" :key="geo" :value="geo">
              {{ geo }}
            </option>
          </select>
        </div>
        
        <div class="control-group">
          <label>Pixel Scale:</label>
          <input type="range" v-model.number="pixelScale" min="1" max="8" />
          <span>{{ pixelScale }}x</span>
        </div>
        
        <div class="control-group">
          <label>
            <input type="checkbox" v-model="enableGlow" />
            Enable Pixel Glow
          </label>
        </div>
        
        <div class="control-group">
          <label>
            <input type="checkbox" v-model="showGrid" />
            Show Pixel Grid
          </label>
        </div>
        
        <div class="control-group">
          <label>
            <input type="checkbox" v-model="dualColor" />
            Dual Color (Yellow/Blue)
          </label>
        </div>
        
        <h3>Screen Demo</h3>
        
        <div class="control-group">
          <label>Screen:</label>
          <select v-model="selectedScreen">
            <option v-for="screen in SCREEN_OPTIONS" :key="screen.id" :value="screen.id">
              {{ screen.name }}
            </option>
          </select>
        </div>
        
        <button @click="renderScreen" class="refresh-btn">Refresh Display</button>
        
        <h3>Code Playground</h3>
        <button @click="playgroundExpanded = !playgroundExpanded" class="toggle-btn">
          {{ playgroundExpanded ? '▼ Hide Editor' : '▶ Show Editor' }}
        </button>
      </div>
      
      <!-- Display Panel -->
      <div class="display-panel">
        <div class="display-wrapper">
          <OLEDEmulator
            ref="emulatorRef"
            :display="display"
            :pixel-scale="pixelScale"
            :preset="selectedPreset"
            :enable-glow="enableGlow"
            :show-grid="showGrid"
            :dual-color="dualColor"
            :refresh-interval="50"
          />
        </div>
        
        <div class="display-info">
          <span>{{ display.getWidth() }}×{{ display.getHeight() }} pixels</span>
          <span>{{ display.getBufferSize() }} bytes framebuffer</span>
        </div>
        
        <!-- Code Playground Editor -->
        <div v-if="playgroundExpanded" class="playground-section">
          <div class="playground-header">
            <span>Interactive Code Editor</span>
            <button @click="runPlaygroundCode" class="run-btn">▶ Run Code</button>
          </div>
          
          <!-- Example Selector -->
          <div class="example-selector">
            <label>Load Example:</label>
            <div class="example-buttons">
              <button 
                v-for="(example, index) in codeExamples" 
                :key="index"
                @click="loadExample(index)"
                :class="{ active: selectedExample === index }"
                class="example-btn"
              >
                {{ example.name }}
              </button>
            </div>
          </div>
          
          <textarea
            v-model="playgroundCode"
            class="code-editor"
            spellcheck="false"
            @keydown.ctrl.enter="runPlaygroundCode"
            @keydown.meta.enter="runPlaygroundCode"
          ></textarea>
          <div v-if="playgroundError" class="error-message">
            Error: {{ playgroundError }}
          </div>
          <div class="playground-hint">
            Press Ctrl+Enter (Cmd+Enter on Mac) to run • Examples based on real firmware code from src/graphics/
          </div>
        </div>
      </div>
    </div>
    
    <!-- Code Example -->
    <div class="code-section">
      <h3>Usage Example</h3>
      <pre><code>import { OLEDDisplay } from './OLEDDisplay';
import { Font_6x8 } from './OLEDFonts';
import OLEDEmulator from './OLEDEmulator.vue';

// Create display (matches firmware OLEDDisplay API)
const display = new OLEDDisplay('128x64');
display.setFont(Font_6x8);

// Draw using same API as firmware
display.clear();
display.drawString(0, 0, 'Hello Mesh!');
display.drawRect(0, 16, 100, 30);
display.fillCircle(64, 40, 10);

// In Vue template:
&lt;OLEDEmulator 
  :display="display"
  :pixel-scale="4"
  preset="ssd1306-blue"
/&gt;</code></pre>
    </div>
    
    <div class="info-section">
      <h3>About</h3>
      <p>
        This emulator provides a TypeScript port of the <code>OLEDDisplay</code> API used in
        Meshtastic firmware. It allows you to prototype and test screen layouts in the browser
        before deploying to hardware.
      </p>
      <h4>Features:</h4>
      <ul>
        <li>Same drawing API as esp8266-oled-ssd1306 library</li>
        <li>Multiple display sizes (128x64, 128x32, 64x48, 128x128)</li>
        <li>Realistic OLED pixel effects (glow, color temperature)</li>
        <li>Support for dual-color yellow/blue displays</li>
        <li>XBM image rendering for icons</li>
        <li>Font rendering with alignment support</li>
      </ul>
      <h4>Future: Protocol Bridge (Approach 3)</h4>
      <p>
        A future enhancement could connect this emulator to a real firmware instance 
        running on native/portduino, streaming the live framebuffer over WebSocket 
        for real-time display mirroring.
      </p>
    </div>
  </div>
</template>

<style scoped>
.demo-container {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
  max-width: 1200px;
  margin: 0 auto;
  padding: 20px;
  color: #e0e0e0;
  background: #121212;
  min-height: 100vh;
}

h1 {
  margin: 0;
  color: #4fc3f7;
}

.subtitle {
  color: #888;
  margin-top: 4px;
}

.demo-layout {
  display: flex;
  gap: 30px;
  margin-top: 30px;
}

.controls-panel {
  flex: 0 0 280px;
  background: #1e1e1e;
  padding: 20px;
  border-radius: 8px;
}

.controls-panel h3 {
  margin-top: 0;
  margin-bottom: 16px;
  color: #81c784;
  font-size: 14px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.control-group {
  margin-bottom: 16px;
}

.control-group label {
  display: block;
  margin-bottom: 6px;
  color: #aaa;
  font-size: 13px;
}

.control-group select,
.control-group input[type="range"] {
  width: 100%;
  padding: 8px;
  background: #2a2a2a;
  border: 1px solid #444;
  border-radius: 4px;
  color: #fff;
}

.control-group input[type="checkbox"] {
  margin-right: 8px;
}

.control-group span {
  margin-left: 8px;
  color: #888;
}

.refresh-btn {
  width: 100%;
  padding: 12px;
  background: #4fc3f7;
  color: #000;
  border: none;
  border-radius: 4px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.2s;
}

.refresh-btn:hover {
  background: #29b6f6;
}

.display-panel {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
}

.display-wrapper {
  background: #0a0a0a;
  padding: 20px;
  border-radius: 12px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.5);
}

.display-info {
  margin-top: 12px;
  display: flex;
  gap: 20px;
  color: #666;
  font-size: 12px;
}

.code-section {
  margin-top: 40px;
  background: #1e1e1e;
  padding: 20px;
  border-radius: 8px;
}

.code-section h3 {
  margin-top: 0;
  color: #81c784;
}

.code-section pre {
  background: #0d0d0d;
  padding: 16px;
  border-radius: 4px;
  overflow-x: auto;
}

.code-section code {
  color: #a5d6a7;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 13px;
  line-height: 1.5;
}

.info-section {
  margin-top: 30px;
  background: #1e1e1e;
  padding: 20px;
  border-radius: 8px;
}

.info-section h3 {
  margin-top: 0;
  color: #81c784;
}

.info-section h4 {
  color: #4fc3f7;
  margin-bottom: 8px;
}

.info-section p {
  color: #bbb;
  line-height: 1.6;
}

.info-section ul {
  color: #bbb;
  padding-left: 20px;
}

.info-section li {
  margin-bottom: 6px;
}

.info-section code {
  background: #2a2a2a;
  padding: 2px 6px;
  border-radius: 3px;
  color: #81c784;
}

/* Playground styles */
.playground-section {
  margin-top: 20px;
  width: 100%;
  max-width: 600px;
}

.playground-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.playground-header span {
  color: #81c784;
  font-size: 14px;
  font-weight: 600;
}

.run-btn {
  padding: 8px 16px;
  background: #4caf50;
  color: #fff;
  border: none;
  border-radius: 4px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.2s;
}

.run-btn:hover {
  background: #43a047;
}

.toggle-btn {
  width: 100%;
  padding: 10px;
  margin-top: 8px;
  background: #2a2a2a;
  color: #aaa;
  border: 1px solid #444;
  border-radius: 4px;
  cursor: pointer;
  text-align: left;
  transition: background 0.2s;
}

.toggle-btn:hover {
  background: #333;
}

.code-editor {
  width: 100%;
  height: 300px;
  padding: 12px;
  background: #0d0d0d;
  border: 1px solid #333;
  border-radius: 4px;
  color: #a5d6a7;
  font-family: 'SF Mono', 'Fira Code', 'Monaco', monospace;
  font-size: 13px;
  line-height: 1.5;
  resize: vertical;
  tab-size: 2;
}

.code-editor:focus {
  outline: none;
  border-color: #4fc3f7;
}

.error-message {
  margin-top: 8px;
  padding: 10px;
  background: #5c1a1a;
  border: 1px solid #d32f2f;
  border-radius: 4px;
  color: #ff8a80;
  font-family: monospace;
  font-size: 12px;
}

.playground-hint {
  margin-top: 8px;
  color: #666;
  font-size: 11px;
}

.example-selector {
  margin-bottom: 12px;
}

.example-selector label {
  display: block;
  color: #888;
  font-size: 12px;
  margin-bottom: 6px;
}

.example-buttons {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.example-btn {
  padding: 6px 12px;
  background: #2a2a2a;
  color: #aaa;
  border: 1px solid #444;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.example-btn:hover {
  background: #333;
  border-color: #4fc3f7;
  color: #4fc3f7;
}

.example-btn.active {
  background: #1a3a4a;
  border-color: #4fc3f7;
  color: #4fc3f7;
}

@media (max-width: 768px) {
  .demo-layout {
    flex-direction: column;
  }
  
  .controls-panel {
    flex: none;
  }
}
</style>
