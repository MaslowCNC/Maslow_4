// Saving the page's log to a text file, for support. Replaces the current UI's "Save Serial" (tablet.js,
// saveSerialMessages). Each log line already starts with its time (HH:MM:SS); the header gives the date.

const pad = (n) => String(n).padStart(2, "0");
const day = (date) => `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
const clock = (date) => `${pad(date.getHours())}-${pad(date.getMinutes())}-${pad(date.getSeconds())}`;

// maslow-log-2026-11-18-14-05-09.txt: the time too, so two saves on one day don't collide
export const logFileName = (date = new Date()) => `maslow-log-${day(date)}-${clock(date)}.txt`;

export function logFileText(lines, { date = new Date(), machine = null } = {}) {
  const head = [
    `Maslow log saved ${day(date)} ${date.toTimeString().slice(0, 8)}`,
    `Machine: ${machine || "not known yet"}`,
    `Page: ${typeof location !== "undefined" ? location.href : ""}`,
    `Lines: ${lines.length} (the page keeps the last 200)`,
    ""
  ];
  return [...head, ...lines, ""].join("\n");
}

// Hand the browser a file to save
export function downloadLog(lines, info) {
  const date = new Date();
  const url = URL.createObjectURL(new Blob([logFileText(lines, { ...info, date })], { type: "text/plain;charset=utf-8" }));
  const a = Object.assign(document.createElement("a"), { href: url, download: logFileName(date) });
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
