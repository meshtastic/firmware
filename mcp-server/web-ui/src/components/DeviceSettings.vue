<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { api } from "../api/client";
import { useDevicesStore } from "../stores/devices";
import type { Device } from "../types";

const props = defineProps<{ device: Device }>();
const devices = useDevicesStore();

const name = ref(props.device.friendly_name || "");
const envInput = ref(props.device.env || "");
const envList = ref<string[]>([]);
const cfgPath = ref("");
const cfgValue = ref("");
const busy = ref(false);
const msg = ref<string | null>(null);
const ok = ref(true);

const isNative = computed(() => props.device.role === "native");
const serial = computed(() => props.device.serial_number);

// --- uhubctl hub-port assignment ---
type Slot = { location: string; port: number; label: string; value: string };
const hubAvailable = ref(true);
const slots = ref<Slot[]>([]);
const selectedSlot = ref("");
const currentSlot = computed(() =>
  props.device.hub_location != null
    ? `${props.device.hub_location}:${props.device.hub_port}`
    : "",
);

async function loadHubs() {
  if (isNative.value) return;
  try {
    const res = await api.get<{ available: boolean; hubs: any[] }>("/api/hubs");
    hubAvailable.value = res.available;
    slots.value = (res.hubs || [])
      .filter((h) => h.ppps)
      .flatMap((h) =>
        h.ports.map((p: any) => {
          const tail = p.device_desc
            ? ` — ${p.device_desc}`
            : p.device_vid
              ? ` — ${p.device_vid.toString(16)}:${(p.device_pid ?? 0).toString(16)}`
              : " (empty)";
          return {
            location: h.location,
            port: p.port,
            value: `${h.location}:${p.port}`,
            label: `${h.location}:${p.port}${tail}`,
          };
        }),
      );
    selectedSlot.value = currentSlot.value;
  } catch {
    /* hubs unavailable — section degrades to read-only */
  }
}

const assignSlot = () => {
  if (!selectedSlot.value) return;
  const slot = slots.value.find((s) => s.value === selectedSlot.value);
  if (!slot) return;
  act("assign hub port", () =>
    devices.setHubPort(serial.value, slot.location, slot.port),
  );
};

const clearSlot = () =>
  act("clear hub port", () => devices.setHubPort(serial.value, null, null));

const autoLocate = () =>
  act("auto-locate", async () => {
    const res = await devices.locate(serial.value);
    if (!res.located) {
      const c = res.candidates || [];
      throw new Error(
        c.length
          ? `ambiguous — pick a port: ${c.map((x) => `${x.location}:${x.port}`).join(", ")}`
          : "no PPPS hub port matched this device's VID",
      );
    }
    selectedSlot.value = currentSlot.value;
  });

async function loadEnvs() {
  try {
    // Suggest envs, narrowed to this board's architecture when we can infer it.
    const arch =
      props.device.role === "nrf52"
        ? "nrf52840"
        : props.device.role === "esp32s3"
          ? "esp32-s3"
          : undefined;
    const q = arch ? `?architecture=${arch}` : "";
    const boards = await api.get<any[]>(`/api/boards${q}`);
    envList.value = boards.map((b) => b.env).filter(Boolean).sort();
  } catch {
    /* leave datalist empty — free-form input still works */
  }
}
onMounted(() => {
  loadEnvs();
  loadHubs();
});

async function act(label: string, fn: () => Promise<any>) {
  busy.value = true;
  msg.value = `${label}…`;
  ok.value = true;
  try {
    await fn();
    msg.value = `${label} ✓`;
  } catch (e: any) {
    msg.value = `${label}: ${e.message}`;
    ok.value = false;
  } finally {
    busy.value = false;
  }
}

const saveName = () =>
  act("rename", () => devices.setFriendlyName(props.device.serial_number, name.value));

const pinEnv = () =>
  envInput.value.trim() &&
  act("pin env", () => devices.setEnv(props.device.serial_number, envInput.value.trim()));

const autoEnv = () =>
  act("auto-detect env", async () => {
    await devices.setEnv(props.device.serial_number, null);
    envInput.value = props.device.env || "";
  });

const setConfig = () => {
  if (!cfgPath.value.trim()) return;
  // Coerce "30"→30, "true"→true; leave bare strings (e.g. "US") as-is.
  let value: any = cfgValue.value;
  try {
    value = JSON.parse(cfgValue.value);
  } catch {
    /* keep string */
  }
  act("set config", () =>
    api.put(`/api/devices/${props.device.serial_number}/config`, {
      path: cfgPath.value.trim(),
      value,
    }),
  );
};
</script>

