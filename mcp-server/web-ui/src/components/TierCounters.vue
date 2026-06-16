<script setup lang="ts">
import { useTestsStore } from "../stores/tests";
const tests = useTestsStore();
</script>

<template>
  <div class="space-y-1.5">
    <div
      v-for="t in tests.tierOrder"
      :key="t"
      class="flex items-center gap-2 text-xs transition-opacity"
      :class="tests.tiers[t].total ? '' : 'opacity-45'"
    >
      <span class="w-24 text-slate-400 capitalize">{{ t }}</span>
      <div class="flex-1 h-2.5 rounded-full bg-slate-800 overflow-hidden flex">
        <div
          class="bg-emerald-500"
          :style="{
            width:
              tests.tiers[t].total
                ? (tests.tiers[t].passed / tests.tiers[t].total) * 100 + '%'
                : '0%',
          }"
        />
        <div
          class="bg-rose-500"
          :style="{
            width:
              tests.tiers[t].total
                ? (tests.tiers[t].failed / tests.tiers[t].total) * 100 + '%'
                : '0%',
          }"
        />
        <div
          class="bg-slate-600"
          :style="{
            width:
              tests.tiers[t].total
                ? (tests.tiers[t].skipped / tests.tiers[t].total) * 100 + '%'
                : '0%',
          }"
        />
      </div>
      <span class="inline-flex items-center gap-1 tabular-nums shrink-0">
        <span
          class="w-6 text-right"
          :class="tests.tiers[t].passed ? 'text-emerald-400' : 'text-slate-600'"
          >{{ tests.tiers[t].passed }}</span
        >
        <span class="text-slate-700">/</span>
        <span
          class="w-6 text-right"
          :class="tests.tiers[t].failed ? 'text-rose-400' : 'text-slate-600'"
          >{{ tests.tiers[t].failed }}</span
        >
        <span class="text-slate-700">/</span>
        <span
          class="w-6 text-right"
          :class="tests.tiers[t].skipped ? 'text-slate-300' : 'text-slate-600'"
          >{{ tests.tiers[t].skipped }}</span
        >
        <span class="w-7 text-right">
          <span v-if="tests.tiers[t].running" class="text-amber-400"
            >⟳{{ tests.tiers[t].running }}</span
          >
        </span>
      </span>
    </div>
  </div>
</template>
