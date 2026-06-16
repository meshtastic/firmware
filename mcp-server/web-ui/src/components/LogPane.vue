<script setup lang="ts">
import { nextTick, ref, watch } from "vue";
import { ansiToHtml } from "../ansi";

const props = defineProps<{ lines: string[]; placeholder?: string }>();

const box = ref<HTMLElement | null>(null);
const pinned = ref(true);

function onScroll() {
  const el = box.value;
  if (!el) return;
  pinned.value = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
}

watch(
  () => props.lines.length,
  async () => {
    if (!pinned.value) return;
    await nextTick();
    const el = box.value;
    if (el) el.scrollTop = el.scrollHeight;
  },
);
</script>

<template>
  <div
    ref="box"
    @scroll="onScroll"
    class="mono text-xs leading-relaxed overflow-auto h-full bg-black/40 rounded-md p-2 whitespace-pre-wrap break-all"
  >
    <span v-if="lines.length === 0" class="text-slate-600">{{
      placeholder || "(no output yet)"
    }}</span>
    <div v-for="(l, i) in lines" :key="i" v-html="ansiToHtml(l)" />
  </div>
</template>
