// Meshtastic Protocol Port Numbers
// For any new 'apps' that run on the device or via sister apps on phones/PCs
// they should pick and use a unique 'portnum' for their application.
// Port number ranges:
// 0-63     Core Meshtastic use
// 64-127   Registered 3rd party apps
// 256-511  Private applications
export const PortNum = {
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

export type PortNumValue = (typeof PortNum)[keyof typeof PortNum];

export type NodeId = number;

export type EventCallback = (data: any) => void;
export type AnyMessageRxCallback<TData = any> = (
  from: NodeId,
  data: TData
) => void;
export type TextMessageRxCallback = AnyMessageRxCallback<string>;
export type BinaryMessageRxCallback = AnyMessageRxCallback<Uint8Array>;

// Espruino makes every Object an EventEmitterLike, so we can use the same methods here.
export type EventEmitterLike = {
  on(event: string, listener: EventCallback): void;
  emit(event: string, data: any): void;
  removeListener(event: string, listener: (...args: any[]) => void): void;
};

export type MeshtasticApi = EventEmitterLike & {
  hello(): void;
  echo(message: string): void;
  ping(message: string): string;
  sendMessage(portNum: PortNumValue, to: NodeId, message: any): void;
  sendTextMessage(to: NodeId, message: string): void;
  PortNum: typeof PortNum;
  onPortMessage<TData>(
    type: PortNumValue,
    callback: AnyMessageRxCallback<TData>
  ): () => void;
  onTextMessage(callback: TextMessageRxCallback): () => void;
  onAudioMessage(callback: BinaryMessageRxCallback): () => void;
  onPositionMessage(callback: BinaryMessageRxCallback): () => void;
  onNodeInfoMessage(callback: AnyMessageRxCallback): () => void;
  onRoutingMessage(callback: AnyMessageRxCallback): () => void;
  onAdminMessage(callback: AnyMessageRxCallback): () => void;
  onTextMessageCompressedMessage(callback: AnyMessageRxCallback): () => void;
  onWaypointMessage(callback: AnyMessageRxCallback): () => void;
  onDetectionSensorMessage(callback: AnyMessageRxCallback): () => void;
  onAlertMessage(callback: AnyMessageRxCallback): () => void;
  onKeyVerificationMessage(callback: AnyMessageRxCallback): () => void;
  onReplyMessage(callback: AnyMessageRxCallback): () => void;
  onIPTunnelMessage(callback: AnyMessageRxCallback): () => void;
  onPaxcounterMessage(callback: AnyMessageRxCallback): () => void;
  onSerialMessage(callback: AnyMessageRxCallback): () => void;
  onStoreForwardMessage(callback: AnyMessageRxCallback): () => void;
  onRangeTestMessage(callback: AnyMessageRxCallback): () => void;
  onTelemetryMessage(callback: AnyMessageRxCallback): () => void;
  onZPSMessage(callback: AnyMessageRxCallback): () => void;
  onSimulatorMessage(callback: AnyMessageRxCallback): () => void;
  onTracerouteMessage(callback: AnyMessageRxCallback): () => void;
  onNeighborInfoMessage(callback: AnyMessageRxCallback): () => void;
  onATAKPluginMessage(callback: AnyMessageRxCallback): () => void;
  onMapReportMessage(callback: AnyMessageRxCallback): () => void;
  onPowerStressMessage(callback: AnyMessageRxCallback): () => void;
  onReticulumTunnelMessage(callback: AnyMessageRxCallback): () => void;
  onCayenneMessage(callback: AnyMessageRxCallback): () => void;
  onPrivateMessage(callback: AnyMessageRxCallback): () => void;
  onATAKForwarderMessage(callback: AnyMessageRxCallback): () => void;
};

const __handlers: Record<string, EventCallback[]> = {};

export type TextMessageParams = [NodeId, string];
export type BinaryMessageParams = [NodeId, Uint8Array];
export type AnyMessageParams = [PortNumValue, NodeId, any];

function notImplemented(): never {
  throw new Error("Not implemented");
}
// Meshtastic API
const Meshtastic: MeshtasticApi = {
  hello() {
    console.log("Hello from JS");
  },
  echo(message: string) {
    console.log(`Echoing message from JS: ${message}`);
  },
  ping(message: string) {
    return message;
  },
  sendMessage(portNum: PortNumValue, to: NodeId, message: any) {
    MeshtasticNative.addPendingMessage(portNum, to, message);
  },
  sendTextMessage(to: NodeId, message: string) {
    Meshtastic.sendMessage(PortNum.TEXT_MESSAGE_APP, to, message);
  },
  /** Port number constants for Meshtastic applications */
  PortNum,
  on(event: string, listener: EventCallback) {
    console.log(`Registering listener for event: ${event}`);
    if (!(event in __handlers)) {
      __handlers[event] = [];
    }
    __handlers[event].push(listener);
    return () => {
      Meshtastic.removeListener(event, listener);
    };
  },
  emit(event: string, data: any) {
    console.log(`Emitting event from JS: ${event}`);
    console.log(`Data type: ${typeof data}`);
    if (!(event in __handlers)) {
      return;
    }
    __handlers[event].forEach((handler) => {
      handler(data);
    });
  },
  removeListener(event: string, listener: EventCallback) {
    console.log(`Removing listener for event: ${event}`);
    if (!(event in __handlers)) {
      return;
    }
    __handlers[event] = __handlers[event].filter((l) => l !== listener);
  },
  onPortMessage<TData>(
    portNum: PortNumValue,
    callback: AnyMessageRxCallback<TData>
  ) {
    const eventName = `message:${portNum}`;

    Meshtastic.on(eventName, (data) => {
      console.log(`Inside callback for event: ${eventName}`);
      console.log(`Data type: ${typeof data}`);
      console.log(`Data length: ${data.length}`);
      console.log(`Data[0] type: ${typeof data[0]}`);
      console.log(`Data[1] type: ${typeof data[1]}`);
      callback(data[0], data[1]);
    });
    return () => {
      Meshtastic.removeListener(eventName, callback);
    };
  },
  onTextMessage(callback: TextMessageRxCallback) {
    return Meshtastic.onPortMessage<string>(PortNum.TEXT_MESSAGE_APP, callback);
  },
  onAudioMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onPositionMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onNodeInfoMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onRoutingMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onAdminMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onTextMessageCompressedMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onDetectionSensorMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onAlertMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onKeyVerificationMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onReplyMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onIPTunnelMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onPaxcounterMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onSerialMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onStoreForwardMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onRangeTestMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onTelemetryMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onZPSMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onSimulatorMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onTracerouteMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onNeighborInfoMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onATAKPluginMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onMapReportMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onPowerStressMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onReticulumTunnelMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onCayenneMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onPrivateMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onATAKForwarderMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
  onWaypointMessage(callback: AnyMessageRxCallback) {
    notImplemented();
  },
};

const MeshtasticNative = {
  pendingMessages: [],
  addPendingMessage(portNum: PortNumValue, to: NodeId, message: string) {
    this.pendingMessages.push({ portNum, to, message });
  },
  flushPendingMessages() {
    this.pendingMessages.forEach(({ portNum, to, message }) => {
      MeshtasticNative.sendMessage(portNum, to, message);
    });
    this.pendingMessages = [];
  },
  sendMessage(portNum: PortNumValue, to: NodeId, message: string) {
    throw new Error("sendTextMessage is a native function");
  },
};

// Attach to global scope for Espruino
declare var global: any;
global.Meshtastic = Meshtastic;
global.PortNum = PortNum;
global.MeshtasticNative = MeshtasticNative;

/**
 * This is for test purposes only and should be removed before production.
 */
Meshtastic.onTextMessage((from, data) => {
  // Meshtastic.sendTextMessage(from, `<EspruinoModule> You said: ${data}`);
  // Meshtastic.hello();
  Meshtastic.sendTextMessage(0xef6b3731, "DM from EspruinoModule");
});
