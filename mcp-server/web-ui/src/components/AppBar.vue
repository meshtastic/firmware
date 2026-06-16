<script setup lang="ts">
import { useWsStore } from "../stores/ws";
import { useDevicesStore } from "../stores/devices";
import FirmwareRef from "./FirmwareRef.vue";

defineProps<{ tab: string }>();
const emit = defineEmits<{ (e: "update:tab", v: string): void }>();

const ws = useWsStore();
const devices = useDevicesStore();
</script>

<template>
  <header
    class="flex items-center gap-4 px-5 py-3 border-b border-slate-800 bg-slate-900/80 backdrop-blur sticky top-0 z-10"
  >
    <div class="flex items-center gap-2">
      <span class="text-lg font-bold tracking-tight text-slate-100"
        >Meshtastic</span
      >
      <span class="text-lg font-light text-emerald-400">FleetSuite</span>
    </div>

    <nav class="flex gap-1 ml-4">
      <button
        v-for="t in ['fleet', 'tests']"
        :key="t"
        @click="emit('update:tab', t)"
        class="px-3 py-1.5 rounded-md text-sm capitalize transition"
        :class="
          tab === t
            ? 'bg-emerald-600/20 text-emerald-300 border border-emerald-700'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
        "
      >
        {{ t === "tests" ? "Test Suite" : "Fleet" }}
      </button>
    </nav>

    <div class="flex-1" />

    <span class="text-xs text-slate-500"
      >{{ devices.list.filter((d) => d.online).length }} online</span
    >
    <FirmwareRef />
    <span
      class="flex items-center gap-1.5 text-xs"
      :class="ws.connected ? 'text-emerald-400' : 'text-rose-400'"
    >
      <span
        class="w-2 h-2 rounded-full"
        :class="ws.connected ? 'bg-emerald-400' : 'bg-rose-400'"
      />
      {{ ws.connected ? "live" : "offline" }}
    </span>
  </header>
</template>
