<script setup lang="ts">
import { ref } from "vue";
import { useCamerasStore } from "../stores/cameras";

const cameras = useCamerasStore();
const name = ref("");
const index = ref("0");
const adding = ref(false);

async function add() {
  if (!name.value.trim()) return;
  adding.value = true;
  try {
    await cameras.add(name.value, index.value);
    name.value = "";
    index.value = "0";
  } finally {
    adding.value = false;
  }
}
</script>

<template>
  <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-4">
    <h3 class="text-sm font-semibold text-slate-200 mb-3">USB Cameras</h3>
    <div class="flex flex-wrap gap-2 mb-3">
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
    <ul class="space-y-1">
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
      <li v-if="cameras.list.length === 0" class="text-xs text-slate-600">
        no cameras yet — add one by its OpenCV device index
      </li>
    </ul>
  </div>
</template>
