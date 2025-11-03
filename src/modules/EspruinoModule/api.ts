// Espruino E Object TypeScript Definitions
// Based on Espruino's built-in EventEmitter system

// Event-specific payload types
interface TouchEvent {
  x: number;
  y: number;
  b: number; // 0 for released, 1 for pressed
}

type ErrorFlag = string;

// Generic event listener type for Espruino
type EspruinoEventListener<T = any> = (...args: T[]) => void;

// E Object Interface - Espruino's global event emitter
interface EspruinoE {
  // Event listener registration - overloaded for built-in events
  on(event: "init", listener: () => void): void;
  on(event: "kill", listener: () => void): void;
  on(event: "errorFlag", listener: (errorFlags: ErrorFlag[]) => void): void;
  on(event: "touch", listener: (event: TouchEvent) => void): void;
  on(event: "comparator", listener: (direction: number) => void): void;
  on(event: string, listener: EspruinoEventListener): void;

  // addListener - alias for on()
  addListener(event: "init", listener: () => void): void;
  addListener(event: "kill", listener: () => void): void;
  addListener(
    event: "errorFlag",
    listener: (errorFlags: ErrorFlag[]) => void
  ): void;
  addListener(event: "touch", listener: (event: TouchEvent) => void): void;
  addListener(event: "comparator", listener: (direction: number) => void): void;
  addListener(event: string, listener: EspruinoEventListener): void;

  // prependListener - add listener at the beginning of the listener array
  prependListener(event: "init", listener: () => void): void;
  prependListener(event: "kill", listener: () => void): void;
  prependListener(
    event: "errorFlag",
    listener: (errorFlags: ErrorFlag[]) => void
  ): void;
  prependListener(event: "touch", listener: (event: TouchEvent) => void): void;
  prependListener(
    event: "comparator",
    listener: (direction: number) => void
  ): void;
  prependListener(event: string, listener: EspruinoEventListener): void;

  // Emit an event with optional arguments
  emit(event: string, ...args: any[]): void;

  // Remove a specific listener
  removeListener(event: string, listener: EspruinoEventListener): void;

  // Remove all listeners for an event, or all listeners if event is undefined
  removeAllListeners(event?: string): void;

  // Stop event propagation during event handling
  stopEventPropagation(): void;
}

// Declare the global E object
declare const E: EspruinoE;

// Meshtastic Protocol Port Numbers
// For any new 'apps' that run on the device or via sister apps on phones/PCs
// they should pick and use a unique 'portnum' for their application.
// Port number ranges:
// 0-63     Core Meshtastic use
// 64-127   Registered 3rd party apps
// 256-511  Private applications
const PortNum = {
  /** Deprecated: A message from outside the mesh (formerly OPAQUE) */
  UNKNOWN_APP: 0,

  /** Simple UTF-8 text message */
  TEXT_MESSAGE_APP: 1,

  /** Built-in GPIO/remote hardware control */
  REMOTE_HARDWARE_APP: 2,

  /** Built-in position messaging */
  POSITION_APP: 3,

  /** Built-in user info */
  NODEINFO_APP: 4,

  /** Protocol control packets for mesh routing */
  ROUTING_APP: 5,

  /** Admin control packets */
  ADMIN_APP: 6,

  /** Compressed text messages (Unishox2) */
  TEXT_MESSAGE_COMPRESSED_APP: 7,

  /** Waypoint messages */
  WAYPOINT_APP: 8,

  /** Audio payloads (codec2 frames, 2.4 GHz only) */
  AUDIO_APP: 9,

  /** Detection sensor messages */
  DETECTION_SENSOR_APP: 10,

  /** Critical alert messages */
  ALERT_APP: 11,

  /** Key verification requests */
  KEY_VERIFICATION_APP: 12,

  /** Ping service for testing */
  REPLY_APP: 32,

  /** Python IP tunnel feature */
  IP_TUNNEL_APP: 33,

  /** Paxcounter integration */
  PAXCOUNTER_APP: 34,

  /** Hardware serial interface (38400 8N1, max 240 bytes) */
  SERIAL_APP: 64,

  /** Store and forward app */
  STORE_FORWARD_APP: 65,

  /** Range test module */
  RANGE_TEST_APP: 66,

  /** Telemetry data */
  TELEMETRY_APP: 67,

  /** Zero-GPS positioning system */
  ZPS_APP: 68,

  /** Linux native app simulator */
  SIMULATOR_APP: 69,

  /** Traceroute functionality */
  TRACEROUTE_APP: 70,

  /** Network neighbor info aggregation */
  NEIGHBORINFO_APP: 71,

  /** Official Meshtastic ATAK plugin */
  ATAK_PLUGIN: 72,

  /** Unencrypted node info for MQTT map */
  MAP_REPORT_APP: 73,

  /** PowerStress monitoring */
  POWERSTRESS_APP: 74,

  /** Reticulum Network Stack tunnel */
  RETICULUM_TUNNEL_APP: 76,

  /** Arbitrary telemetry (CayenneLLP) */
  CAYENNE_APP: 77,

  /** Private applications (use >= 256) */
  PRIVATE_APP: 256,

  /** ATAK Forwarder Module (libcotshrink) */
  ATAK_FORWARDER: 257,

  /** Maximum allowed port number */
  MAX: 511,
} as const;

