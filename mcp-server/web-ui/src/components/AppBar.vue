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
    class="flex items-center gap-4 px-5 py-3 border-b border-slate-800 bg-slate-950/70 backdrop-blur sticky top-0 z-10"
  >
    <!-- Wordmark + LoRa-chirp signature (nods to the Meshtastic logo origin) -->
    <div class="flex items-center gap-2.5">
      <svg
        viewBox="0 0 40 24"
        class="w-9 h-5 shrink-0"
        fill="none"
        stroke-linecap="round"
        stroke-linejoin="round"
        aria-hidden="true"
      >
        <defs>
          <linearGradient id="chirp" x1="0" y1="0" x2="40" y2="0"
            gradientUnits="userSpaceOnUse">
            <stop offset="0" stop-color="#9ba8e0" />
            <stop offset="1" stop-color="#67ea94" />
          </linearGradient>
        </defs>
        <!-- a rising chirp: frequency increases left→right -->
        <path
          d="M1 12 C3 5, 5 5, 7 12 S11 19, 13 12 S16 6, 18 12 S20.5 17, 22.5 12 S24.5 8, 26 12 S27.5 15.5, 29 12 S30.5 9.5, 32 12 S33 14, 34 12 S35 11, 36 12 39 12 39 12"
          stroke="url(#chirp)"
          stroke-width="2"
        />
      </svg>
      <div class="flex items-baseline gap-1.5">
        <span class="text-sm font-medium text-slate-400 tracking-tight"
          >Meshtastic</span
        >
        <span class="fs-display text-base font-bold text-indigo-300"
          >FleetSuite</span
        >
      </div>
    </div>

    <nav class="flex gap-1 ml-3">
      <button
        v-for="t in ['fleet', 'tests']"
        :key="t"
        @click="emit('update:tab', t)"
        class="px-3 py-1.5 rounded-md text-xs fs-display transition"
        :class="
          tab === t
            ? 'bg-indigo-600/20 text-indigo-200 ring-1 ring-indigo-600/60'
            : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800'
        "
      >
        {{ t === "tests" ? "Test Suite" : "Fleet" }}
      </button>
    </nav>

    <div class="flex-1" />

    <!-- Instrument readout: online count, mint when any are live -->
    <span
      class="flex items-center gap-1.5 text-xs mono px-2 py-1 rounded-md bg-slate-900/70 border border-slate-800"
    >
      <span
        class="w-1.5 h-1.5 rounded-full"
        :class="
          devices.list.filter((d) => d.online).length
            ? 'bg-emerald-400'
            : 'bg-slate-600'
        "
      />
      <span class="text-slate-300 tabular-nums">{{
        devices.list.filter((d) => d.online).length
      }}</span>
      <span class="text-slate-500">online</span>
    </span>

    <FirmwareRef />

    <span
      class="flex items-center gap-1.5 text-xs fs-display"
      :class="ws.connected ? 'text-emerald-400' : 'text-rose-400'"
    >
      <span class="relative flex h-2 w-2">
        <span
          v-if="ws.connected"
          class="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-60"
        />
        <span
          class="relative inline-flex rounded-full h-2 w-2"
          :class="ws.connected ? 'bg-emerald-400' : 'bg-rose-400'"
        />
      </span>
      {{ ws.connected ? "live" : "offline" }}
    </span>
  </header>
</template>
