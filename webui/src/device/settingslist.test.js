import { test } from "node:test";
import assert from "node:assert/strict";
import { parseSettingsList, grouped, valueError, isChanged, pendingChanges, espSetCommand, espFailure, withValues } from "./settingslist.js";
import { sampleAnswer, sampleValues } from "./settingslist.sample.js";
import { createSimulator } from "./simulator.js";

// Entries as the firmware's JSONencoder writes them (begin_webui: F, P, H, T, V, then S max and M min)
const ANSWER = JSON.stringify({ EEPROM: [
  { F: "nvs", P: "Hostname", H: "Hostname", T: "S", V: "maslow", S: 32, M: 1 },
  { F: "nvs", P: "Sta/Password", H: "Sta/Password", T: "S", V: "********", S: 64, M: 8 },
  { F: "nvs", P: "Report/Status", H: "Report/Status", T: "I", V: "1", S: 3, M: 0 },
  { F: "nvs", P: "Sta/IP", H: "Sta/IP", T: "A", V: "0.0.0.0" },
  { F: "nvs", P: "Telnet/Enable", H: "Telnet/Enable", T: "B", V: "1", O: [{ OFF: 0 }, { ON: 1 }] },
  { F: "tree", P: "/stepping/engine", H: "/stepping/engine", T: "B", V: "RMT", O: [{ RMT: 0 }, { TIMED: 2 }] },
  { F: "tree", P: "/planner_blocks", H: "/planner_blocks", T: "I", V: "16", S: 120, M: 10 },
  { F: "tree", P: "/kinematics/MaslowKinematics/tlY", H: "/kinematics/MaslowKinematics/tlY", T: "R", V: "2100.000" },
  { F: "tree", P: "/Maslow_Scale_X", H: "/Maslow_Scale_X", T: "R", V: "1.000" },
  { F: "tree", P: "/Maslow_vertical", H: "/Maslow_vertical", T: "B", V: "0", O: [{ False: 0 }, { True: 1 }] },
  { cmd: "not a setting" }
] });

test("parseSettingsList sorts [ESP400] into the Maslow and FluidNC lists, with each setting's type", () => {
  const items = parseSettingsList(ANSWER);
  assert.equal(items.length, 10);
  const by = Object.fromEntries(items.map((i) => [i.path, i]));
  assert.deepEqual([by.Hostname.list, by.Hostname.group, by.Hostname.store, by.Hostname.restart], ["fluidnc", "Board (flash)", "flash", true]);
  assert.equal(by["Report/Status"].restart, false);
  assert.deepEqual([by["Sta/Password"].secret, by["Sta/Password"].value], [true, ""]);
  assert.equal(by["Sta/IP"].type, "ip");
  assert.equal(by["Telnet/Enable"].type, "bool");
  // A choice given by name is matched to its option's value
  assert.deepEqual([by["stepping/engine"].type, by["stepping/engine"].value, by["stepping/engine"].group], ["choice", "0", "Config: stepping"]);
  assert.deepEqual([by.planner_blocks.group, by.planner_blocks.restart], ["Config: general", null]);
  assert.deepEqual([by["kinematics/MaslowKinematics/tlY"].list, by["kinematics/MaslowKinematics/tlY"].name], ["maslow", "tlY"]);
  // Real numbers get the firmware's range from MachineConfig.cpp, and a pointer to their friendly editor
  assert.deepEqual([by.Maslow_Scale_X.type, by.Maslow_Scale_X.min, by.Maslow_Scale_X.max, by.Maslow_Scale_X.friendly], ["number", 0.8, 1.2, "Frame and belts"]);
  assert.equal(by.Maslow_vertical.type, "bool");
  assert.throws(() => parseSettingsList("{}"), /No settings/);
});

