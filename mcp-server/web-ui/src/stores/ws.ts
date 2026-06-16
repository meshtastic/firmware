// Single WebSocket to /ws. Other stores register topic handlers; this store
// owns the socket, (re)subscribes on (re)connect, and dispatches frames.

import { defineStore } from "pinia";
import { ref } from "vue";

type Handler = (data: any) => void;

export const useWsStore = defineStore("ws", () => {
  const connected = ref(false);
  let socket: WebSocket | null = null;
  const handlers = new Map<string, Set<Handler>>();
  const subscribed = new Set<string>();
  let reconnectTimer: number | null = null;

  function url(): string {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/ws`;
  }

  function connect() {
    if (socket && socket.readyState <= WebSocket.OPEN) return;
    socket = new WebSocket(url());
    socket.onopen = () => {
      connected.value = true;
      // Re-subscribe everything after a reconnect.
      for (const topic of subscribed) send({ action: "subscribe", topic });
    };
    socket.onclose = () => {
      connected.value = false;
      scheduleReconnect();
    };
    socket.onerror = () => socket?.close();
    socket.onmessage = (ev) => {
      try {
        const frame = JSON.parse(ev.data);
        const hs = handlers.get(frame.topic);
        if (hs) for (const h of hs) h(frame.data);
      } catch {
        /* ignore malformed */
      }
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer != null) return;
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, 1500);
  }

  function send(msg: object) {
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(msg));
    }
  }

  function subscribe(topic: string, handler: Handler) {
    if (!handlers.has(topic)) handlers.set(topic, new Set());
    handlers.get(topic)!.add(handler);
    if (!subscribed.has(topic)) {
      subscribed.add(topic);
      send({ action: "subscribe", topic });
    }
  }

  function unsubscribe(topic: string, handler: Handler) {
    handlers.get(topic)?.delete(handler);
    if (handlers.get(topic)?.size === 0) {
      handlers.delete(topic);
      subscribed.delete(topic);
      send({ action: "unsubscribe", topic });
    }
  }

  return { connected, connect, subscribe, unsubscribe };
});
