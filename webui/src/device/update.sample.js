// Stand-ins for the simulator and the tests: GitHub's release list (newest first, as its API answers), a firmware
// image that passes checkFirmware, and the mirror's manifest.json for it.

const notes = (lines) => lines.join("\n");

export const SIM_RELEASES = [
  { tag_name: "v1.29.0-exp.1", name: "v1.29.0-exp.1", prerelease: true, draft: false, published_at: "2026-11-26T18:02:00Z",
    body: notes(["Experimental: please test and report in the forum thread.", "", "- Find anchors reports its progress", "- Done extending"]),
    assets: [{ name: "firmware.bin" }, { name: "index.html.gz" }] },
  { tag_name: "v1.28.0", name: "v1.28.0", prerelease: false, draft: false, published_at: "2026-11-24T15:10:00Z",
    body: notes(["- The new web page", "- Stock thickness counts in Find anchors (#1164)", "- Wrong encoderTeeth warning (#1162)", "- Update streams (#1142)"]),
    assets: [{ name: "firmware.bin" }, { name: "index.html.gz" }] },
  { tag_name: "v1.28.0-exp.2", name: "v1.28.0-exp.2", prerelease: true, draft: false, published_at: "2026-11-20T11:00:00Z",
    body: "Second test build of v1.28.0.", assets: [{ name: "firmware.bin" }, { name: "index.html.gz" }] },
  { tag_name: "v1.27.0", name: "v1.27.0", prerelease: false, draft: false, published_at: "2026-11-02T09:30:00Z",
    body: "Stable release.", assets: [{ name: "firmware.bin" }, { name: "index.html.gz" }] }
];

/** A small ESP32-S3 app image with a Maslow version string (null: no Maslow string, as plain FluidNC). */
export function simFirmware(version, size = 128 * 1024) {
  const b = new Uint8Array(size);
  b[0] = 0xe9; b[1] = 6; b[12] = 9;   // image magic, segments, chip id ESP32-S3
  for (let i = 64; i < size; i++) b[i] = (i * 31) & 0x7f;   // something that isn't a version string
  if (version) {
    const text = `\0${version}\0https://github.com/MaslowCNC/Maslow_4\0`;
    for (let i = 0; i < text.length; i++) b[4096 + i] = text.charCodeAt(i);
  }
  return b;
}

/** The mirror's manifest.json for these files (no checksums: the simulator doesn't compute them). */
export function simManifest(tag, firmware, page) {
  return {
    tag_name: tag, published_at: "2026-11-24T15:10:00Z",
    files: { "firmware.bin": { size_bytes: firmware.length }, ...(page ? { "index.html.gz": { size_bytes: page.length } } : {}) }
  };
}
