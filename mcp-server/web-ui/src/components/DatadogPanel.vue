<script setup lang="ts">
import { reactive, ref, watch } from "vue";
import { useDatadogStore } from "../stores/datadog";

const dd = useDatadogStore();

// Local editable draft, seeded from status and kept in sync when status loads.
const draft = reactive({
  enabled: false,
  site: "us5.datadoghq.com",
  scrub: "coarse",
  collector: "bench",
  ship_debug: false,
  api_key: "", // write-only; blank = keep existing
});

let seeded = false;
watch(
  () => dd.status,
  (s) => {
    if (!s || seeded) return;
    draft.enabled = s.config.enabled;
    draft.site = s.config.site;
    draft.scrub = s.config.scrub;
    draft.collector = s.config.collector;
    draft.ship_debug = s.config.ship_debug;
    seeded = true;
  },
  { immediate: true },
);

const busy = ref(false);
const msg = ref<string | null>(null);
const ok = ref(true);

async function save() {
  busy.value = true;
  msg.value = "saving…";
  ok.value = true;
  try {
    const payload: Record<string, unknown> = {
      enabled: draft.enabled,
      site: draft.site,
      scrub: draft.scrub,
      collector: draft.collector,
      ship_debug: draft.ship_debug,
    };
    if (draft.api_key.trim()) payload.api_key = draft.api_key.trim();
    await dd.save(payload);
    draft.api_key = ""; // never keep the secret in the field
    msg.value = "saved";
  } catch (e: any) {
    msg.value = e.message;
    ok.value = false;
  } finally {
    busy.value = false;
  }
}

async function test() {
  busy.value = true;
  msg.value = "sending test log…";
  ok.value = true;
  try {
    const r = await dd.test();
    ok.value = r.ok;
    msg.value = r.ok ? "test log accepted by Datadog ✓" : `test failed: ${r.error}`;
  } catch (e: any) {
    ok.value = false;
    msg.value = e.message;
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="rounded-xl border border-slate-700 bg-slate-900/60 p-4">
    <div class="flex items-center gap-3 mb-3">
      <h3 class="text-sm font-semibold text-slate-200">Datadog logging</h3>
      <span
        v-if="dd.status"
        class="flex items-center gap-1.5 text-xs"
        :class="dd.status.stats.running ? 'text-emerald-400' : 'text-slate-500'"
      >
        <span
          class="w-2 h-2 rounded-full"
          :class="dd.status.stats.running ? 'bg-emerald-400' : 'bg-slate-600'"
        />
        {{ dd.status.stats.running ? "shipping" : "idle" }}
      </span>
      <span v-if="dd.status" class="text-xs text-slate-500 tabular-nums">
        {{ dd.status.stats.sent_logs }} logs ·
        {{ dd.status.stats.sent_metrics }} metrics
      </span>
      <span
        v-if="dd.status?.stats.last_error"
        class="text-xs text-rose-400 truncate"
        :title="dd.status.stats.last_error"
        >⚠ {{ dd.status.stats.last_error }}</span
      >
    </div>

    <div class="grid grid-cols-2 gap-2 text-xs">
      <label class="flex items-center gap-2 col-span-2">
        <input type="checkbox" v-model="draft.enabled" class="accent-emerald-500" />
        <span class="text-slate-300">Ship recorder logs + telemetry to Datadog</span>
      </label>

      <div class="col-span-2 flex gap-1.5">
        <input
          v-model="draft.api_key"
          type="password"
          :placeholder="
            dd.status?.config.has_key
              ? `API key set (••••${dd.status.config.key_hint}) — leave blank to keep`
              : 'DD_API_KEY (or pub… client token)'
          "
          class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        />
      </div>

      <label class="text-slate-500"
        >Site
        <input
          v-model="draft.site"
          class="w-full mt-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
      /></label>
      <label class="text-slate-500"
        >GPS scrub
        <select
          v-model="draft.scrub"
          class="w-full mt-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none"
        >
          <option value="off">off</option>
          <option value="coarse">coarse (~11 km)</option>
          <option value="redact">redact</option>
        </select>
      </label>

      <label class="text-slate-500"
        >Collector tag
        <input
          v-model="draft.collector"
          class="w-full mt-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
      /></label>
      <label class="flex items-end gap-2 pb-1">
        <input type="checkbox" v-model="draft.ship_debug" class="accent-emerald-500" />
        <span class="text-slate-400">ship DEBUG lines</span>
      </label>
    </div>

    <div class="flex items-center gap-2 mt-3">
      <button
        @click="save"
        :disabled="busy"
        class="text-xs px-3 py-1 rounded bg-emerald-700/30 border border-emerald-700 text-emerald-300 hover:bg-emerald-700/50 disabled:opacity-40"
      >
        save
      </button>
      <button
        @click="test"
        :disabled="busy"
        class="text-xs px-3 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
      >
        test connection
      </button>
      <span
        v-if="msg"
        class="text-xs mono truncate"
        :class="ok ? 'text-slate-500' : 'text-rose-400'"
        :title="msg"
        >{{ msg }}</span
      >
    </div>
    <p class="text-[10px] text-slate-600 mt-2">
      Streams <code>.mtlog/logs.jsonl</code> → Datadog Logs and
      <code>telemetry.jsonl</code> → Metrics, tagged <code>collector:{{ draft.collector }}</code
      >. Same schema as the bench/fleet forwarders, so the existing dashboard works.
    </p>
  </div>
</template>
