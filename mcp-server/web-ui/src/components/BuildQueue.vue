<script setup lang="ts">
import { useBuildsStore } from "../stores/builds";
import { useFirmwareStore } from "../stores/firmware";

const builds = useBuildsStore();
const fw = useFirmwareStore();

const STATUS: Record<string, { glyph: string; cls: string }> = {
  queued: { glyph: "…", cls: "text-slate-400" },
  building: { glyph: "⏳", cls: "text-amber-400 animate-pulse" },
  success: { glyph: "✓", cls: "text-emerald-400" },
  cached: { glyph: "✓", cls: "text-emerald-400/70" },
  failed: { glyph: "✗", cls: "text-rose-400" },
  cancelled: { glyph: "∅", cls: "text-slate-500" },
};
</script>

<template>
  <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-4">
    <div class="flex items-center gap-3 mb-3">
      <h3 class="text-sm font-semibold text-slate-200">Build Queue</h3>
      <span
        v-if="!builds.dockerAvailable"
        class="text-[11px] px-2 py-0.5 rounded bg-amber-950/40 text-amber-400"
        title="Docker not detected — builds fall back to host pio (not parallelized)"
        >Docker unavailable — host builds</span
      >
      <div class="flex-1" />
      <button
        @click="builds.prebuildTracked()"
        class="text-xs px-3 py-1 rounded bg-emerald-700/30 border border-emerald-700 text-emerald-300 hover:bg-emerald-700/50"
        :title="'prebuild connected device targets @ ' + (fw.ref.short_sha || '')"
      >
        prebuild current ref
      </button>
    </div>

    <div class="flex flex-wrap gap-2">
      <div
        v-for="b in builds.list"
        :key="b.id"
        class="flex items-center gap-2 text-xs rounded-lg border border-slate-800 px-2.5 py-1.5"
        :title="b.error || b.artifact_dir || ''"
      >
        <span :class="(STATUS[b.status] || STATUS.queued).cls">{{
          (STATUS[b.status] || STATUS.queued).glyph
        }}</span>
        <span class="text-slate-200">{{ b.env }}</span>
        <span class="mono text-emerald-300/60">{{ b.fw_sha?.slice(0, 7) }}</span>
        <span v-if="b.duration_s" class="text-slate-500"
          >{{ b.duration_s.toFixed(0) }}s</span
        >
        <span v-if="b.cached" class="text-slate-600">cached</span>
      </div>
      <div v-if="builds.list.length === 0" class="text-xs text-slate-600">
        no builds yet — "prebuild current ref" builds each connected target in
        parallel (Docker) in the background
      </div>
    </div>
  </div>
</template>
