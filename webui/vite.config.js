import { defineConfig } from "vite";
import preact from "@preact/preset-vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// One self-contained index.html, so it can later be gzipped and served from the board like the current WebUI.
export default defineConfig({
  plugins: [preact(), viteSingleFile()],
  build: { target: "es2020", assetsInlineLimit: 100000000 }
});
