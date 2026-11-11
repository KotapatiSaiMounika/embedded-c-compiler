import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// During `npm run dev`, requests to /api/... are forwarded to the C server,
// so the browser never has to deal with CORS.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": "http://127.0.0.1:8080",
    },
  },
});