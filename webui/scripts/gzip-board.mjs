// Gzips the Vite build (dist/index.html) into dist/index.html.gz, the page the board serves from its flash,
// and fails if it is over the size budget (REWRITE.md › "Fits on the board": no bigger than the old page).
// Run by `npm run build:board` after `vite build`.
import { readFileSync, writeFileSync } from "node:fs";
import { gzipSync, constants } from "node:zlib";

const BUDGET = 145 * 1024;   // bytes, gzipped

const html = readFileSync(new URL("../dist/index.html", import.meta.url));
const gz = gzipSync(html, { level: constants.Z_BEST_COMPRESSION });
writeFileSync(new URL("../dist/index.html.gz", import.meta.url), gz);

const kb = (n) => (n / 1024).toFixed(1);
console.log(`dist/index.html.gz: ${kb(gz.length)} KB gzipped (${kb(html.length)} KB raw), budget ${kb(BUDGET)} KB`);
if (gz.length > BUDGET) {
  console.error(`Over the ${kb(BUDGET)} KB budget by ${kb(gz.length - BUDGET)} KB`);
  process.exit(1);
}
