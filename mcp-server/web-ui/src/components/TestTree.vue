<script setup lang="ts">
import { computed } from "vue";
import { useTestsStore } from "../stores/tests";
import type { TestLeaf } from "../types";

const tests = useTestsStore();

const GLYPH: Record<string, string> = {
  passed: "✓",
  failed: "✗",
  skipped: "⊘",
  running: "⟳",
  pending: "·",
};
const CLS: Record<string, string> = {
  passed: "text-emerald-400",
  failed: "text-rose-400",
  skipped: "text-slate-500",
  running: "text-amber-400 animate-pulse",
  pending: "text-slate-600",
};

// Group leaves: tier → file → [leaves]
const grouped = computed(() => {
  const out: Record<string, Record<string, TestLeaf[]>> = {};
  for (const leaf of Object.values(tests.leaves)) {
    (out[leaf.tier] ??= {});
    (out[leaf.tier][leaf.file] ??= []).push(leaf);
  }
  return out;
});
</script>

<template>
  <div class="overflow-auto text-xs mono h-full">
    <template v-for="tier in tests.tierOrder" :key="tier">
      <div v-if="grouped[tier]" class="mb-2">
        <div class="text-slate-300 capitalize font-semibold">{{ tier }}</div>
        <div v-for="(leaves, file) in grouped[tier]" :key="file" class="ml-2">
          <div class="text-slate-500">{{ file }}</div>
          <div
            v-for="leaf in leaves"
            :key="leaf.nodeid"
            class="ml-3 flex items-center gap-1.5"
            :class="leaf.nodeid === tests.runningNodeId ? 'bg-amber-950/30 rounded' : ''"
          >
            <span :class="CLS[leaf.outcome]">{{ GLYPH[leaf.outcome] }}</span>
            <span class="text-slate-400 truncate">{{ leaf.testname }}</span>
            <span
              v-if="leaf.duration"
              class="text-slate-600 ml-auto pl-2"
              >{{ leaf.duration.toFixed(1) }}s</span
            >
          </div>
        </div>
      </div>
    </template>
    <div
      v-if="Object.keys(tests.leaves).length === 0"
      class="text-slate-600 p-2"
    >
      no tests collected yet — start a run
    </div>
  </div>
</template>
