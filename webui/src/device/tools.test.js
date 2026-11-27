import { test } from "node:test";
import assert from "node:assert/strict";
import { parseTools, serializeTools, toolErrors, applyEdit, newId, addRecent, readRecent, stickoutSuggestions, matchSuggestion, toolActions, TOOLS_IDLE } from "./tools.js";

const BIT = { id: "t1", name: "1/4 in end mill", type: "endmill", diameter: 6.35, stickout: 15, notes: "" };

test("a missing or empty file is an empty library with nothing to say", () => {
  assert.deepEqual(parseTools(null), { tools: [], problem: null });
  assert.deepEqual(parseTools("  \n"), { tools: [], problem: null });
});

test("a file that isn't JSON, or has no list, is an empty library with one problem", () => {
  const r = parseTools("{nope");
  assert.deepEqual(r.tools, []);
  assert.match(r.problem, /isn't valid JSON/);
  assert.match(parseTools('{"version":1}').problem, /no list/);
});

test("good entries are kept, bad ones dropped and counted once", () => {
  const r = parseTools(JSON.stringify({ tools: [BIT, { name: "" }, { name: "x", diameter: 3, stickout: -1 }, { name: "V", type: "weird", diameter: "6", stickout: "10" }] }));
  assert.equal(r.tools.length, 2);
  assert.equal(r.tools[1].type, "other");
  assert.equal(r.tools[1].diameter, 6);
  assert.match(r.problem, /2 entries/);
});

test("a bare array is read too, and repeated ids are made unique", () => {
  const r = parseTools(JSON.stringify([BIT, { ...BIT, name: "Other" }]));
  assert.deepEqual(r.tools.map((t) => t.id), ["t1", "t1x"]);
  assert.equal(r.problem, null);
});

test("serialize then parse gives the same list; empty notes are left out", () => {
  const list = [BIT, { ...BIT, id: "t2", name: "V", type: "vbit", notes: "60°" }];
  const text = serializeTools(list);
  assert.ok(!text.includes('"notes": ""'));
  assert.deepEqual(parseTools(text).tools, list);
});

test("toolErrors checks the name, diameter and the Set Z step's stick-out limit", () => {
  assert.equal(toolErrors(BIT, [], 30), null);
  const e = toolErrors({ name: " ", diameter: 0, stickout: 30 }, [], 30);
  assert.deepEqual(Object.keys(e).sort(), ["diameter", "name", "stickout"]);
  assert.match(toolErrors({ ...BIT, id: "t9", name: "1/4 IN END MILL" }, [BIT], 30).name, /already saved/);
  assert.equal(toolErrors({ ...BIT }, [BIT], 30), null);   // editing itself
  assert.ok(toolErrors({ ...BIT, diameter: NaN }, [], 30).diameter);
});

test("applyEdit adds with a new id, replaces by id and removes", () => {
  let list = applyEdit([], { tool: { ...BIT, id: undefined, name: "  A  " } });
  assert.deepEqual(list.map((t) => [t.id, t.name]), [["t1", "A"]]);
  list = applyEdit(list, { tool: { ...BIT, id: undefined, name: "B" } });
  assert.equal(list[1].id, "t2");
  list = applyEdit(list, { tool: { ...list[0], stickout: 12 } });
  assert.equal(list[0].stickout, 12);
  assert.deepEqual(applyEdit(list, { remove: "t1" }).map((t) => t.id), ["t2"]);
  assert.equal(newId([{ id: "t2" }]), "t3");
});

test("recent stick-outs: newest first, each once, at most five", () => {
  let r = [];
  for (const v of [10, 12, 10.001, 14, 15, 16, 17]) r = addRecent(r, v);
  assert.deepEqual(r, [17, 16, 15, 14, 10.001]);
  assert.deepEqual(addRecent(r, -1), r);
  assert.deepEqual(readRecent("[3, \"x\", -2, 4]"), [3, 4]);
  assert.deepEqual(readRecent("nope"), []);
  assert.deepEqual(readRecent(null), []);
});

test("suggestions: bits with names, then typed values not already a bit's", () => {
  const s = stickoutSuggestions([BIT], [15, 12], (mm) => String(mm));
  assert.deepEqual(s.map((x) => [x.text, x.label]), [["15", "1/4 in end mill"], ["12", "Typed before"]]);
  assert.equal(matchSuggestion(" 12 ", s).mm, 12);
  assert.equal(matchSuggestion("13", s), null);
});

function fakeBoard(initial) {
  let file = initial, state = { ...TOOLS_IDLE };
  const logs = [];
  const acts = toolActions({
    get: () => state, set: (p) => { state = { ...state, ...p }; }, log: (l) => logs.push(l),
    io: {
      read: async () => { if (file == null) throw new Error("HTTP 404"); return file; },
      upload: async (_p, text) => { file = text; return { status: "Ok" }; }
    },
    failed: (a) => (/fail/i.test(a.status) ? a.status : null)
  });
  return { acts, get state() { return state; }, get file() { return file; }, set file(v) { file = v; }, logs };
}

test("reading a missing file is an empty library, not a failure", async () => {
  const b = fakeBoard(null);
  await b.acts.readTools();
  assert.equal(b.state.status, "ready");
  assert.deepEqual(b.state.list, []);
  assert.equal(b.state.detail, null);
});

test("saving reads the board's list again, so another connection's bit is kept", async () => {
  const b = fakeBoard(serializeTools([BIT]));
  await b.acts.readTools();
  b.file = serializeTools([BIT, { ...BIT, id: "t2", name: "From the phone" }]);   // another tab saved meanwhile
  assert.equal(await b.acts.saveTool({ tool: { name: "New", type: "drill", diameter: 3, stickout: 20 } }), true);
  assert.deepEqual(parseTools(b.file).tools.map((t) => t.name), ["1/4 in end mill", "From the phone", "New"]);
  assert.equal(b.state.list.length, 3);
  await b.acts.deleteTool({ id: "t1" });
  assert.deepEqual(b.state.list.map((t) => t.id), ["t2", "t3"]);
  assert.match(b.logs.at(-1), /deleted 1\/4 in end mill/);
});
