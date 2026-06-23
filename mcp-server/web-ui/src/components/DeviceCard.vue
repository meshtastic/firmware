<script setup lang="ts">
import { computed, ref } from "vue";
import { useCamerasStore } from "../stores/cameras";
import { useDevicesStore } from "../stores/devices";
import { useFirmwareStore } from "../stores/firmware";
import type { Device } from "../types";
import CameraFeed from "./CameraFeed.vue";
import DeviceControls from "./DeviceControls.vue";
import DeviceSettings from "./DeviceSettings.vue";
import PacketPane from "./PacketPane.vue";
import SerialLogPane from "./SerialLogPane.vue";
import TestResultsPane from "./TestResultsPane.vue";

const props = defineProps<{ device: Device }>();
const devices = useDevicesStore();
const cameras = useCamerasStore();
const fw = useFirmwareStore();

const tab = ref<"serial" | "packets" | "results">("serial");
const editing = ref(false);
const nameDraft = ref("");
const showSettings = ref(false);

const assignedCamera = computed(() =>
  cameras.forDevice(props.device.serial_number),
);

// Circular node identifier (Meshtastic design standard): a deterministic color
// + short token per node. Color is used on the ring/text only — never as a row
// background wash.
function colorFor(seed: string): string {
  let h = 0;
  for (let i = 0; i < seed.length; i++) h = (h * 31 + seed.charCodeAt(i)) >>> 0;
  return `hsl(${h % 360} 60% 68%)`;
}
const nodeColor = computed(() =>
  props.device.online ? colorFor(props.device.serial_number) : "#5c5e78",
);
const nodeToken = computed(() => {
  const d = props.device;
  if (d.role === "native") return "DK";
  if (d.node_num) return d.node_num.toString(16).slice(-2).toUpperCase();
  return (d.serial_number || "??")
    .replace(/[^a-zA-Z0-9]/g, "")
    .slice(-2)
    .toUpperCase();
});

const flashedDrift = computed(
  () =>
    props.device.flashed_fw_sha &&
    fw.ref.sha &&
    props.device.flashed_fw_sha !== fw.ref.sha,
);

function startEdit() {
  nameDraft.value = props.device.friendly_name || "";
  editing.value = true;
}
async function saveName() {
  editing.value = false;
  await devices.setFriendlyName(props.device.serial_number, nameDraft.value);
}

async function onAssign(e: Event) {
  const val = (e.target as HTMLSelectElement).value;
  // find camera currently assigned and reassign; here we assign the picked
  // camera id to this device (or unassign all when "")
  const id = val ? Number(val) : null;
  // Unassign whatever is currently on this device first if changing.
  const current = assignedCamera.value;
  if (current && current.id !== id) await cameras.assign(current.id, null);
  if (id != null) await cameras.assign(id, props.device.serial_number);
}
</script>

