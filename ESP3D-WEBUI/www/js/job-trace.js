// Where the machine thinks it went during a job, for the red trace over the G-code preview.
//
// Each status report carries the commanded position (MPos, from the step counters, i.e. the
// planned path at that moment) and each belt's position error (BE: encoder minus target, mm).
// A belt that is e mm longer than its target puts the sled about e mm further from that anchor,
// so the encoder-implied position is MPos plus the least-squares XY offset that explains the
// four belt errors. This is what the firmware's encoders say, not what the bit did: anything
// after the spools (belt stretch, sled friction, tilt) is invisible here.
//
// Points are machine coordinates (mm), the same frame the preview draws the G-code in
// (WPOS + WCO). A trace starts when a job starts and stays until the next one.

const jobTrace = (() => {
	const MAX_POINTS = 200000; // ~3 h of reports at 50 ms; the oldest are dropped beyond this
	const MIN_STEP_MM = 0.02; // skip reports that didn't move, so a pause doesn't pile up points

	let points = [];
	let drawn = 0; // points already on the canvas (toolpath-displayer.js draws the rest)
	let jobRunning = false;

	/** Firmware frame centre: where the anchor diagonals cross (MaslowKinematics::calculateCenter).
	 * Machine (0,0) is this point; the anchors are in a frame with BL at (0,0). */
	const frameCentre = (a) => {
		const A = (a.tr.y - a.bl.y) / (a.tr.x - a.bl.x);
		const B = (a.br.y - a.tl.y) / (a.br.x - a.tl.x);
		const x = (a.br.y - B * a.br.x + A * a.tr.x - a.tr.y) / (A - B);
		return { x, y: A * (x - a.tr.x) + a.tr.y };
	};

	/**
	 * Encoder-implied position for one report.
	 * mpos: {x, y, z} machine position, mm. errors: {etl, etr, ebl, ebr}, mm (encoder minus target).
	 * anchors: {tl, tr, bl, br} each {x, y}, in the firmware's anchor frame.
	 * Returns {x, y, z}, or null if the inputs can't give one.
	 */
	const actualPosition = (mpos, errors, anchors) => {
		if (!mpos || !errors || !anchors) {
			return null;
		}
		const e = [errors.etl, errors.etr, errors.ebl, errors.ebr];
		if (!e.every(Number.isFinite) || ![mpos.x, mpos.y].every(Number.isFinite)) {
			return null;
		}
		const c = frameCentre(anchors);
		const p = { x: mpos.x + c.x, y: mpos.y + c.y };
		// Normal equations of u_i . d = e_i, u_i the unit vector from anchor i to the sled
		let sxx = 0,
			sxy = 0,
			syy = 0,
			bx = 0,
			by = 0;
		[anchors.tl, anchors.tr, anchors.bl, anchors.br].forEach((anchor, i) => {
			const dx = p.x - anchor.x;
			const dy = p.y - anchor.y;
			const len = Math.hypot(dx, dy);
			const ux = dx / len;
			const uy = dy / len;
			sxx += ux * ux;
			sxy += ux * uy;
			syy += uy * uy;
			bx += ux * e[i];
			by += uy * e[i];
		});
		const det = sxx * syy - sxy * sxy;
		if (!Number.isFinite(det) || Math.abs(det) < 1e-9) {
			return null;
		}
		return {
			x: mpos.x + (syy * bx - sxy * by) / det,
			y: mpos.y + (sxx * by - sxy * bx) / det,
			z: mpos.z,
		};
	};

	const clear = () => {
		points = [];
		drawn = 0;
	};

	/** Feed every machine state snapshot here (machine.on("state")). Returns true if a point
	 * was added. anchors: as for actualPosition. */
	const addSample = (s, anchors) => {
		const running = !!s.job;
		if (running && !jobRunning) {
			clear(); // a new job (or a resume) starts a new trace
		}
		jobRunning = running;
		if (!running) {
			return false;
		}
		const p = actualPosition(s.mpos, s.belts, anchors);
		if (!p) {
			return false;
		}
		const last = points[points.length - 1];
		if (last && Math.hypot(p.x - last.x, p.y - last.y, p.z - last.z) < MIN_STEP_MM) {
			return false;
		}
		points.push(p);
		if (points.length > MAX_POINTS) {
			const drop = points.length - MAX_POINTS + MAX_POINTS / 10;
			points.splice(0, drop);
			drawn = Math.max(0, drawn - drop);
		}
		return true;
	};

	return {
		actualPosition,
		addSample,
		clear,
		points: () => points,
		/** Points not yet drawn, starting with the last drawn one so the line joins up. */
		undrawn: () => points.slice(Math.max(0, drawn - 1)),
		markDrawn: () => {
			drawn = points.length;
		},
	};
})();

globalThis.jobTrace = jobTrace;
