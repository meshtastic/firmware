<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { api } from "../api/client";
import type { Camera } from "../types";

const props = defineProps<{ camera?: Camera }>();

const errored = ref(false);
const statusMsg = ref<string | null>(null);
// Cache-bust so reassign / remount restarts the stream.
const nonce = ref(Date.now());

const src = computed(() =>
  props.camera
    ? `/api/cameras/${props.camera.id}/stream.mjpg?t=${nonce.value}`
    : "",
);

watch(
  () => props.camera?.id,
  () => {
    errored.value = false;
    statusMsg.value = null;
    nonce.value = Date.now();
  },
);

async function onError() {
  errored.value = true;
  if (!props.camera) return;
  try {
    const s = await api.get<{ ok: boolean; error: string | null }>(
      `/api/cameras/${props.camera.id}/status`,
    );
    statusMsg.value = s.ok ? "stream interrupted" : s.error;
  } catch {
    statusMsg.value = "camera unavailable";
  }
}

function retry() {
  errored.value = false;
  statusMsg.value = null;
  nonce.value = Date.now();
}
</script>

<template>
  <div
    class="relative aspect-video w-full bg-black rounded-md overflow-hidden border border-slate-800"
  >
    <template v-if="camera && !errored">
      <img
        :src="src"
        class="w-full h-full object-contain"
        @error="onError"
        alt="camera feed"
      />
      <span
        class="absolute top-1 left-1 text-[10px] px-1.5 py-0.5 rounded bg-black/60 text-emerald-300"
        >● {{ camera.name }}</span
      >
    </template>

    <div
      v-else-if="camera && errored"
      class="absolute inset-0 flex flex-col items-center justify-center gap-2 text-center px-3"
    >
      <span class="text-rose-400 text-sm">⚠ no signal</span>
      <span class="text-xs text-slate-500">{{
        statusMsg || "camera produced no frames"
      }}</span>
      <button
        @click="retry"
        class="text-xs px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 text-slate-300"
      >
        retry
      </button>
    </div>

    <div
      v-else
      class="absolute inset-0 flex items-center justify-center text-xs text-slate-600"
    >
      no camera assigned
    </div>
  </div>
</template>
