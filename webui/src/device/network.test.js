import { test } from "node:test";
import assert from "node:assert/strict";
import { parseNetSettings, parseNetStatus, parseApList, onFallbackHotspot, setCommand, netError, netChanges } from "./network.js";
import { createSimulator } from "./simulator.js";

// Shaped as the firmware's [ESP400] (WebCommands.cpp listSettings, JSONEncoder begin_webui)
const ESP400 = JSON.stringify({ EEPROM: [
  { F: "nvs", P: "Sta/SSID", H: "Sta/SSID", T: "S", V: "Workshop", S: 32, M: 0 },
  { F: "nvs", P: "Sta/Password", H: "Sta/Password", T: "S", V: "********", S: 64, M: 8 },
  { F: "nvs", P: "WiFi/Mode", H: "WiFi/Mode", T: "B", V: "3", O: [{ Off: 0 }, { STA: 1 }, { AP: 2 }, { "STA>AP": 3 }] },
  { F: "nvs", P: "AP/SSID", H: "AP/SSID", T: "S", V: "maslow", S: 32, M: 1 },
  { F: "nvs", P: "Hostname", H: "Hostname", T: "S", V: "maslow", S: 32, M: 1 },
  { F: "tree", P: "/kinematics/MaslowKinematics/tlX", H: "x", T: "R", V: "0" }
] });

test("[ESP400] gives the network settings, with WiFi/Mode by its option name", () => {
  assert.deepEqual(parseNetSettings(ESP400), { ssid: "Workshop", mode: "STA>AP", apSsid: "maslow", hostname: "maslow" });
});

test("[ESP420] gives the network the board is on, and its hotspot", () => {
  const text = "[MSG:Chip ID: 123]\n[MSG:Mode=STA:SSID=Shop: 2nd floor:Status=Connected:IP=192.168.1.42:MAC=AA-BB]\n[MSG:Mode=AP:SSID=maslow:IP=192.168.0.1:MAC=CC-DD]\nok";
  assert.deepEqual(parseNetStatus(text), {
    sta: { ssid: "Shop: 2nd floor", connected: true, ip: "192.168.1.42" },
    ap: { ssid: "maslow", ip: "192.168.0.1" }
  });
  assert.deepEqual(parseNetStatus("[MSG:No Wifi]"), { sta: null, ap: null });
});

test("[ESP410]'s scan is sorted strongest first, one entry per name, hidden networks left out", () => {
  const scan = { AP_LIST: [
    { SSID: "Weak", SIGNAL: "20", IS_PROTECTED: "1" }, { SSID: "Guest", SIGNAL: "70", IS_PROTECTED: "0" },
    { SSID: "Weak", SIGNAL: "45", IS_PROTECTED: "1" }, { SSID: "", SIGNAL: "90", IS_PROTECTED: "1" }
  ] };
  assert.deepEqual(parseApList(JSON.stringify(scan)), [
    { ssid: "Guest", signal: 70, locked: false }, { ssid: "Weak", signal: 45, locked: true }
  ]);
});

test("[ESP401] %-encodes the value: the firmware stops at a space and decodes it", () => {
  assert.equal(setCommand("ssid", "Shop 2"), "[ESP401]P=Sta/SSID V=Shop%202");
  assert.equal(setCommand("password", "a\\b c%d"), "[ESP401]P=Sta/Password V=a%5Cb%20c%25d");
});

test("values are checked against the firmware's limits", () => {
  assert.equal(netError("password", "short"), "At least 8 characters");
  assert.equal(netError("hostname", "my maslow"), "Letters, numbers and dashes only");
  assert.equal(netError("apSsid", ""), "Can't be empty");
  assert.equal(netError("mode", "Off"), "Choose an option");
  assert.equal(netError("ssid", "x".repeat(33)), "At most 32 characters");
  assert.equal(netError("mode", "STA>AP"), null);
});