<template>
  <div
    class="card-rail rounded-xl border bg-slate-900/60 p-4 flex flex-col gap-3"
    :class="
      device.online
        ? 'border-slate-700/80'
        : 'border-slate-800 opacity-60 is-offline'
    "
  >
    <!-- header -->
    <div class="flex items-start gap-2.5">
      <div
        class="node-id mt-0.5"
        :style="{ color: nodeColor }"
        :title="
          (device.online ? 'online' : 'offline') + ' · ' + device.serial_number
        "
      >
        <span class="text-slate-100">{{ nodeToken }}</span>
      </div>
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-2">
          <template v-if="editing">
            <input
              v-model="nameDraft"
              @keyup.enter="saveName"
              @blur="saveName"
              autofocus
              class="text-sm bg-slate-800 border border-slate-600 rounded px-1.5 py-0.5 outline-none"
            />
          </template>
          <template v-else>
            <span
              class="font-semibold text-slate-100 truncate cursor-pointer hover:text-emerald-300"
              @click="startEdit"
              :title="'click to rename · ' + device.serial_number"
            >
              {{ device.friendly_name || device.serial_number }}
            </span>
          </template>
          <span
            v-if="device.role"
            class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400 uppercase"
            >{{ device.role }}</span
          >
          <span
            v-if="!device.has_stable_id"
            class="text-[10px] px-1.5 py-0.5 rounded bg-amber-950/50 text-amber-400"
            title="no stable USB serial — won't follow across port changes"
            >no-serial</span
          >
        </div>
        <div class="text-xs text-slate-500 mono truncate">
          {{ device.current_port || "—" }}
          <span v-if="device.node_num" class="text-slate-600"
            >· !{{ device.node_num.toString(16) }}</span
          >
          <span v-if="device.stale" class="text-amber-500/70">· stale</span>
        </div>
        <!-- auto-sniffed specs: running firmware, hw model, region, exact env -->
        <div
          v-if="device.firmware_version || device.hw_model"
          class="text-[11px] mono truncate mt-0.5 flex flex-wrap items-center gap-x-1.5"
        >
          <span v-if="device.firmware_version" class="text-emerald-300/90"
            >v{{ device.firmware_version }}</span
          >
          <span v-if="device.hw_model" class="text-slate-400"
            >· {{ device.hw_model }}</span
          >
          <span
            v-if="device.region && device.region !== 'UNSET'"
            class="text-indigo-300/90"
            >· {{ device.region }}</span
          >
          <span
            v-else-if="device.region === 'UNSET'"
            class="text-amber-400/90"
            title="region unset — node will not transmit"
            >· region unset</span
          >
          <span v-if="device.env" class="text-slate-500"
            >· env <span class="text-slate-300">{{ device.env }}</span></span
          >
        </div>
      </div>
      <div class="flex items-center gap-1 shrink-0">
        <button
          @click="showSettings = !showSettings"
          class="p-1 rounded transition hover:bg-slate-800"
          :class="
            showSettings ? 'text-emerald-300' : 'text-slate-500 hover:text-slate-300'
          "
          title="device settings (env override, rename, node config)"
        >
          <svg
            viewBox="0 0 24 24"
            class="w-4 h-4"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
          >
            <circle cx="12" cy="12" r="3" />
            <path
              d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"
            />
          </svg>
        </button>
        <button
          @click="devices.refresh(device.serial_number)"
          class="p-1 rounded text-slate-500 hover:text-slate-300 transition hover:bg-slate-800"
          title="refresh device info"
        >
          <svg
            viewBox="0 0 24 24"
            class="w-4 h-4"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
          >
            <polyline points="23 4 23 10 17 10" />
            <polyline points="1 20 1 14 7 14" />
            <path
              d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"
            />
          </svg>
        </button>
      </div>
    </div>

    <!-- settings panel (cog) -->
    <DeviceSettings v-if="showSettings" :device="device" />

    <!-- flashed firmware ref -->
    <div class="text-[11px] flex items-center gap-1.5">
      <span class="text-slate-500">firmware:</span>
      <template v-if="device.flashed_fw_sha">
        <span class="mono text-emerald-300/80">{{
          device.flashed_fw_sha.slice(0, 7)
        }}</span>
        <span
          v-if="flashedDrift"
          class="text-amber-400"
          title="device firmware differs from the current checkout"
          >⚠ behind current ref</span
        >
      </template>
      <span v-else class="text-slate-600">unknown (not flashed via FleetSuite)</span>
    </div>

    <!-- camera -->
    <CameraFeed :camera="assignedCamera" />
    <select
      :value="assignedCamera?.id ?? ''"
      @change="onAssign"
      class="text-xs bg-slate-900 border border-slate-700 rounded px-2 py-1 outline-none"
    >
      <option value="">— no camera —</option>
      <option v-for="c in cameras.list" :key="c.id" :value="c.id">
        {{ c.name }} (idx {{ c.device_index }})
      </option>
    </select>

    <!-- tabs -->
    <div class="flex gap-1 text-xs border-b border-slate-800">
      <button
        v-for="t in ['serial', 'packets', 'results']"
        :key="t"
        @click="tab = t as any"
        class="px-2 py-1 capitalize -mb-px border-b-2 transition"
        :class="
          tab === t
            ? 'border-emerald-500 text-emerald-300'
            : 'border-transparent text-slate-500 hover:text-slate-300'
        "
      >
        {{ t }}
      </button>
    </div>
    <SerialLogPane v-if="tab === 'serial'" :serial="device.serial_number" />
    <PacketPane v-else-if="tab === 'packets'" :serial="device.serial_number" />
    <TestResultsPane v-else :serial="device.serial_number" />

    <DeviceControls :device="device" />
  </div>
</template>
