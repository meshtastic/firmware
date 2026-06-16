import { defineStore } from "pinia";
import { ref } from "vue";
import { api } from "../api/client";
import type { FirmwareRef } from "../types";
import { useWsStore } from "./ws";

export const useFirmwareStore = defineStore("firmware", () => {
  const ref_ = ref<FirmwareRef>({ available: false });

  async function load() {
    ref_.value = await api.get<FirmwareRef>("/api/firmware");
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("firmware.update", (r: FirmwareRef) => {
      ref_.value = r;
    });
    load();
  }

  return { ref: ref_, load, init };
});