<template>
  <div class="rounded-lg border border-slate-700 bg-slate-950/50 p-3 space-y-3 text-xs">
    <!-- friendly name -->
    <div>
      <label class="text-slate-500">Friendly name</label>
      <div class="flex gap-1.5 mt-1">
        <input
          v-model="name"
          class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        />
        <button
          @click="saveName"
          :disabled="busy"
          class="px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
        >
          save
        </button>
      </div>
    </div>

    <!-- pio env override -->
    <div>
      <div class="flex items-center gap-2">
        <label class="text-slate-500">PlatformIO env (flash/build target)</label>
        <span
          class="text-[10px] px-1.5 py-0.5 rounded"
          :class="
            device.env_locked
              ? 'bg-amber-950/50 text-amber-400'
              : 'bg-slate-800 text-slate-400'
          "
          >{{ device.env_locked ? "manual" : "auto" }}</span
        >
      </div>
      <div class="flex gap-1.5 mt-1">
        <input
          v-model="envInput"
          list="envlist"
          placeholder="e.g. heltec-v4"
          class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        />
        <datalist id="envlist">
          <option v-for="e in envList" :key="e" :value="e" />
        </datalist>
        <button
          @click="pinEnv"
          :disabled="busy"
          class="px-2 py-1 rounded border border-emerald-700 text-emerald-300 hover:bg-emerald-700/40 disabled:opacity-40"
        >
          pin
        </button>
        <button
          @click="autoEnv"
          :disabled="busy"
          class="px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
          title="release the override and re-resolve env from hw_model"
        >
          auto
        </button>
      </div>
      <p class="text-[10px] text-slate-600 mt-1">
        current: <span class="mono text-emerald-300/70">{{ device.env || "—" }}</span>
        <span v-if="device.hw_model"> · hw {{ device.hw_model }}</span>
      </p>
    </div>

    <!-- generic node config set -->
    <div>
      <label class="text-slate-500">Set node config (advanced)</label>
      <div class="flex gap-1.5 mt-1">
        <input
          v-model="cfgPath"
          placeholder="path e.g. lora.region"
          class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        />
        <input
          v-model="cfgValue"
          placeholder="value e.g. US"
          class="w-28 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        />
        <button
          @click="setConfig"
          :disabled="busy"
          class="px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
        >
          set
        </button>
      </div>
    </div>

    <!-- uhubctl power port -->
    <div v-if="!isNative">
      <div class="flex items-center gap-2">
        <label class="text-slate-500">USB power port (uhubctl)</label>
        <span
          class="text-[10px] px-1.5 py-0.5 rounded"
          :class="
            device.hub_location != null
              ? 'bg-emerald-950/50 text-emerald-400'
              : 'bg-slate-800 text-slate-400'
          "
          >{{ device.hub_location != null ? currentSlot : "unmapped" }}</span
        >
      </div>
      <div v-if="!hubAvailable" class="text-[10px] text-amber-400/80 mt-1">
        uhubctl not available on this host
      </div>
      <div v-else class="flex gap-1.5 mt-1">
        <select
          v-model="selectedSlot"
          class="flex-1 bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none focus:border-emerald-700"
        >
          <option value="">— select hub:port —</option>
          <option v-for="s in slots" :key="s.value" :value="s.value">
            {{ s.label }}
          </option>
        </select>
        <button
          @click="assignSlot"
          :disabled="busy || !selectedSlot"
          class="px-2 py-1 rounded border border-emerald-700 text-emerald-300 hover:bg-emerald-700/40 disabled:opacity-40"
        >
          assign
        </button>
        <button
          @click="autoLocate"
          :disabled="busy"
          class="px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
          title="auto-bind if exactly one PPPS port matches this device's VID"
        >
          auto
        </button>
        <button
          v-if="device.hub_location != null"
          @click="clearSlot"
          :disabled="busy"
          class="px-2 py-1 rounded bg-slate-800 hover:bg-slate-700 disabled:opacity-40"
        >
          clear
        </button>
      </div>
    </div>

    <div
      v-if="msg"
      class="mono truncate"
      :class="ok ? 'text-slate-500' : 'text-rose-400'"
      :title="msg"
    >
      {{ msg }}
    </div>
  </div>
</template>
