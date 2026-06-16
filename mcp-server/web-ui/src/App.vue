<script setup lang="ts">
import { onMounted, ref } from "vue";
import AppBar from "./components/AppBar.vue";
import DeviceGrid from "./components/DeviceGrid.vue";
import TestDashboard from "./components/TestDashboard.vue";
import { useBuildsStore } from "./stores/builds";
import { useCamerasStore } from "./stores/cameras";
import { useDevicesStore } from "./stores/devices";
import { useFirmwareStore } from "./stores/firmware";
import { useTestsStore } from "./stores/tests";
import { useWsStore } from "./stores/ws";

const tab = ref("fleet");

const ws = useWsStore();
const devices = useDevicesStore();
const cameras = useCamerasStore();
const firmware = useFirmwareStore();
const tests = useTestsStore();
const builds = useBuildsStore();

onMounted(() => {
  ws.connect();
  devices.init();
  cameras.init();
  firmware.init();
  tests.init();
  builds.init();
});
</script>

<template>
  <div class="min-h-screen">
    <AppBar :tab="tab" @update:tab="(v) => (tab = v)" />
    <main>
      <DeviceGrid v-show="tab === 'fleet'" />
      <TestDashboard v-if="tab === 'tests'" />
    </main>
  </div>
</template>
