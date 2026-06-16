import { defineStore } from "pinia";
import { computed, reactive, ref } from "vue";
import { api } from "../api/client";
import type { TestLeaf, TestRun } from "../types";
import { useWsStore } from "./ws";

const TIERS = [
  "bake",
  "unit",
  "mesh",
  "telemetry",
  "monitor",
  "fleet",
  "admin",
  "provisioning",
] as const;

const MAX_LOG = 4000;

function pushBounded(arr: string[], line: string) {
  arr.push(line);
  if (arr.length > MAX_LOG) arr.splice(0, arr.length - MAX_LOG);
}

export const useTestsStore = defineStore("tests", () => {
  const running = ref(false);
  const runId = ref<number | null>(null);
  const exitCode = ref<number | null>(null);
  const runningNodeId = ref<string | null>(null);

  const leaves = reactive<Record<string, TestLeaf>>({});
  const stdout = reactive<string[]>([]);
  const flash = reactive<string[]>([]);
  const fwlog = reactive<string[]>([]);
  const runs = ref<TestRun[]>([]);

  const tiers = computed(() => {
    const out: Record<
      string,
      {
        passed: number;
        failed: number;
        skipped: number;
        running: number;
        total: number;
      }
    > = {};
    for (const t of TIERS)
      out[t] = { passed: 0, failed: 0, skipped: 0, running: 0, total: 0 };
    for (const leaf of Object.values(leaves)) {
      const t = out[leaf.tier];
      if (!t) continue;
      t.total++;
      if (leaf.outcome === "running") t.running++;
      else if (leaf.outcome in t) (t as any)[leaf.outcome]++;
    }
    return out;
  });

  const totals = computed(() => {
    let passed = 0,
      failed = 0,
      skipped = 0;
    for (const leaf of Object.values(leaves)) {
      if (leaf.outcome === "passed") passed++;
      else if (leaf.outcome === "failed") failed++;
      else if (leaf.outcome === "skipped") skipped++;
    }
    return { passed, failed, skipped };
  });

  function reset() {
    for (const k of Object.keys(leaves)) delete leaves[k];
    stdout.length = 0;
    flash.length = 0;
    fwlog.length = 0;
    exitCode.value = null;
  }

  function onProgress(d: any) {
    switch (d.type) {
      case "run_started":
        reset();
        running.value = true;
        runId.value = d.run_id;
        break;
      case "run_finished":
        running.value = false;
        exitCode.value = d.exit_code;
        runningNodeId.value = null;
        loadRuns();
        break;
      case "register":
        if (!leaves[d.nodeid])
          leaves[d.nodeid] = {
            nodeid: d.nodeid,
            tier: d.tier,
            file: d.file,
            testname: d.testname,
            outcome: "pending",
          };
        break;
      case "running":
        if (leaves[d.nodeid]) leaves[d.nodeid].outcome = "running";
        runningNodeId.value = d.nodeid;
        break;
      case "outcome":
        if (leaves[d.nodeid]) {
          leaves[d.nodeid].outcome = d.outcome;
          leaves[d.nodeid].duration = d.duration;
        }
        if (runningNodeId.value === d.nodeid) runningNodeId.value = null;
        break;
    }
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("test.progress", onProgress);
    ws.subscribe("test.stdout", (d: any) =>
      pushBounded(
        stdout,
        d.source === "stderr" ? `[stderr] ${d.line}` : d.line,
      ),
    );
    ws.subscribe("test.flash", (d: any) => pushBounded(flash, d.line));
    ws.subscribe("fw.log", (d: any) =>
      pushBounded(fwlog, `${d.port ?? ""} ${d.line}`.trim()),
    );
    loadStatus();
    loadRuns();
  }

  async function loadStatus() {
    const s = await api.get<any>("/api/tests/status");
    running.value = s.running;
    runId.value = s.run_id;
    exitCode.value = s.exit_code;
  }

  async function loadRuns() {
    runs.value = await api.get<TestRun[]>("/api/tests/runs");
  }

  async function start(args: string[]) {
    await api.post("/api/tests/start", { args });
  }

  async function stop() {
    await api.post("/api/tests/stop");
  }

  return {
    running,
    runId,
    exitCode,
    runningNodeId,
    leaves,
    stdout,
    flash,
    fwlog,
    runs,
    tiers,
    totals,
    tierOrder: TIERS,
    init,
    start,
    stop,
    loadRuns,
  };
});
