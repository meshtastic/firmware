<script setup lang="ts">
import { onMounted, ref } from "vue";
import { api } from "../api/client";
import { useCamerasStore } from "../stores/cameras";

interface Discovered {
  index: number;
  name: string;
  in_use: boolean;
  width?: number;
  height?: number;
  unavailable?: boolean;
}

const cameras = useCamerasStore();
const name = ref("");
const index = ref("0");
const adding = ref(false);
const manual = ref(false);

const discovered = ref<Discovered[]>([]);
const scanning = ref(false);
const scanned = ref(false);
const hasBackend = ref(true); // cv2 present → live preview possible

async function scan() {
  scanning.value = true;
  try {
    const res = await api.get<{ cv2: boolean; cameras: Discovered[] }>(
      "/api/cameras/discover",
    );
    hasBackend.value = res.cv2;
    discovered.value = res.cameras;
    scanned.value = true;
  } catch {
    discovered.value = [];
    scanned.value = true;
  } finally {
    scanning.value = false;
  }
}
onMounted(scan);

async function quickAdd(cam: Discovered) {
  await cameras.add(cam.name, String(cam.index));
  await scan(); // refresh in_use flags
}

async function add() {
  if (!name.value.trim()) return;
  adding.value = true;
  try {
    await cameras.add(name.value, index.value);
    name.value = "";
    index.value = "0";
    await scan();
  } finally {
    adding.value = false;
  }
}

function res(c: Discovered): string {
  if (c.unavailable) return "can't open";
  if (c.width && c.height) return `${c.width}×${c.height}`;
  return hasBackend.value ? "" : "no preview backend";
}
</script>

<template>
  <div class="card-rail rounded-xl border border-slate-700/80 bg-slate-900/60 p-4">
    <div class="flex items-center gap-2 mb-3">
      <span class="w-1 h-3.5 rounded-full bg-indigo-500/80" />
      <h3 class="section-label">USB Cameras</h3>
      <div class="flex-1" />
      <button
        @click="scan"
        :disabled="scanning"
        class="text-xs px-2.5 py-1 rounded border border-indigo-700 text-indigo-300 hover:bg-indigo-600/20 disabled:opacity-40 fs-display"
      >
        {{ scanning ? "scanning…" : "⟳ scan" }}
      </button>
    </div>

    <!-- discovered cameras -->
    <ul v-if="discovered.length" class="space-y-1 mb-3">
      <li
        v-for="c in discovered"
        :key="c.index"
        class="flex items-center gap-2 text-xs px-2 py-1.5 rounded bg-slate-950/40 border border-slate-800"
      >
        <span class="w-1.5 h-1.5 rounded-full" :class="c.unavailable ? 'bg-rose-500' : 'bg-emerald-400'" />
        <span class="text-slate-200 truncate">{{ c.name }}</span>
        <span class="mono text-slate-500">idx {{ c.index }}</span>
        <span v-if="res(c)" class="mono text-slate-600">{{ res(c) }}</span>
        <span class="ml-auto">
          <span v-if="c.in_use" class="text-emerald-400/70">added ✓</span>
          <button
            v-else
            @click="quickAdd(c)"
            class="px-2 py-0.5 rounded bg-emerald-700/30 border border-emerald-700 text-emerald-300 hover:bg-emerald-700/50"
          >
            add
          </button>
        </span>
      </li>
    </ul>
    <p
      v-else-if="scanned && !scanning"
      class="text-xs text-slate-600 mb-3"
    >
      no cameras detected — connect a USB capture device and scan again
    </p>

    <p
      v-if="scanned && !hasBackend"
      class="text-[11px] text-amber-400/80 mb-3"
    >
      live preview needs OpenCV — install the bench extra:
      <span class="mono">pip install -e '.[ui]'</span> (discovery still works without it)
    </p>

    <!-- manual fallback -->
    <button
      @click="manual = !manual"
      class="text-[11px] text-slate-500 hover:text-slate-300"
    >
      {{ manual ? "▾" : "▸" }} add by index manually
    </button>
    <div v-if="manual" class="flex flex-wrap gap-2 mt-2">
      <input
        v-model="name"
        placeholder="name"
        class="text-xs bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
      />
      <input
        v-model="index"
        placeholder="device index (0)"
        class="text-xs w-32 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
      />
      <button
        @click="add"
        :disabled="adding"
        class="text-xs px-3 py-1 rounded bg-emerald-700/30 border border-emerald-700 text-emerald-300 hover:bg-emerald-700/50 disabled:opacity-40"
      >
        add camera
      </button>
    </div>

    <!-- registered cameras -->
    <ul v-if="cameras.list.length" class="space-y-1 mt-3 pt-3 border-t border-slate-800">
      <li
        v-for="c in cameras.list"
        :key="c.id"
        class="flex items-center gap-2 text-xs text-slate-400"
      >
        <span class="text-slate-200">{{ c.name }}</span>
        <span class="mono">idx {{ c.device_index }}</span>
        <span v-if="c.device_serial" class="text-emerald-400/70"
          >→ {{ c.device_serial }}</span
        >
        <span v-else class="text-slate-600">unassigned</span>
        <button
          @click="cameras.remove(c.id)"
          class="ml-auto text-rose-400/70 hover:text-rose-300"
        >
          remove
        </button>
      </li>
    </ul>
  </div>
</template>
