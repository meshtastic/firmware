<script setup lang="ts">
import { ref } from "vue";
import { useTestsStore } from "../stores/tests";

const tests = useTestsStore();
const args = ref("");
const busy = ref(false);

async function start() {
  busy.value = true;
  try {
    const parsed = args.value.trim() ? args.value.trim().split(/\s+/) : [];
    await tests.start(parsed);
  } catch (e: any) {
    alert(e.message);
  } finally {
    busy.value = false;
  }
}

async function stop() {
  busy.value = true;
  try {
    await tests.stop();
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="flex items-center gap-2">
    <button
      v-if="!tests.running"
      @click="start"
      :disabled="busy"
      class="px-4 py-1.5 rounded-md bg-emerald-600 hover:bg-emerald-500 text-white text-sm font-medium disabled:opacity-40"
    >
      ▶ Run suite
    </button>
    <button
      v-else
      @click="stop"
      :disabled="busy"
      class="px-4 py-1.5 rounded-md bg-rose-600 hover:bg-rose-500 text-white text-sm font-medium disabled:opacity-40"
    >
      ■ Stop
    </button>

    <input
      v-model="args"
      :disabled="tests.running"
      placeholder="pytest args (optional, e.g. tests/mesh)"
      class="flex-1 text-sm bg-slate-900 border border-slate-700 rounded px-3 py-1.5 outline-none focus:border-emerald-700 disabled:opacity-40"
    />

    <div class="text-sm flex items-center gap-3">
      <span v-if="tests.running" class="text-amber-400 flex items-center gap-1">
        <span class="animate-spin">⟳</span> running
      </span>
      <span
        v-else-if="tests.exitCode !== null"
        :class="tests.exitCode === 0 ? 'text-emerald-400' : 'text-rose-400'"
      >
        exit {{ tests.exitCode }}
      </span>
      <span class="tabular-nums text-xs text-slate-400">
        <span class="text-emerald-400">{{ tests.totals.passed }}</span> ·
        <span class="text-rose-400">{{ tests.totals.failed }}</span> ·
        <span class="text-slate-500">{{ tests.totals.skipped }}</span>
      </span>
    </div>
  </div>
</template>
