import { defineStore } from "pinia";
import { computed, reactive, ref } from "vue";
import { api } from "../api/client";
import { useWsStore } from "./ws";

export interface Build {
  id: number;
  env: string;
  fw_sha: string;
  fw_branch: string | null;
  status: string; // queued | building | success | failed | cached | cancelled
  duration_s: number | null;
  artifact_dir: string | null;
  error: string | null;
  cached?: boolean;
}

export const useBuildsStore = defineStore("builds", () => {
  const byId = reactive<Record<number, Build>>({});
  const dockerAvailable = ref(true);

  const list = computed(() => Object.values(byId).sort((a, b) => b.id - a.id));

  async function load() {
    const res = await api.get<{ docker: boolean; builds: Build[] }>(
      "/api/builds",
    );
    dockerAvailable.value = res.docker;
    for (const b of res.builds) byId[b.id] = b;
  }

  function init() {
    const ws = useWsStore();
    ws.subscribe("build.update", (b: Build) => {
      if (b && b.id != null) byId[b.id] = b;
    });
    load();
  }

  // Latest build row for an (env, sha) — drives the device flash button state.
  function statusFor(
    env: string,
    sha: string | null | undefined,
  ): Build | undefined {
    if (!sha) return undefined;
    return Object.values(byId)
      .filter((b) => b.env === env && b.fw_sha === sha)
      .sort((a, b) => b.id - a.id)[0];
  }

  async function prebuildTracked() {
    await api.post("/api/builds", {});
  }

  async function enqueue(envs: string[], force = false) {
    await api.post("/api/builds", { envs, force });
  }

  return {
    byId,
    list,
    dockerAvailable,
    load,
    init,
    statusFor,
    prebuildTracked,
    enqueue,
  };
});
