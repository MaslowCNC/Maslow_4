import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;
let api: any;

beforeAll(async () => {
  await import("../checkupdatesdlg.js");
  api = g.__checkUpdatesTestApi;
});

const rel = (tag: string, prerelease = false, draft = false) => ({ tag_name: tag, prerelease, draft });

// Newest-first, as GitHub returns them.
const releases = [
  rel("nightly", true),
  rel("v1.28.0-exp.10", true),
  rel("v1.28.0-exp.2", true),
  rel("v1.28.0-exp.1", true),
  rel("v1.27.0"),
  rel("v1.26.0"),
];

describe("parseVersion", () => {
  test("finds the version in firmware and UI version strings", () => {
    expect(api.parseVersion("fluidnc v1.26.0 (claude/zhome-popup-tweaks-71ab5d-8700a9bf9)"))
      .toEqual({ major: 1, minor: 26, patch: 0, pre: [] });
    expect(api.parseVersion("github.com/BarbourSmith/ESP3D-WEBUI@v1.26.0-1336-g8700a9bf9"))
      .toEqual({ major: 1, minor: 26, patch: 0, pre: [] });
    expect(api.parseVersion("fluidnc v1.28.0-exp.1 (branch-abc)").pre).toEqual(["exp", "1"]);
    expect(api.parseVersion("v1.28.0-exp.1-4-gabc1234-dirty").pre).toEqual(["exp", "1"]);
    expect(api.parseVersion("v1.27.0-dirty").pre).toEqual([]);
    expect(api.parseVersion("v1.21")).toEqual({ major: 1, minor: 21, patch: 0, pre: [] });
    expect(api.parseVersion("nightly")).toBeNull();
  });
});

describe("isNewerVersion", () => {
  test("a release is newer than its own pre-releases", () => {
    expect(api.isNewerVersion("v1.28.0-exp.1", "v1.28.0")).toBe(true);
    expect(api.isNewerVersion("v1.28.0", "v1.28.0-exp.1")).toBe(false);
  });

  test("later pre-releases are newer, with -exp.10 after -exp.2", () => {
    expect(api.isNewerVersion("v1.28.0-exp.1", "v1.28.0-exp.2")).toBe(true);
    expect(api.isNewerVersion("v1.28.0-exp.2", "v1.28.0-exp.10")).toBe(true);
    expect(api.isNewerVersion("v1.28.0-exp.2", "v1.28.0-exp.1")).toBe(false);
  });

  test("follows semver exactly (semver.org spec item 11 examples)", () => {
    const order = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
      "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"];
    for (let i = 0; i + 1 < order.length; i++) {
      expect(api.isNewerVersion(order[i], order[i + 1])).toBe(true);
      expect(api.isNewerVersion(order[i + 1], order[i])).toBe(false);
    }
    // Without the dot, "exp10" is one text identifier and sorts before "exp2".
    expect(api.isNewerVersion("v1.28.0-exp2", "v1.28.0-exp10")).toBe(false);
  });

  test("the same version, or a local build of it, is not an update", () => {
    expect(api.isNewerVersion("fluidnc v1.27.0 (main-abc)", "v1.27.0")).toBe(false);
    expect(api.isNewerVersion("ESP3D-WEBUI@v1.27.0-3-gabc1234", "v1.27.0")).toBe(false);
    expect(api.isNewerVersion("fluidnc v1.26.0 (x)", "v1.27.0")).toBe(true);
    expect(api.isNewerVersion("v1.27.0", "v1.26.0")).toBe(false);
  });
});

describe("selectReleaseForStream", () => {
  test("Release stream on a stable version offers only stable releases", () => {
    expect(api.selectReleaseForStream(releases, "release", ["fluidnc v1.26.0"]).tag_name).toBe("v1.27.0");
  });

  test("someone on a pre-release is offered newer pre-releases on any stream", () => {
    expect(api.selectReleaseForStream(releases, "release", ["fluidnc v1.28.0-exp.1 (x)"]).tag_name)
      .toBe("v1.28.0-exp.10");
    expect(api.selectReleaseForStream(releases, "experimental", ["v1.27.0"]).tag_name).toBe("v1.28.0-exp.10");
  });

  test("a pre-release user is offered the stable release once it's out", () => {
    const withStable = [rel("v1.28.0"), ...releases];
    expect(api.selectReleaseForStream(withStable, "release", ["v1.28.0-exp.2"]).tag_name).toBe("v1.28.0");
    expect(api.selectReleaseForStream(withStable, "experimental", ["v1.28.0-exp.2"]).tag_name).toBe("v1.28.0");
  });

  test("drafts are skipped and Nightly picks the nightly release", () => {
    expect(api.selectReleaseForStream([rel("v9.0.0", false, true), ...releases], "release", []).tag_name)
      .toBe("v1.27.0");
    expect(api.selectReleaseForStream(releases, "nightly", []).tag_name).toBe("nightly");
  });
});