type PortNumValue = (typeof PortNum)[keyof typeof PortNum];

type NodeId = number;

type AnyMessageRxCallback = (
  type: PortNumValue,
  from: NodeId,
  msg: string
) => void;

type MessageRxCallback = (from: NodeId, msg: string) => void;

// Meshtastic API
const Meshtastic = {
  /** Port number constants for Meshtastic applications */
  PortNum,

  onMessage(type: PortNumValue, callback: AnyMessageRxCallback) {
    const eventName = `message:${type}`;
    E.on(eventName, callback);
    return () => {
      E.removeListener(eventName, callback);
    };
  },
  onTextMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.TEXT_MESSAGE_APP, callback);
  },
  onAudioMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.AUDIO_APP, callback);
  },
  onPositionMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.POSITION_APP, callback);
  },
  onNodeInfoMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.NODEINFO_APP, callback);
  },
  onRoutingMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ROUTING_APP, callback);
  },
  onAdminMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ADMIN_APP, callback);
  },
  onTextMessageCompressedMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.TEXT_MESSAGE_COMPRESSED_APP, callback);
  },
  onWaypointMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.WAYPOINT_APP, callback);
  },
  onDetectionSensorMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.DETECTION_SENSOR_APP, callback);
  },
  onAlertMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ALERT_APP, callback);
  },
  onKeyVerificationMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.KEY_VERIFICATION_APP, callback);
  },
  onReplyMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.REPLY_APP, callback);
  },
  onIPTunnelMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.IP_TUNNEL_APP, callback);
  },
  onPaxcounterMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.PAXCOUNTER_APP, callback);
  },
  onSerialMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.SERIAL_APP, callback);
  },
  onStoreForwardMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.STORE_FORWARD_APP, callback);
  },
  onRangeTestMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.RANGE_TEST_APP, callback);
  },
  onTelemetryMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.TELEMETRY_APP, callback);
  },
  onZPSMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ZPS_APP, callback);
  },
  onSimulatorMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.SIMULATOR_APP, callback);
  },
  onTracerouteMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.TRACEROUTE_APP, callback);
  },
  onNeighborInfoMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.NEIGHBORINFO_APP, callback);
  },
  onATAKPluginMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ATAK_PLUGIN, callback);
  },
  onMapReportMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.MAP_REPORT_APP, callback);
  },
  onPowerStressMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.POWERSTRESS_APP, callback);
  },
  onReticulumTunnelMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.RETICULUM_TUNNEL_APP, callback);
  },
  onCayenneMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.CAYENNE_APP, callback);
  },
  onPrivateMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.PRIVATE_APP, callback);
  },
  onATAKForwarderMessage(callback: MessageRxCallback) {
    return this.onMessage(PortNum.ATAK_FORWARDER, callback);
  },
};

// Attach to global scope for Espruino
declare var global: any;
global.Meshtastic = Meshtastic;
global.PortNum = PortNum;
