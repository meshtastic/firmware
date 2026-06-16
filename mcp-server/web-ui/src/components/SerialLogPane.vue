<script setup lang="ts">
import { onMounted, onUnmounted, reactive } from "vue";
import { useWsStore } from "../stores/ws";
import LogPane from "./LogPane.vue";

const props = defineProps<{ serial: string }>();
const ws = useWsStore();
const lines = reactive<string[]>([]);
const topic = `serial.${props.serial}`;

function onLine(d: any) {
  lines.push(d.line);
  if (lines.length > 2000) lines.splice(0, lines.length - 2000);
}

onMounted(() => ws.subscribe(topic, onLine));
onUnmounted(() => ws.unsubscribe(topic, onLine));
</script>

<template>
  <div class="h-48">
    <LogPane :lines="lines" placeholder="opening serial monitor…" />
  </div>
</template>
