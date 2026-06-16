<script setup lang="ts">
import { onMounted, ref } from "vue";
import { api } from "../api/client";

const props = defineProps<{ serial: string }>();
const packets = ref<any[]>([]);
const loading = ref(false);

async function load() {
  loading.value = true;
  try {
    const res = await api.get<{ packets: any[] }>(
      `/api/devices/${props.serial}/packets?start=-30m&max=100`,
    );
    packets.value = res.packets.slice().reverse();
  } finally {
    loading.value = false;
  }
}

onMounted(load);

function ts(t: number) {
  return new Date(t * 1000).toLocaleTimeString();
}
</script>

<template>
  <div class="h-48 overflow-auto bg-black/40 rounded-md p-2">
    <div class="flex justify-between items-center mb-1">
      <span class="text-xs text-slate-500">last 30 min · packet API</span>
      <button
        @click="load"
        class="text-xs px-2 py-0.5 rounded bg-slate-800 hover:bg-slate-700 text-slate-300"
      >
        {{ loading ? "…" : "refresh" }}
      </button>
    </div>
    <table class="w-full text-xs mono">
      <thead class="text-slate-500">
        <tr class="text-left">
          <th class="font-normal">time</th>
          <th class="font-normal">portnum</th>
          <th class="font-normal">from→to</th>
          <th class="font-normal">snr</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(p, i) in packets" :key="i" class="border-t border-slate-800/60">
          <td class="text-slate-400">{{ ts(p.ts) }}</td>
          <td class="text-emerald-300">{{ p.portnum }}</td>
          <td class="text-slate-300">{{ p.from_node }}→{{ p.to_node }}</td>
          <td class="text-slate-400">{{ p.rx_snr ?? "" }}</td>
        </tr>
        <tr v-if="packets.length === 0">
          <td colspan="4" class="text-slate-600 py-2">no packets recorded</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
