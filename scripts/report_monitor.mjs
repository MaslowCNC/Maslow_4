#!/usr/bin/env node
// Measure status-report size and delivery on a Maslow over WiFi.
//
// Connects as its own WebSocket session (?independent_session), so a browser tab on the same
// machine keeps its connection, and asks for a sequence number on its reports (?report_seq), which
// other clients don't get. Sets $Report/Interval for this connection only, and otherwise only
// listens: it sends nothing that moves the machine. Drive the machine from the UI while it runs.
//
//   node scripts/report_monitor.mjs [host] [--interval 50] [--seconds 60] [--heap] [--csv out.csv]
//
//   host        machine address (default m4.local)
//   --interval  report interval to ask for, ms (default 50, what the UI uses)
//   --seconds   stop after this long (default: run until Ctrl-C)
//   --heap      run $Heap at the start and at the end (free memory and its low point)
//   --csv       also write the per-second numbers to a CSV file
//
// Each second it prints: machine state, reports/s, frames/s, bytes/s, average and largest report
// size, reports missing (gaps in the |N: sequence number; needs firmware that supports
// ?report_seq), the longest gap between reports while moving, and which Maslow fields
// (MS BL BE MC) were seen. Ctrl-C prints totals.
//
// Needs Node 22+ (built-in WebSocket).

import { writeFileSync, appendFileSync } from "node:fs";

const args = process.argv.slice(2);
const flag = (name) => args.includes(`--${name}`);
const option = (name, fallback) => {
	const i = args.indexOf(`--${name}`);
	return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : fallback;
};
const host = args.find((a, i) => !a.startsWith("--") && !(i > 0 && args[i - 1].startsWith("--") && ["interval", "seconds", "csv"].includes(args[i - 1].slice(2)))) ?? "m4.local";
const intervalMs = Number(option("interval", 50));
const seconds = Number(option("seconds", 0));
const csv = option("csv", "");
const wantHeap = flag("heap");

const MOVING = ["Run", "Jog", "Home"];
const LATE_MS = 2 * intervalMs; // a gap this long while moving means a report was dropped or held up

// Per-second window and run totals
const fresh = () => ({ reports: 0, frames: 0, bytes: 0, reportBytes: 0, maxReport: 0, missing: 0, maxGapMoving: 0, fields: new Set(), state: "" });
let win = fresh();
const total = { reports: 0, frames: 0, bytes: 0, reportBytes: 0, maxReport: 0, missing: 0, late: 0, maxGapMoving: 0, seconds: 0 };
let lastSeq = null;
let lastReportAt = null;
let lastState = "";
let buf = "";
const heap = { start: null, end: null };
let heapWaiter = null;

const ws = new WebSocket(`ws://${host}/?independent_session&report_seq`, ["arduino"]);
ws.binaryType = "arraybuffer";

const handleLine = (line, now) => {
	if (line.startsWith("[MSG:INFO: Heap free")) {
		if (heapWaiter) {
			heapWaiter(line);
			heapWaiter = null;
		}
		return;
	}
	if (!line.startsWith("<")) {
		return;
	}
	const bytes = line.length + 1;
	const fields = line.slice(1, line.lastIndexOf(">")).split("|");
	const state = fields[0].split(":")[0];
	win.reports++;
	win.reportBytes += bytes;
	win.maxReport = Math.max(win.maxReport, bytes);
	win.state = state;
	for (const f of fields.slice(1)) {
		const key = f.slice(0, f.indexOf(":"));
		if (key === "N") {
			const seq = Number(f.slice(2));
			if (lastSeq !== null && seq > lastSeq + 1) {
				win.missing += seq - lastSeq - 1;
			}
			lastSeq = seq;
		} else if (["MS", "BL", "BE", "MC"].includes(key)) {
			win.fields.add(key);
		}
	}
	if (lastReportAt !== null && MOVING.includes(state) && MOVING.includes(lastState)) {
		const gap = now - lastReportAt;
		win.maxGapMoving = Math.max(win.maxGapMoving, gap);
		if (gap > LATE_MS) {
			total.late++;
		}
	}
	lastReportAt = now;
	lastState = state;
};

