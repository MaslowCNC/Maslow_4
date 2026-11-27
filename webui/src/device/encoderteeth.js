// A wrong encoderTeeth (#1162, #1163): the teeth on the encoder roller scale every belt length, so a config copied
// from the other machine makes calibration and cuts wrong by the ratio. The current UI's check is encoderTeethWarning
// in ESP3D-WEBUI/www/js/tablet.js; this is the same comparison, as a plain function.

export const TEETH_PATH = "kinematics/MaslowKinematics/encoderTeeth";
export const EXTEND_PATH = "Maslow_Extend_Dist";
// Teeth on the encoder roller, by the [ESP800] board field
export const EXPECTED_ENCODER_TEETH = { maslow41: 22, unified: 35, unified36v: 35 };
const MACHINE_NAMES = { maslow41: "Maslow 4.1", unified: "Maslow Mini", unified36v: "Maslow Mini" };
// The firmware's upper limit for Maslow_Extend_Dist (mm)
export const EXTEND_DIST_MAX = 4250;

/**
 * What's wrong with this board's encoderTeeth, or null when it matches or the board is unknown.
 * teeth and extendDist are the settings as read (text or numbers). The Extend distance was saved at the wrong scale
 * too: belt lengths read teeth/expected of their true size, so once the teeth are fixed the same distance pays out a
 * different amount of belt. newExtendDist pays out what it did before (0 when the current one couldn't be read).
 * @returns {null | { board, machine, expected, teeth, errorPct, extendDist, newExtendDist }}
 */
export function encoderTeethCheck(board, teeth, extendDist) {
  const expected = EXPECTED_ENCODER_TEETH[board];
  const t = Number.parseFloat(teeth);
  if (!expected || !Number.isFinite(t) || Math.abs(t - expected) < 0.01) return null;
  const ext = Number.parseFloat(extendDist);
  const newExtendDist = t > 0 && ext > 0 ? Math.min(EXTEND_DIST_MAX, Math.ceil(ext * expected / t)) : 0;
  return {
    board, machine: MACHINE_NAMES[board], expected, teeth: t, errorPct: Math.round(Math.abs(t / expected - 1) * 100),
    extendDist: ext > 0 ? ext : null, newExtendDist
  };
}

/** The lines that fix it from the command box, in order: [command, what it does] */
export function encoderTeethCommands(c) {
  return [
    [`$/${TEETH_PATH}=${c.expected}`, "the teeth"],
    c.newExtendDist ? [`$/${EXTEND_PATH}=${c.newExtendDist}`, `the Extend distance, scaled from ${c.extendDist} mm`]
      : [`$/${EXTEND_PATH}=N`, `the Extend distance, where N is the current one times ${c.expected}/${c.teeth}`],
    ["$CO", "save the config"]
  ];
}
