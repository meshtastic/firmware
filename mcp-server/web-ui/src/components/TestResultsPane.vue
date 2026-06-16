<script setup lang="ts">
import { onMounted, ref } from "vue";
import { api } from "../api/client";

const props = defineProps<{ serial: string }>();
const results = ref<any[]>([]);

const OUTCOME_CLASS: Record<string, string> = {
  passed: "text-emerald-400",
  failed: "text-rose-400",
  skipped: "text-slate-500",
};

async function load() {
  results.value = await api.get<any[]>(
    `/api/devices/${props.serial}/test-results?limit=100`,
  );
}

onMounted(load);
</script>

<template>
  <div class="h-48 overflow-auto bg-black/40 rounded-md p-2">
    <div class="flex justify-between items-center mb-1">
      <span class="text-xs text-slate-500">test history</span>
      <button
        @click="load"
        class="text-xs px-2 py-0.5 rounded bg-slate-800 hover:bg-slate-700 text-slate-300"
      >
        refresh
      </button>
    </div>
    <table class="w-full text-xs">
      <tbody>
        <tr
          v-for="(r, i) in results"
          :key="i"
          class="border-t border-slate-800/60"
        >
          <td :class="OUTCOME_CLASS[r.outcome] || 'text-slate-400'" class="w-4">
            {{
              r.outcome === "passed" ? "✓" : r.outcome === "failed" ? "✗" : "⊘"
            }}
          </td>
          <td class="mono text-slate-300 truncate max-w-[14rem]" :title="r.nodeid">
            {{ r.nodeid.split("::").pop() }}
          </td>
          <td class="mono text-emerald-300/70 text-right">{{ r.fw_sha?.slice(0, 7) }}</td>
        </tr>
        <tr v-if="results.length === 0">
          <td colspan="3" class="text-slate-600 py-2">no runs recorded yet</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
