import { defineStore } from "pinia";
import { computed, reactive } from "vue";
import { api } from "../api/client";
import type { Device } from "../types";
import { useWsStore } from "./ws";

export const useDevicesStore = defineStore("devices", () => {
  // Keyed by serial_number — a device.update with a new current_port is a field
  // update on the same entry, so the card "follows" the device across ports.
  const bySerial = reactive<Record<string, Device>>({});

  const list = computed(() =>
    Object.values(bySerial).sort((a, b) => {
      if (a.online !== b.online) return b.online - a.online;
      return (a.friendly_name || a.serial_number).localeCompare(
        b.friendly_name || b.serial_number,
      );
    }),
  );

  async function load() {
    const devices = await api.get<Device[]>("/api/devices");
    for (const d of devices) bySerial[d.serial_number] = d;
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("device.update", (d: any) => {
      if (d && d.deleted) delete bySerial[d.serial_number];
      else if (d) bySerial[d.serial_number] = d;
    });
    load();
  }

  async function setFriendlyName(serial: string, name: string) {
    const updated = await api.patch<Device>(`/api/devices/${serial}`, {
      friendly_name: name,
    });
    bySerial[serial] = updated;
  }

  async function refresh(serial: string) {
    const res = await api.post<{ device: Device }>(
      `/api/devices/${serial}/refresh`,
    );
    bySerial[serial] = res.device;
  }

  // Pin a pio env (manual override) or release to auto-detect (env=null).
  async function setEnv(serial: string, env: string | null) {
    const updated = await api.put<Device>(`/api/devices/${serial}/env`, {
      env,
    });
    bySerial[serial] = updated;
  }

  return { bySerial, list, load, init, setFriendlyName, refresh, setEnv };
});
