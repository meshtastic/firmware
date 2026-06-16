<script setup lang="ts">
import { computed, ref } from "vue";
import { useTestsStore } from "../stores/tests";
import BuildQueue from "./BuildQueue.vue";
import LogPane from "./LogPane.vue";
import RunControls from "./RunControls.vue";
import TestTree from "./TestTree.vue";
import TierCounters from "./TierCounters.vue";

const tests = useTestsStore();
const logTab = ref<"pytest" | "flash" | "firmware">("pytest");

const activeLines = computed(() => {
  if (logTab.value === "flash") return tests.flash;
  if (logTab.value === "firmware") return tests.fwlog;
  return tests.stdout;
});

function fmtTime(t: number) {
  return new Date(t * 1000).toLocaleString();
}
</script>

<template>
  <div class="p-5 flex flex-col gap-4 h-[calc(100vh-57px)]">
    <RunControls />
    <BuildQueue />

    <div class="grid grid-cols-2 gap-4 flex-1 min-h-0">
      <!-- left: counters + tree -->
      <div class="flex flex-col gap-3 min-h-0">
        <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-4">
          <TierCounters />
        </div>
        <div
          class="rounded-xl border border-slate-700 bg-slate-900/60 p-3 flex-1 min-h-0"
        >
          <TestTree />
        </div>
      </div>

      <!-- right: log panes -->
      <div class="flex flex-col min-h-0">
        <div class="flex gap-1 text-xs mb-2">
          <button
            v-for="t in ['pytest', 'flash', 'firmware']"
            :key="t"
            @click="logTab = t as any"
            class="px-3 py-1 rounded-md capitalize transition"
            :class="
              logTab === t
                ? 'bg-slate-700 text-slate-100'
                : 'bg-slate-900 text-slate-500 hover:text-slate-300'
            "
          >
            {{ t }}
          </button>
        </div>
        <div class="flex-1 min-h-0">
          <LogPane :lines="activeLines" />
        </div>
      </div>
    </div>

    <!-- run history -->
    <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-3">
      <div class="text-xs text-slate-500 mb-2">recent runs</div>
      <div class="flex gap-3 overflow-x-auto text-xs">
        <div
          v-for="r in tests.runs"
          :key="r.id"
          class="shrink-0 rounded-lg border border-slate-800 px-3 py-1.5"
        >
          <div class="flex items-center gap-2">
            <span class="text-emerald-400">{{ r.passed }}</span>
            <span class="text-rose-400">{{ r.failed }}</span>
            <span class="text-slate-500">{{ r.skipped }}</span>
            <span class="mono text-emerald-300/60">{{
              r.fw_sha?.slice(0, 7)
            }}</span>
          </div>
          <div class="text-slate-600">{{ fmtTime(r.started_at) }}</div>
        </div>
        <div v-if="tests.runs.length === 0" class="text-slate-600">
          no runs recorded yet
        </div>
      </div>
    </div>
  </div>
</template>
