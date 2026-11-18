// The machine settings Settings › Frame and belts edits, shared by the real-machine adapter and the simulator so
// both keep them in the same state shape. Each is a $/Maslow_… setting, as the current UI reads and writes it
// (ESP3D-WEBUI/www/js/tablet.js, maslow.js), with the range the firmware accepts (MachineConfig.cpp) and the
// default it writes into a new maslow.yaml.
//
// In state they are flat under d.settings: { workW: 0, workH: 0, offX: 0, … }. Work area 0 means automatic: the
// firmware sizes it from the anchors (MaslowKinematics::getWorkArea, 80 % of the frame's width, 60 % of its height).

export const MACHINE_SETTINGS = {
  workW:        { path: "Maslow_Work_Area_X", min: 0, max: 10000, def: 0, unit: "mm" },
  workH:        { path: "Maslow_Work_Area_Y", min: 0, max: 10000, def: 0, unit: "mm" },
  offX:         { path: "Maslow_Work_Area_Center_Offset_X", min: -5000, max: 5000, def: 0, unit: "mm" },
  offY:         { path: "Maslow_Work_Area_Center_Offset_Y", min: -5000, max: 5000, def: 0, unit: "mm" },
  parkX:        { path: "Maslow_Park_X", min: -10000, max: 10000, def: 0, unit: "mm" },
  parkY:        { path: "Maslow_Park_Y", min: -10000, max: 10000, def: 0, unit: "mm" },
  parkZ:        { path: "Maslow_Park_Z", min: -100, max: 100, def: 2, unit: "mm" },
  scaleX:       { path: "Maslow_Scale_X", min: 0.8, max: 1.2, def: 1, unit: "" },
  scaleY:       { path: "Maslow_Scale_Y", min: 0.8, max: 1.2, def: 1, unit: "" },
  retractLimit: { path: "Maslow_Apply_Tension_Belt_Retraction_Limit", min: 0, max: 4250, def: 300, unit: "mm" },
  retractForce: { path: "Maslow_Retract_Current_Threshold", min: 0, max: 3500, def: 1300, unit: "", integer: true }
};

export const defaultSettings = () =>
  Object.fromEntries(Object.entries(MACHINE_SETTINGS).map(([k, v]) => [k, v.def]));

// Why a value can't be saved, or null if it can
export function settingError(key, value) {
  const def = MACHINE_SETTINGS[key];
  if (!def) return "Unknown setting";
  if (typeof value !== "number" || !Number.isFinite(value)) return "Enter a number";
  if (def.integer && !Number.isInteger(value)) return "Enter a whole number";
  if (value < def.min || value > def.max) return `Between ${def.min} and ${def.max}`;
  return null;
}

// The work area the firmware uses: the set size, or for an axis at 0 the automatic one from the frame (null while
// the anchors aren't known)
export function effectiveWorkArea(settings, frame) {
  const fw = frame && Math.min(frame.tr[0] - frame.tl[0], frame.br[0] - frame.bl[0]);
  const fh = frame && Math.min(frame.tl[1] - frame.bl[1], frame.tr[1] - frame.br[1]);
  const w = settings.workW > 0 ? settings.workW : frame ? Math.max(fw * 0.8, 0) : null;
  const h = settings.workH > 0 ? settings.workH : frame ? Math.max(fh * 0.6, 0) : null;
  return { w, h, auto: { w: !(settings.workW > 0), h: !(settings.workH > 0) } };
}
