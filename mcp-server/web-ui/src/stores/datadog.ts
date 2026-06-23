import { defineStore } from "pinia";
import { ref } from "vue";
import { api } from "../api/client";
import { useWsStore } from "./ws";

export interface DDConfig {
  enabled: boolean;
  site: string;
  scrub: string;
  collector: string;
  host: string;
  ship_debug: boolean;
  has_key: boolean;
  key_hint: string;
  is_client_token: boolean;
}

export interface DDStats {
  running: boolean;
  sent_logs: number;
  sent_metrics: number;
  cycles: number;
  last_error: string | null;
  last_cycle_ts: number | null;
}

export interface DDStatus {
  config: DDConfig;
  stats: DDStats;
}

export const useDatadogStore = defineStore("datadog", () => {
  const status = ref<DDStatus | null>(null);

  async function load() {
    status.value = await api.get<DDStatus>("/api/datadog");
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("datadog.update", (s: DDStatus) => {
      status.value = s;
    });
    load();
  }

  // Send only the fields that changed. Omit api_key to keep the existing one.
  async function save(updates: Record<string, unknown>) {
    status.value = await api.put<DDStatus>("/api/datadog", updates);
  }

  async function test(): Promise<{ ok: boolean; error: string | null }> {
    return api.post("/api/datadog/test");
  }

  return { status, load, init, save, test };
});
