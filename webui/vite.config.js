import { defineConfig } from "vite";
import preact from "@preact/preset-vite";
import { viteSingleFile } from "vite-plugin-singlefile";
import { lookup } from "node:dns/promises";

// The machine the dev server forwards to, for http://localhost:5199/?device=machine. MASLOW_HOST overrides
// maslow.local. The name is looked up once at startup: on some Macs each .local lookup takes about 5 s.
async function machineAddress() {
  const host = process.env.MASLOW_HOST || "maslow.local";
  try {
    return (await lookup(host, { family: 4 })).address;
  } catch {
    return host;   // not found now; the proxy will try the name itself
  }
}

// One self-contained index.html, so it can be gzipped and served from the board like the current WebUI.
export default defineConfig(async ({ command }) => {
  const machine = command === "serve" ? await machineAddress() : null;
  return {
    plugins: [preact(), viteSingleFile()],
    build: { target: "es2020", assetsInlineLimit: 100000000 },
    server: {
      // machine.js and wscommands.js are imported from ESP3D-WEBUI/www/js, outside this folder
      fs: { allow: [".."] },
      // The page talks to the machine at its own address, so the dev server forwards the socket and file requests
      proxy: machine ? {
        "/ws": { target: `ws://${machine}`, ws: true, rewrite: () => "/" },   // the firmware's socket is at "/"
        "/upload": { target: `http://${machine}` },
        "/command": { target: `http://${machine}` },
        "/SD": { target: `http://${machine}` }   // file contents, to count a job's lines
      } : undefined
    }
  };
});
