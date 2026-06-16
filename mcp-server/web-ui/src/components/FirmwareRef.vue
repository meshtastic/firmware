<script setup lang="ts">
import { useFirmwareStore } from "../stores/firmware";
const fw = useFirmwareStore();
</script>

<template>
  <div
    class="flex items-center gap-2 px-3 py-1.5 rounded-md bg-slate-800/70 border border-slate-700 text-sm"
    :title="fw.ref.subject || ''"
  >
    <svg
      class="w-4 h-4 text-emerald-400"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
    >
      <circle cx="6" cy="6" r="2.5" />
      <circle cx="6" cy="18" r="2.5" />
      <circle cx="18" cy="9" r="2.5" />
      <path d="M6 8.5v7M8.4 7.2A6 6 0 0 1 15.5 9" />
    </svg>
    <template v-if="fw.ref.available">
      <span class="font-semibold text-slate-100">{{
        fw.ref.branch || "(detached)"
      }}</span>
      <span class="mono text-emerald-300">{{ fw.ref.short_sha }}</span>
      <span
        v-if="fw.ref.dirty"
        class="text-amber-400 text-xs"
        title="working tree has uncommitted changes"
        >● dirty</span
      >
    </template>
    <span v-else class="text-slate-500">no git ref</span>
  </div>
</template>
