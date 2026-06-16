import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import tailwindcss from "@tailwindcss/vite";

// Backend (FastAPI) dev address. The pywebview/production build serves the
// SPA from FastAPI itself, so these proxies only matter under `npm run dev`.
const BACKEND = "http://127.0.0.1:8765";

export default defineConfig({
  plugins: [vue(), tailwindcss()],
  server: {
    proxy: {
      "/api": { target: BACKEND, changeOrigin: true },
      "/ws": { target: BACKEND.replace("http", "ws"), ws: true },
    },
  },
  build: {
    // Built SPA is served by FastAPI from src/meshtastic_mcp/web/static.
    outDir: "../src/meshtastic_mcp/web/static",
    emptyOutDir: true,
  },
});
