<script setup lang="ts">
import { useDevicesStore } from "../stores/devices";
import CameraManager from "./CameraManager.vue";
import DeviceCard from "./DeviceCard.vue";
import NativeManager from "./NativeManager.vue";

const devices = useDevicesStore();
</script>

<template>
  <div class="p-5 space-y-5">
    <div class="grid gap-4 md:grid-cols-2">
      <CameraManager />
      <NativeManager />
    </div>

    <div
      v-if="devices.list.length === 0"
      class="rounded-xl border border-dashed border-slate-700 p-10 text-center text-slate-500"
    >
      No devices detected. Plug in a Meshtastic board over USB — the card will
      appear automatically and follow it across ports.
    </div>

    <div
      v-else
      class="grid gap-4"
      style="grid-template-columns: repeat(auto-fill, minmax(360px, 1fr))"
    >
      <DeviceCard
        v-for="d in devices.list"
        :key="d.serial_number"
        :device="d"
      />
    </div>
  </div>
</template>
