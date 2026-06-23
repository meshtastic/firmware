import { defineStore } from "pinia";
import { computed, reactive } from "vue";
import { api } from "../api/client";
import type { Camera } from "../types";
import { useWsStore } from "./ws";

export const useCamerasStore = defineStore("cameras", () => {
  const byId = reactive<Record<number, Camera>>({});

  const list = computed(() => Object.values(byId).sort((a, b) => a.id - b.id));

  function forDevice(serial: string): Camera | undefined {
    return Object.values(byId).find((c) => c.device_serial === serial);
  }

  async function load() {
    const cams = await api.get<Camera[]>("/api/cameras");
    for (const c of cams) byId[c.id] = c;
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("camera.update", (c: Camera) => {
      if (c.deleted) delete byId[c.id];
      else byId[c.id] = c;
    });
    load();
  }

  async function add(name: string, device_index: string) {
    const cam = await api.post<Camera>("/api/cameras", { name, device_index });
    byId[cam.id] = cam;
  }

  async function remove(id: number) {
    await api.del(`/api/cameras/${id}`);
    delete byId[id];
  }

  async function assign(id: number, device_serial: string | null) {
    const cam = await api.post<Camera>(`/api/cameras/${id}/assign`, {
      device_serial,
    });
    byId[id] = cam;
  }

  async function setRotation(id: number, rotation: number) {
    const cam = await api.post<Camera>(`/api/cameras/${id}/rotation`, {
      rotation,
    });
    byId[id] = cam;
  }

  async function setMirror(id: number, mirror: boolean) {
    const cam = await api.post<Camera>(`/api/cameras/${id}/mirror`, { mirror });
    byId[id] = cam;
  }

  return {
    byId,
    list,
    forDevice,
    load,
    init,
    add,
    remove,
    assign,
    setRotation,
    setMirror,
  };
});
