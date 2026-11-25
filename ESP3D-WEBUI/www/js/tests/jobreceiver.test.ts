import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;
let api: any;

beforeAll(async () => {
  await import("../jobreceiver.js");
  api = g.__jobReceiverTestApi;
});

const ABUNDANCE = "https://abundance.maslowcnc.com";
const idle = { grbl: "Idle", job: null };

/** Send one message through the handler; returns what was posted back and what was uploaded. */
const send = async (data: any, opts: { origin?: string; state?: object; ready?: boolean; upload?: any } = {}) => {
  const posted: any[] = [];
  const uploaded: File[] = [];
  const refusals: any[] = [];
  const origin = opts.origin ?? ABUNDANCE;
  await api.handleMessage(
    { origin, data, source: { postMessage: (message: any, target: string) => posted.push({ message, target }) } },
    {
      uiReady: () => opts.ready ?? true,
      state: opts.state ?? idle,
      upload:
        opts.upload ??
        (async (file: File) => {
          uploaded.push(file);
          return { name: file.name, path: `/${file.name}`, size: file.size };
        }),
      notify: (name: string, refused: any) => refusals.push({ name, ...refused }),
    },
  );
  return { posted, uploaded, refusals };
};

const gcode = { type: "maslow:gcode", id: 7, name: "part.nc", gcode: "G0 X0\n" };

describe("who may send a file", () => {
  test("Abundance and a local development server", () => {
    expect(api.isAllowedOrigin(ABUNDANCE)).toBe(true);
    expect(api.isAllowedOrigin("http://localhost:5173")).toBe(true);
    expect(api.isAllowedOrigin("http://127.0.0.1:8080")).toBe(true);
  });

  test("nobody else", () => {
    expect(api.isAllowedOrigin("http://abundance.maslowcnc.com")).toBe(false);
    expect(api.isAllowedOrigin("https://abundance.maslowcnc.com.evil.example")).toBe(false);
    expect(api.isAllowedOrigin("https://evil.example")).toBe(false);
    expect(api.isAllowedOrigin("http://localhost.evil.example")).toBe(false);
    expect(api.isAllowedOrigin("null")).toBe(false);
  });

  test("a message from another page gets no reply and no upload", async () => {
    const { posted, uploaded } = await send(gcode, { origin: "https://evil.example" });
    expect(posted).toEqual([]);
    expect(uploaded).toEqual([]);
  });
});

describe("hello", () => {
  test("answered with ready, to the sender's origin only", async () => {
    const { posted } = await send({ type: "maslow:hello" });
    expect(posted).toEqual([{ message: { version: 1, type: "maslow:ready" }, target: ABUNDANCE }]);
  });

  test("not answered while the UI is still connecting", async () => {
    const { posted } = await send({ type: "maslow:hello" }, { ready: false });
    expect(posted).toEqual([]);
  });
});

describe("receiving G-code", () => {
  test("uploads the file and reports where it went", async () => {
    const { posted, uploaded } = await send(gcode);
    expect(uploaded.length).toBe(1);
    expect(uploaded[0].name).toBe("part.nc");
    expect(await uploaded[0].text()).toBe("G0 X0\n");
    expect(posted[0].message).toEqual({
      version: 1,
      type: "maslow:result",
      id: 7,
      ok: true,
      name: "part.nc",
      path: "/part.nc",
    });
  });

  test("takes a Blob as well as a string", async () => {
    const { uploaded } = await send({ ...gcode, gcode: new Blob(["G1 X5\n"]) });
    expect(await uploaded[0].text()).toBe("G1 X5\n");
  });

  test("refuses while a job runs or the machine moves, and tells the user", async () => {
    for (const state of [
      { grbl: "Run", job: { pct: 1, file: "/a.nc", line: 3 } },
      { grbl: "Idle", job: { pct: 1, file: "/a.nc", line: 3 } },
      { grbl: "Hold", job: null },
      { grbl: "Jog", job: null },
      { grbl: "Home", job: null },
    ]) {
      const { posted, uploaded, refusals } = await send(gcode, { state });
      expect(uploaded).toEqual([]);
      expect(posted[0].message).toMatchObject({ type: "maslow:result", id: 7, ok: false, reason: "busy" });
      expect(refusals[0]).toMatchObject({ name: "part.nc", reason: "busy" });
    }
  });

  test("an alarm doesn't block an upload", async () => {
    const { uploaded } = await send(gcode, { state: { grbl: "Alarm", job: null } });
    expect(uploaded.length).toBe(1);
  });

  test("refuses before the UI has connected", async () => {
    const { posted, uploaded } = await send(gcode, { ready: false });
    expect(uploaded).toEqual([]);
    expect(posted[0].message).toMatchObject({ ok: false, reason: "not-ready" });
  });

  test("refuses empty or missing G-code", async () => {
    for (const bad of ["", undefined, 42, new Blob([])]) {
      const { posted, uploaded } = await send({ ...gcode, gcode: bad });
      expect(uploaded).toEqual([]);
      expect(posted[0].message).toMatchObject({ ok: false, reason: "invalid" });
    }
  });

  test("passes on why an upload failed", async () => {
    const upload = async () => {
      throw Object.assign(new Error("x"), { reason: "failed", detail: "Upload failed" });
    };
    const { posted } = await send(gcode, { upload });
    expect(posted[0].message).toMatchObject({ ok: false, reason: "failed", detail: "Upload failed" });
  });

  test("other message types are ignored", async () => {
    const { posted } = await send({ type: "maslow:run" });
    expect(posted).toEqual([]);
  });
});

describe("file names", () => {
  test("a path becomes a plain name", () => {
    expect(api.safeFileName("../../config.yaml")).toBe("config.yaml.nc");
    expect(api.safeFileName("C:\\jobs\\part.nc")).toBe("part.nc");
    expect(api.safeFileName("/sd/part.gcode")).toBe("part.gcode");
  });

  test("anything that isn't G-code gets .nc", () => {
    expect(api.safeFileName("maslow.yaml")).toBe("maslow.yaml.nc");
    expect(api.safeFileName("My Chair")).toBe("My Chair.nc");
    expect(api.safeFileName("part.NC")).toBe("part.NC");
  });

  test("odd characters are replaced and a missing name gets a default", () => {
    expect(api.safeFileName("pärt <1>?.nc")).toBe("p_rt _1__.nc");
    expect(api.safeFileName("")).toBe("abundance.nc");
    expect(api.safeFileName(undefined)).toBe("abundance.nc");
    expect(api.safeFileName("...")).toBe("abundance.nc");
  });

  test("long names are cut, keeping the extension", () => {
    const name = api.safeFileName(`${"a".repeat(100)}.gcode`);
    expect(name.length).toBe(60);
    expect(name.endsWith(".gcode")).toBe(true);
  });
});