test("only changed settings are sent; passwords only when typed; a new network needs its password unless open", () => {
  const cur = parseNetSettings(ESP400);
  const same = { ssid: "Workshop", password: "", mode: "STA>AP", apSsid: "maslow", apPassword: "" };
  assert.deepEqual(netChanges(same, cur), { changes: [], error: null });
  assert.deepEqual(netChanges({ ...same, mode: "AP", apPassword: "hotspot99" }, cur).changes,
    [["mode", "AP"], ["apPassword", "hotspot99"]]);
  assert.match(netChanges({ ...same, mode: "STA" }, cur).error, /Choose an option/);   // WiFi only isn't offered
  assert.equal(netChanges({ ...same, ssid: "Office" }, cur).error, "Enter the password for Office");
  assert.deepEqual(netChanges({ ...same, ssid: "Guest", open: true }, cur).changes, [["ssid", "Guest"]]);
  assert.deepEqual(netChanges({ ...same, ssid: "Office", password: "secret123" }, cur).changes,
    [["ssid", "Office"], ["password", "secret123"]]);
  assert.match(netChanges({ ...same, apSsid: "" }, cur).error, /^Hotspot name/);
});

test("the simulator saves network settings, hides passwords in the log, and asks for a restart", async () => {
  const sim = createSimulator();
  sim.send("readNetwork");
  await new Promise(r => setTimeout(r, 450));
  assert.equal(sim.getState().net.settings.ssid, "Workshop");
  sim.send("saveNetwork", { ssid: "Office", password: "secret123", mode: "STA>AP", apSsid: "maslow", apPassword: "" });
  const s = sim.getState();
  assert.equal(s.net.settings.ssid, "Office");
  assert.equal(s.net.restartNeeded, true);
  assert.ok(s.log.some(l => l.endsWith("[ESP401]P=Sta/SSID V=Office")));
  assert.ok(!s.log.some(l => l.includes("secret123")));
  sim.send("reboot");
  assert.equal(sim.getState().net.restartNeeded, false);
});

test("the board is on a fallback hotspot when it runs the hotspot without WiFi, unless set to Hotspot only", () => {
  const ap = parseNetStatus("[MSG:Mode=AP:SSID=maslow:IP=192.168.0.1:MAC=80-B5-4E-64-5E-8D]");
  const sta = parseNetStatus("[MSG:Mode=STA:SSID=Workshop:Status=Connected:IP=10.0.0.5:MAC=80-B5-4E-64-5E-8C]");
  assert.equal(onFallbackHotspot(ap, null), true);                    // first boot, settings not read yet
  assert.equal(onFallbackHotspot(ap, { mode: "STA>AP" }), true);      // couldn't join, so it fell back
  assert.equal(onFallbackHotspot(ap, { mode: "AP" }), false);         // the hotspot is what was asked for
  assert.equal(onFallbackHotspot(sta, { mode: "STA>AP" }), false);
  assert.equal(onFallbackHotspot(parseNetStatus(""), null), false);   // WiFi off, or no answer
});

test("before saving, a network the board can't see and the way back in are spelled out", async () => {
  const { netWarnings } = await import("./network.js");
  const current = { ssid: "Home", mode: "STA>AP", apSsid: "maslow" };
  const scan = { state: "done", list: [{ ssid: "Home" }, { ssid: "Shop" }] };
  let w = netWarnings({ form: { ssid: "Shopp" }, current, scan, changes: [["ssid", "Shopp"], ["password", "x"]] });
  assert.match(w[0], /can't see Shopp/);
  assert.match(w[1], /hotspot maslow.*12345678/);
  w = netWarnings({ form: { ssid: "Shop" }, current, scan, changes: [["ssid", "Shop"], ["password", "x"]] });
  assert.equal(w.length, 1);                                            // seen: only the hotspot reminder
  assert.match(netWarnings({ form: { ssid: "Shop" }, current, scan: null, changes: [["ssid", "Shop"]] })[0], /Scan first/);
  assert.match(netWarnings({ form: { apPassword: "newpass12" }, current, scan, changes: [["apPassword", "newpass12"]] })[0], /new hotspot password/);
  assert.match(netWarnings({ form: {}, current: { ...current, mode: "STA" }, scan, changes: [] })[0], /need USB/);   // set elsewhere
  assert.match(netWarnings({ form: { mode: "AP" }, current, scan, changes: [["mode", "AP"]] })[0], /only through its hotspot maslow/);
  assert.deepEqual(netWarnings({ form: {}, current, scan, changes: [] }), []);
});