test("grouped keeps the list's own groups first and filters by name or value", () => {
  const items = parseSettingsList(ANSWER);
  assert.deepEqual(grouped(items, "maslow").map(([g, l]) => [g, l.length]), [["Maslow", 1], ["Frame anchors and arms", 1], ["Also in Frame and belts", 1]]);
  assert.deepEqual(grouped(items, "fluidnc").map(([g]) => g), ["Board (flash)", "Config: stepping", "Config: general"]);
  assert.deepEqual(grouped(items, "fluidnc", "telnet").flatMap(([, l]) => l.map((i) => i.path)), ["Telnet/Enable"]);
  assert.deepEqual(grouped(items, "fluidnc", "RMT").flatMap(([, l]) => l.map((i) => i.path)), ["stepping/engine"]);
});

test("valueError follows the setting's type and range", () => {
  const by = Object.fromEntries(parseSettingsList(ANSWER).map((i) => [i.path, i]));
  assert.equal(valueError(by.planner_blocks, "9"), "Between 10 and 120");
  assert.equal(valueError(by.planner_blocks, "12.5"), "Enter a whole number");
  assert.equal(valueError(by.Maslow_Scale_X, "1.3"), "Between 0.8 and 1.2");
  assert.equal(valueError(by.Maslow_Scale_X, "1.01"), null);
  assert.equal(valueError(by["kinematics/MaslowKinematics/tlY"], "abc"), "Enter a number");
  assert.equal(valueError(by.Hostname, ""), "Can't be empty");
  assert.equal(valueError(by["Sta/Password"], "short"), "At least 8 characters");
  assert.equal(valueError(by["Sta/IP"], "192.168.1.300"), "Enter an IP address, like 192.168.0.1");
  assert.equal(valueError(by["Telnet/Enable"], "2"), "Choose an option");
});

test("only real changes are sent: numbers compare as numbers, an empty password box is no change", () => {
  const items = parseSettingsList(ANSWER);
  const by = Object.fromEntries(items.map((i) => [i.path, i]));
  assert.equal(isChanged(by.Maslow_Scale_X, "1"), false);
  assert.equal(isChanged(by["Sta/Password"], ""), false);
  const { changes, errors } = pendingChanges(items, { Maslow_Scale_X: "1.0", Hostname: "shop-maslow", planner_blocks: "5", "Sta/Password": "" });
  assert.deepEqual(changes, [{ path: "Hostname", store: "flash", value: "shop-maslow" }]);
  assert.deepEqual(errors, [{ path: "planner_blocks", error: "Between 10 and 120" }]);
  assert.deepEqual(withValues(items, changes).find((i) => i.path === "Hostname").value, "shop-maslow");
});

test("a flash setting is saved with [ESP401], the value %-encoded; failures are found in the body", () => {
  const by = Object.fromEntries(parseSettingsList(ANSWER).map((i) => [i.path, i]));
  assert.equal(espSetCommand(by["Sta/Password"], "a b&c"), "[ESP401]P=Sta/Password T=S V=a%20b%26c");
  assert.equal(espFailure("ok\n"), null);
  assert.equal(espFailure("error:3\n"), "error:3");
});

test("the simulator's sample parses, and a save shows on the next read and in Frame and belts", () => {
  assert.ok(parseSettingsList(sampleAnswer(sampleValues())).length > 60);
  const sim = createSimulator();
  sim.send("readSettingsList");
  const items = sim.getState().settingsList.items;
  assert.equal(items.find((i) => i.path === "Maslow_spoilboardThickness").value, "12.700");
  sim.send("saveSettingsList", { changes: [
    { path: "Maslow_Scale_X", store: "config", value: "1.01" },
    { path: "Hostname", store: "flash", value: "shop" }
  ] });
  let s = sim.getState();
  assert.equal(s.settings.scaleX, 1.01);
  assert.deepEqual(s.settingsList.save, { state: "saved", restart: true });
  assert.ok(s.log.some((l) => l.endsWith("[ESP401]P=Hostname T=S V=shop")));
  assert.ok(s.log.at(-1).endsWith("$CO"));
  sim.send("readSettingsList");
  assert.equal(sim.getState().settingsList.items.find((i) => i.path === "Hostname").value, "shop");
  sim.send("saveSettingsList", { changes: [{ path: "Maslow_Scale_X", store: "config", value: "2" }] });
  s = sim.getState();
  assert.equal(s.settings.scaleX, 1.01);
  assert.equal(s.settingsList.save.state, "failed");
});