ws.onmessage = (e) => {
	const now = performance.now();
	const text = typeof e.data === "string" ? e.data : Buffer.from(e.data).toString("latin1");
	win.frames++;
	win.bytes += text.length;
	buf += typeof e.data === "string" ? `${text}\n` : text;
	let i;
	while ((i = buf.indexOf("\n")) >= 0) {
		const line = buf.slice(0, i).trim();
		buf = buf.slice(i + 1);
		if (line) {
			handleLine(line, now);
		}
	}
};

const askHeap = () =>
	new Promise((resolve) => {
		heapWaiter = resolve;
		ws.send("$Heap\n");
		setTimeout(() => {
			if (heapWaiter) {
				heapWaiter = null;
				resolve("(no $Heap reply)");
			}
		}, 2000);
	});

const pad = (v, n) => String(v).padStart(n);
const header = "  t  state  rep/s frm/s  bytes/s  avg  max  missing  gap(ms)  fields";
const csvHeader = "t,state,reports,frames,bytes,avgReport,maxReport,missing,maxGapMovingMs,fields\n";

const tick = () => {
	total.seconds++;
	const avg = win.reports ? Math.round(win.reportBytes / win.reports) : 0;
	const gap = win.maxGapMoving ? Math.round(win.maxGapMoving) : "";
	const fields = [...win.fields].join(" ");
	console.log(
		`${pad(total.seconds, 3)}  ${(win.state || "-").padEnd(5)} ${pad(win.reports, 5)} ${pad(win.frames, 5)} ${pad(win.bytes, 8)} ${pad(avg, 4)} ${pad(win.maxReport, 4)} ${pad(win.missing, 8)} ${pad(gap, 8)}  ${fields}`,
	);
	if (csv) {
		appendFileSync(csv, `${total.seconds},${win.state},${win.reports},${win.frames},${win.bytes},${avg},${win.maxReport},${win.missing},${gap},${fields}\n`);
	}
	total.reports += win.reports;
	total.frames += win.frames;
	total.bytes += win.bytes;
	total.reportBytes += win.reportBytes;
	total.maxReport = Math.max(total.maxReport, win.maxReport);
	total.missing += win.missing;
	total.maxGapMoving = Math.max(total.maxGapMoving, win.maxGapMoving);
	win = fresh();
};

let ticker = null;
let finishing = false;
const finish = async () => {
	if (finishing) {
		return;
	}
	finishing = true;
	clearInterval(ticker);
	if (wantHeap && ws.readyState === WebSocket.OPEN) {
		heap.end = await askHeap();
	}
	const s = Math.max(total.seconds, 1);
	console.log("\nTotals");
	console.log(`  ${total.seconds} s, ${total.reports} reports (${(total.reports / s).toFixed(1)}/s), ${total.frames} frames, ${total.bytes} bytes (${Math.round(total.bytes / s)} B/s)`);
	console.log(`  report size: average ${total.reports ? Math.round(total.reportBytes / total.reports) : 0} B, largest ${total.maxReport} B`);
	console.log(`  missing reports (N gaps): ${total.missing}${lastSeq === null ? " (no N field: firmware without ?report_seq)" : ""}`);
	console.log(`  gaps over ${LATE_MS} ms while moving: ${total.late}, longest ${Math.round(total.maxGapMoving)} ms`);
	if (wantHeap) {
		console.log(`  heap at start: ${heap.start}`);
		console.log(`  heap at end:   ${heap.end}`);
	}
	ws.close();
	setTimeout(() => process.exit(0), 200);
};

ws.onopen = async () => {
	console.log(`Connected to ${host}; asking for a report every ${intervalMs} ms on this connection. Ctrl-C to stop.`);
	ws.send(`$Report/Interval=${intervalMs}\n`);
	if (wantHeap) {
		heap.start = await askHeap();
		console.log(`heap: ${heap.start}`);
	}
	if (csv) {
		writeFileSync(csv, csvHeader);
	}
	console.log(header);
	ticker = setInterval(tick, 1000);
	if (seconds > 0) {
		setTimeout(finish, seconds * 1000);
	}
};
ws.onclose = () => {
	if (!finishing) {
		console.log("Connection closed by the machine.");
		finish();
	}
};
ws.onerror = (e) => {
	console.error(`WebSocket error (${host}):`, e.message ?? e);
	process.exit(1);
};
process.on("SIGINT", finish);
