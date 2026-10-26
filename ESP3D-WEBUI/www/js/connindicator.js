// Connection dot in the navbar (#connDot): green when the machine answered recently, amber
// when the socket claims to be open but nothing has arrived for a few seconds (e.g. WiFi
// dropped without the socket noticing), red when the socket isn't open. Click to reconnect.

const connIndicator = (() => {
	const UPDATE_MS = 500;
	const CLASSES = { ok: "conn-ok", quiet: "conn-quiet", down: "conn-down" };

	const describe = (health) => {
		const since = wsCommands.msSinceRx();
		const heard = since === null ? "nothing yet" : `${(since / 1000).toFixed(1)} s ago`;
		const sock = wsCommands.socketStatus();
		if (health === "ok") {
			return `Connected to the machine (last heard ${heard})`;
		}
		if (health === "quiet") {
			return `No reply from the machine for ${heard.replace(" ago", "")}; connection may be lost. Click to reconnect.`;
		}
		return `Not connected (socket ${sock}). Click to reconnect.`;
	};

	let lastHealth = "";

	const update = () => {
		const dot = document.getElementById("connDot");
		if (!dot) {
			return; // navbar not loaded yet
		}
		const health = wsCommands.health();
		if (health !== lastHealth) {
			dot.classList.remove(...Object.values(CLASSES));
			dot.classList.add(CLASSES[health]);
			lastHealth = health;
		}
		dot.title = describe(health);
		if (!dot.dataset.wired) {
			dot.dataset.wired = "1";
			dot.addEventListener("click", () => {
				if (wsCommands.health() !== "ok" && typeof resetConnectionState === "function") {
					resetConnectionState();
				}
			});
		}
	};

	if (typeof document !== "undefined") {
		setInterval(update, UPDATE_MS);
	}

	return { update };
})();

globalThis.connIndicator = connIndicator;
