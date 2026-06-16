<script setup lang="ts">
import { onMounted, ref } from "vue";
import { api } from "../api/client";

interface NativeInfo {
  docker: boolean;
  image: string;
  nodes: any[];
}

const info = ref<NativeInfo>({ docker: true, image: "", nodes: [] });
const name = ref("");
const port = ref(4403);
const busy = ref(false);
const err = ref<string | null>(null);

async function load() {
  info.value = await api.get<NativeInfo>("/api/native");
}
onMounted(load);

async function act(fn: () => Promise<any>) {
  busy.value = true;
  err.value = null;
  try {
    await fn();
    await load();
  } catch (e: any) {
    err.value = e.message;
  } finally {
    busy.value = false;
  }
}

const add = () =>
  name.value.trim() &&
  act(async () => {
    await api.post("/api/native", { name: name.value.trim(), tcp_port: port.value });
    name.value = "";
    port.value = port.value + 1;
  });
</script>

<template>
  <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-4">
    <div class="flex items-center gap-3 mb-3">
      <h3 class="text-sm font-semibold text-slate-200">Native Nodes (Docker)</h3>
      <span
        v-if="!info.docker"
        class="text-[11px] px-2 py-0.5 rounded bg-amber-950/40 text-amber-400"
        >Docker unavailable</span
      >
      <span class="text-[11px] text-slate-600 mono">{{ info.image }}</span>
    </div>

    <div class="flex flex-wrap gap-2 mb-3">
      <input
        v-model="name"
        placeholder="node name"
        class="text-xs bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
      />
      <input
        v-model.number="port"
        type="number"
        class="text-xs w-24 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        title="host TCP port → container 4403"
      />
      <button
        @click="add"
        :disabled="busy || !info.docker"
        class="text-xs px-3 py-1 rounded bg-emerald-700/30 border border-emerald-700 text-emerald-300 hover:bg-emerald-700/50 disabled:opacity-40"
      >
        run native node
      </button>
      <span v-if="err" class="text-xs text-rose-400 self-center">{{ err }}</span>
    </div>

    <ul class="space-y-1">
      <li
        v-for="n in info.nodes"
        :key="n.serial_number"
        class="flex items-center gap-2 text-xs text-slate-400"
      >
        <span
          class="w-2 h-2 rounded-full"
          :class="n.online ? 'bg-emerald-400' : 'bg-slate-600'"
        />
        <span class="text-slate-200">{{ n.friendly_name }}</span>
        <span class="mono">{{ n.current_port }}</span>
        <button
          v-if="!n.online"
          @click="act(() => api.post(`/api/native/${n.friendly_name}/start`))"
          class="ml-auto text-emerald-400/80 hover:text-emerald-300"
        >
          start
        </button>
        <button
          v-else
          @click="act(() => api.post(`/api/native/${n.friendly_name}/stop`))"
          class="ml-auto text-amber-400/80 hover:text-amber-300"
        >
          stop
        </button>
        <button
          @click="act(() => api.post(`/api/native/${n.friendly_name}/restart`))"
          class="text-sky-400/80 hover:text-sky-300"
        >
          restart
        </button>
        <button
          @click="act(() => api.del(`/api/native/${n.friendly_name}`))"
          class="text-rose-400/70 hover:text-rose-300"
        >
          remove
        </button>
      </li>
      <li v-if="info.nodes.length === 0" class="text-xs text-slate-600">
        no native nodes — run meshtasticd in Docker and manage it as a TCP device
      </li>
    </ul>
  </div>
</template>
