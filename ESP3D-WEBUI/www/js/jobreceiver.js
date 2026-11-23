// Receives a G-code file from another web page (Abundance) and puts it on the SD card.
//
// A page served over https can't make requests to the machine, which is plain http on the local
// network: browsers block that as mixed content. It can open this UI in a new tab, though, and
// hand the file over with window.postMessage. This UI then uploads the file to its own machine,
// the same way the Upload button does. docs/protocol.md section 7 describes the messages.
//
// The file is only uploaded and loaded for preview. Nothing here starts a job: the user presses
// Play. Messages from any page that isn't in ALLOWED_ORIGINS are ignored without a reply.
//
// import - machine, files_upload_files, files_currentPath, alertdlg, translate_text_item, Monitor_output_Update

const jobReceiver = (() => {
	const VERSION = 1;

	// Pages allowed to send a file. localhost is for people developing Abundance.
	const ALLOWED_ORIGINS = ["https://abundance.maslowcnc.com"];
	const LOCAL_ORIGIN = /^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/;

	const isAllowedOrigin = (origin) =>
		typeof origin === "string" && (ALLOWED_ORIGINS.includes(origin) || LOCAL_ORIGIN.test(origin));

	// The extensions the file list treats as G-code by default (prefDefs.js f_filters)
	const GCODE_NAME = /\.(g|gc|gco|gcode|nc|txt)$/i;
	const MAX_NAME_LENGTH = 60;

	/** A plain file name for the SD card: no path, no odd characters, a G-code extension. */
	const safeFileName = (name) => {
		let clean = String(name ?? "")
			.split(/[\\/]/)
			.pop()
			.replace(/[^A-Za-z0-9._ -]/g, "_")
			.replace(/^[. ]+/, "")
			.trim();
		if (!GCODE_NAME.test(clean)) {
			clean = `${clean || "abundance"}.nc`;
		}
		if (clean.length > MAX_NAME_LENGTH) {
			const dot = clean.lastIndexOf(".");
			clean = clean.slice(0, MAX_NAME_LENGTH - (clean.length - dot)) + clean.slice(dot);
		}
		return clean;
	};

	/** Why a file can't be taken right now ({reason, detail}), or null if it can. */
	const refusal = (state, uiReady) => {
		if (!uiReady) {
			return { reason: "not-ready", detail: "The Maslow page is still connecting to the machine" };
		}
		if (state.job || ["Run", "Hold", "Jog", "Home"].includes(state.grbl)) {
			return { reason: "busy", detail: "The machine is running" };
		}
		return null;
	};

	const uiReady = () =>
		typeof setup_is_done !== "undefined" && setup_is_done && typeof direct_sd !== "undefined" && !!direct_sd;

	const toFile = (gcode, name) => {
		if (typeof gcode === "string" && gcode.length) {
			return new File([gcode], name, { type: "text/plain" });
		}
		if (gcode instanceof Blob && gcode.size) {
			return new File([gcode], name, { type: "text/plain" });
		}
		return null;
	};

	/**
	 * Handle one message event. deps is for the tests; the UI's own functions are the default.
	 * Resolves with the reply that was posted back, or null if the message was ignored.
	 */
	const handleMessage = async (event, deps = {}) => {
		const data = event.data;
		if (!isAllowedOrigin(event.origin) || !data || typeof data.type !== "string" || !event.source) {
			return null;
		}
		const ready = deps.uiReady ?? uiReady;
		const state = deps.state ?? machine.state;
		const upload = deps.upload ?? ((file) => files_upload_files(files_currentPath(), [file]));
		const notify = deps.notify ?? showRefusal;
		const reply = (message) => {
			const full = { version: VERSION, ...message };
			event.source.postMessage(full, event.origin);
			return full;
		};

		if (data.type === "maslow:hello") {
			// No reply until the UI can take a file, so the sender keeps asking
			return ready() ? reply({ type: "maslow:ready" }) : null;
		}
		if (data.type !== "maslow:gcode") {
			return null;
		}

		const result = (fields) => reply({ type: "maslow:result", id: data.id, ...fields });
		const name = safeFileName(data.name);
		const file = toFile(data.gcode, name);
		if (!file) {
			return result({ ok: false, reason: "invalid", detail: "gcode must be a non-empty string or Blob" });
		}
		const refused = refusal(state, ready());
		if (refused) {
			notify(name, refused);
			return result({ ok: false, ...refused });
		}
		try {
			const uploaded = await upload(file);
			return result({ ok: true, name: uploaded.name, path: uploaded.path });
		} catch (err) {
			return result({ ok: false, reason: err.reason ?? "failed", detail: err.detail ?? String(err.message ?? err) });
		}
	};

	// The user is looking at this tab when the file arrives, so say here why it wasn't taken
	const showRefusal = (name, refused) => {
		const text = `${translate_text_item("Could not receive")} ${name}: ${translate_text_item(refused.detail)}`;
		Monitor_output_Update(`[Upload] ${text}\n`);
		alertdlg(translate_text_item("Error"), text);
	};

	if (typeof window !== "undefined" && typeof window.addEventListener === "function") {
		window.addEventListener("message", (event) => {
			handleMessage(event);
		});
	}

	return { handleMessage, isAllowedOrigin, safeFileName, refusal };
})();

if (typeof globalThis !== "undefined") {
	globalThis.__jobReceiverTestApi = jobReceiver;
}
