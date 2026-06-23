<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { api } from "../api/client";
import { useCamerasStore } from "../stores/cameras";
import type { Camera } from "../types";

const props = defineProps<{ camera?: Camera }>();
const cameras = useCamerasStore();

const errored = ref(false);
const statusMsg = ref<string | null>(null);
// Cache-bust so reassign / remount restarts the stream.
const nonce = ref(Date.now());

const src = computed(() =>
  props.camera
    ? `/api/cameras/${props.camera.id}/stream.mjpg?t=${nonce.value}`
    : "",
);

const rotation = computed(() => props.camera?.rotation ?? 0);

// Rotation is pure CSS (the MJPEG stream isn't restarted). For 90/270 we scale
// a 16:9 feed (filling the 16:9 box) by 9/16 so it fits after the quarter turn.
const imgStyle = computed(() => {
  const r = rotation.value;
  const scale = r === 90 || r === 270 ? 0.5625 : 1;
  return {
    transform: `rotate(${r}deg) scale(${scale})`,
    transition: "transform 0.2s ease",
  };
});

// Only the camera id changing should restart the stream (not a rotation save).
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

async function rotate() {
  if (!props.camera) return;
  try {
    await cameras.setRotation(props.camera.id, (rotation.value + 90) % 360);
  } catch {
    /* ignore — transient */
  }
}
</script>

<template>
  <div
    class="relative aspect-video w-full bg-black rounded-md overflow-hidden border border-slate-800"
  >
    <template v-if="camera && !errored">
      <img
        :src="src"
        :style="imgStyle"
        class="w-full h-full object-contain"
        @error="onError"
        alt="camera feed"
      />
      <span
        class="absolute top-1 left-1 text-[10px] px-1.5 py-0.5 rounded bg-black/60 text-emerald-300"
        >● {{ camera.name }}</span
      >
      <button
        @click="rotate"
        class="absolute top-1 right-1 p-1 rounded bg-black/60 text-slate-300 hover:text-emerald-300 transition"
        :title="`rotate (now ${rotation}°)`"
      >
        <svg
          viewBox="0 0 24 24"
          class="w-3.5 h-3.5"
          fill="none"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        >
          <polyline points="23 4 23 10 17 10" />
          <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10" />
        </svg>
      </button>
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
