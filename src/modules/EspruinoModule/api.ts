import {
  MeshtasticApi,
  EventCallback,
  PortNumValue,
  NodeId,
  AnyMessageRxCallback,
  TextMessageRxCallback,
  PortNum,
} from "./types";

const __handlers: Record<string, EventCallback[]> = {};

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
    // console.log(`Registering listener for event: ${event}`);
    if (!(event in __handlers)) {
      __handlers[event] = [];
    }
    __handlers[event].push(listener);
    return () => {
      Meshtastic.removeListener(event, listener);
    };
  },
  emit(event: string, data: any) {
    // console.log(`Emitting event from JS: ${event}`);
    // console.log(`Data type: ${typeof data}`);
    if (!(event in __handlers)) {
      return;
    }
    __handlers[event].forEach((handler) => {
      handler(data);
    });
  },
  removeListener(event: string, listener: EventCallback) {
    // console.log(`Removing listener for event: ${event}`);
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
      // console.log(`Inside callback for event: ${eventName}`);
      // console.log(`Data type: ${typeof data}`);
      // console.log(`Data length: ${data.length}`);
      // console.log(`Data[0] type: ${typeof data[0]}`);
      // console.log(`Data[1] type: ${typeof data[1]}`);
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
  pendingMessages: [] as { portNum: PortNumValue; to: NodeId; message: any }[],
  addPendingMessage(portNum: PortNumValue, to: NodeId, message: any) {
    MeshtasticNative.pendingMessages.push({ portNum, to, message });
    console.log(
      `Added pending message to queue: ${portNum}, ${to}, ${message}`
    );
  },
  flushPendingMessages() {
    const message = MeshtasticNative.pendingMessages.shift();

    if (!message) {
      // console.log("No pending messages to flush");
      return;
    }
    const success = MeshtasticNative.sendMessage(message);
    if (!success) {
      console.log("Script: Failed to send message, queueing again");
      MeshtasticNative.pendingMessages.unshift(message);
    } else {
      console.log("Script: Message sent successfully");
    }
  },
  sendMessage(params: {
    portNum: PortNumValue;
    to: NodeId;
    message: any;
  }): boolean {
    throw new Error("sendMessage is a native function");
  },
};

// Attach to global scope for Espruino
declare var global: any;
global.Meshtastic = Meshtastic;
global.PortNum = PortNum;
global.MeshtasticNative = MeshtasticNative;
