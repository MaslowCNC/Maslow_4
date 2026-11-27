// Receives a G-code file from another web page (Abundance) and puts it on the SD card (#1166).
//
// A page served over https can't make requests to the machine, which is plain http on the local network: browsers
// block that as mixed content. It can open this UI in a new tab, though, and hand the file over with
// window.postMessage. This UI then uploads the file to its own machine, the same way the Upload button does.
// docs/protocol.md section 7 describes the messages.
//
// The same rules and replies as the current UI's ESP3D-WEBUI/www/js/jobreceiver.js (its globals don't exist here, so
// this is a copy that takes the page's own functions instead; jobreceiver.test.js checks the two agree). The file is
// only uploaded and chosen. Nothing here starts a job: the user presses Run. Messages from any page that isn't
// allowed are ignored without a reply.

export const VERSION = 1;

// Pages allowed to send a file. localhost is for people developing Abundance.
const ALLOWED_ORIGINS = ["https://abundance.maslowcnc.com"];
const LOCAL_ORIGIN = /^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/;

export const isAllowedOrigin = (origin) =>
  typeof origin === "string" && (ALLOWED_ORIGINS.includes(origin) || LOCAL_ORIGIN.test(origin));

// The extensions the current UI's file list treats as G-code by default (prefDefs.js f_filters)
const GCODE_NAME = /\.(g|gc|gco|gcode|nc|txt)$/i;
const MAX_NAME_LENGTH = 60;

/** A plain file name for the SD card: no path, no odd characters, a G-code extension. */
export function safeFileName(name) {
  let clean = String(name ?? "")
    .split(/[\\/]/)
    .pop()
    .replace(/[^A-Za-z0-9._ -]/g, "_")
    .replace(/^[. ]+/, "")
    .trim();
  if (!GCODE_NAME.test(clean)) clean = `${clean || "abundance"}.nc`;
  if (clean.length > MAX_NAME_LENGTH) {
    const dot = clean.lastIndexOf(".");
    clean = clean.slice(0, MAX_NAME_LENGTH - (clean.length - dot)) + clean.slice(dot);
  }
  return clean;
}

/** Why a file can't be taken right now ({reason, detail}), or null if it can. state: { job, grbl } */
export function refusal(state, uiReady) {
  if (!uiReady) return { reason: "not-ready", detail: "The Maslow page is still connecting to the machine" };
  if (state.job || ["Run", "Hold", "Jog", "Home"].includes(state.grbl)) return { reason: "busy", detail: "The machine is running" };
  return null;
}

function toFile(gcode, name) {
  if ((typeof gcode === "string" && gcode.length) || (gcode instanceof Blob && gcode.size)) {
    return new File([gcode], name, { type: "text/plain" });
  }
  return null;
}

/**
 * Handle one message event. Resolves with the reply that was posted back, or null if the message was ignored.
 * deps: { ready() → bool, state() → { job, grbl }, upload(file) → Promise<{ name, path }> (rejects with
 * { reason, detail }), notify(name, refused) for a refusal, received(uploaded) once the file is on the card }
 */
export async function handleMessage(event, deps) {
  const data = event.data;
  if (!isAllowedOrigin(event.origin) || !data || typeof data.type !== "string" || !event.source) return null;
  const reply = (message) => {
    const full = { version: VERSION, ...message };
    event.source.postMessage(full, event.origin);
    return full;
  };

  if (data.type === "maslow:hello") {
    // No reply until the UI can take a file, so the sender keeps asking
    return deps.ready() ? reply({ type: "maslow:ready" }) : null;
  }
  if (data.type !== "maslow:gcode") return null;

  const result = (fields) => reply({ type: "maslow:result", id: data.id, ...fields });
  const name = safeFileName(data.name);
  const file = toFile(data.gcode, name);
  if (!file) return result({ ok: false, reason: "invalid", detail: "gcode must be a non-empty string or Blob" });
  const refused = refusal(deps.state(), deps.ready());
  if (refused) {
    deps.notify?.(name, refused);
    return result({ ok: false, ...refused });
  }
  try {
    const uploaded = await deps.upload(file);
    deps.received?.(uploaded);
    return result({ ok: true, name: uploaded.name, path: uploaded.path });
  } catch (err) {
    return result({ ok: false, reason: err.reason ?? "failed", detail: err.detail ?? String(err.message ?? err) });
  }
}
