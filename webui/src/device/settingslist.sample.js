// A believable [ESP400] answer for the simulator: the flash settings and config items a Maslow Mini lists, with the
// types, ranges and options the firmware gives them (SettingsDefinitions.cpp, WifiConfig.cpp, MachineConfig.cpp,
// MaslowKinematics.cpp). The simulator keeps the values (path → text) and builds the answer from them, so a save
// shows on the next read, as on the machine.

const onoff = [{ OFF: 0 }, { ON: 1 }], bool = [{ False: 0 }, { True: 1 }];
const flash = (P, T, V, x = {}) => ({ F: "nvs", P, H: P, T, V: String(V), ...x });
const tree = (P, T, V, x = {}) => ({ F: "tree", P: `/${P}`, H: `/${P}`, T, V: String(V), ...x });
const real = (v) => Number(v).toFixed(3);

// path → [store, type, default, extra]; config items in the order the firmware walks the tree
const SAMPLE = [
  ["Message/Level", "nvs", "B", "3", { O: [{ None: 0 }, { Error: 1 }, { Warning: 2 }, { Info: 3 }, { Debug: 4 }, { Verbose: 5 }] }],
  ["Config/Filename", "nvs", "S", "maslow.yaml", { M: 1, S: 50 }],
  ["Report/Status", "nvs", "I", "1", { M: 0, S: 3 }],
  ["Start/Message", "nvs", "S", "Grbl \\V [FluidNC \\B (\\X) \\H]", { M: 0, S: 40 }],
  ["Firmware/Build", "nvs", "S", "", { M: 0, S: 20 }],
  ["SD/FallbackCS", "nvs", "I", "-1", { M: -1, S: 40 }],
  ["GCode/Echo", "nvs", "B", "0", { O: onoff }],
  ["Hostname", "nvs", "S", "maslow", { M: 1, S: 32 }],
  ["WiFi/Mode", "nvs", "B", "3", { O: [{ Off: 0 }, { STA: 1 }, { AP: 2 }, { "STA>AP": 3 }] }],
  ["Sta/SSID", "nvs", "S", "Workshop", { M: 0, S: 32 }],
  ["Sta/Password", "nvs", "S", "********", { M: 8, S: 64 }],
  ["Sta/IPMode", "nvs", "B", "0", { O: [{ DHCP: 0 }, { Static: 1 }] }],
  ["Sta/IP", "nvs", "A", "0.0.0.0"],
  ["Sta/Gateway", "nvs", "A", "0.0.0.0"],
  ["Sta/Netmask", "nvs", "A", "0.0.0.0"],
  ["AP/SSID", "nvs", "S", "maslow", { M: 1, S: 32 }],
  ["AP/Password", "nvs", "S", "********", { M: 8, S: 64 }],
  ["AP/IP", "nvs", "A", "192.168.0.1"],
  ["AP/Channel", "nvs", "I", "1", { M: 1, S: 14 }],
  ["HTTP/Enable", "nvs", "B", "1", { O: onoff }],
  ["HTTP/Port", "nvs", "I", "80", { M: 1, S: 65001 }],
  ["Telnet/Enable", "nvs", "B", "1", { O: onoff }],
  ["Telnet/Port", "nvs", "I", "23", { M: 1, S: 65001 }],
  ["Bluetooth/Enable", "nvs", "B", "0", { O: onoff }],
  ["board", "tree", "S", "Maslow Mini", { M: 0, S: 255 }],
  ["name", "tree", "S", "Maslow Mini", { M: 0, S: 255 }],
  ["meta", "tree", "S", "", { M: 0, S: 255 }],
  ["stepping/engine", "tree", "B", "RMT", { O: [{ RMT: 0 }, { I2S_STATIC: 1 }, { TIMED: 2 }] }],
  ["stepping/idle_ms", "tree", "I", "240", { M: 0, S: 10000000 }],
  ["stepping/pulse_us", "tree", "I", "4", { M: 0, S: 30 }],
  ["stepping/segments", "tree", "I", "12", { M: 6, S: 20 }],
  ...["x", "y", "z"].flatMap((a) => [
    [`axes/${a}/steps_per_mm`, "tree", "R", a === "z" ? 100 : 80],
    [`axes/${a}/max_rate_mm_per_min`, "tree", "R", a === "z" ? 400 : 2000],
    [`axes/${a}/acceleration_mm_per_sec2`, "tree", "R", a === "z" ? 10 : 25],
    [`axes/${a}/max_travel_mm`, "tree", "R", a === "z" ? 100 : 4000]
  ]),
  ["axes/homing_runs", "tree", "I", "2", { M: 1, S: 5 }],
  ["kinematics/MaslowKinematics/tlX", "tree", "R", 0],
  ["kinematics/MaslowKinematics/tlY", "tree", "R", 2100],
  ["kinematics/MaslowKinematics/tlZ", "tree", "R", 100],
  ["kinematics/MaslowKinematics/trX", "tree", "R", 3000],
  ["kinematics/MaslowKinematics/trY", "tree", "R", 2100],
  ["kinematics/MaslowKinematics/trZ", "tree", "R", 56],
  ["kinematics/MaslowKinematics/blX", "tree", "R", 0],
  ["kinematics/MaslowKinematics/blY", "tree", "R", 0],
  ["kinematics/MaslowKinematics/blZ", "tree", "R", 34],
  ["kinematics/MaslowKinematics/brX", "tree", "R", 3000],
  ["kinematics/MaslowKinematics/brY", "tree", "R", 0],
  ["kinematics/MaslowKinematics/brZ", "tree", "R", 78],
  ["kinematics/MaslowKinematics/beltEndExtension", "tree", "R", 30],
  ["kinematics/MaslowKinematics/armLength", "tree", "R", 123.4],
  ["kinematics/MaslowKinematics/beltToothSpacing", "tree", "R", 2],
  ["kinematics/MaslowKinematics/encoderTeeth", "tree", "R", 20],
  ["kinematics/MaslowKinematics/maxSegmentLength", "tree", "R", 0.5],
  ["kinematics/MaslowKinematics/fixedZ", "tree", "B", "0", { O: bool }],
  ["arc_tolerance_mm", "tree", "R", 0.002],
  ["junction_deviation_mm", "tree", "R", 0.01],
  ["verbose_errors", "tree", "B", "1", { O: bool }],
  ["report_inches", "tree", "B", "0", { O: bool }],
  ["use_line_numbers", "tree", "B", "0", { O: bool }],
  ["planner_blocks", "tree", "I", "16", { M: 10, S: 120 }],
  ["MaslowAutoUpdate", "tree", "B", "0", { O: bool }],
  ["MaslowUpdateURL", "tree", "S", "", { M: 0, S: 255 }],
  ["Maslow_vertical", "tree", "B", "0", { O: bool }],
  ["Maslow_calibration_grid_width_mm_X", "tree", "R", 2000],
  ["Maslow_calibration_grid_height_mm_Y", "tree", "R", 1000],
  ["Maslow_calibration_grid_size", "tree", "I", "9", { M: 3, S: 9 }],
  ["Maslow_Retract_Current_Threshold", "tree", "I", "1300", { M: 0, S: 3500 }],
  ["Maslow_Acceptable_Calibration_Threshold", "tree", "R", 0.5],
  ["Maslow_Apply_Tension_Belt_Retraction_Limit", "tree", "R", 300],
  ["Maslow_Apply_Tension_Allow_Limiting", "tree", "B", "1", { O: bool }],
  ["Maslow_Extend_Dist", "tree", "R", 0],
  ["Maslow_Scale_X", "tree", "R", 1],
  ["Maslow_Scale_Y", "tree", "R", 1],
  ["Maslow_debugEnabled", "tree", "B", "0", { O: bool }],
  ["Maslow_Work_Area_X", "tree", "R", 0],
  ["Maslow_Work_Area_Y", "tree", "R", 0],
  ["Maslow_Work_Area_Center_Offset_X", "tree", "R", 0],
  ["Maslow_Work_Area_Center_Offset_Y", "tree", "R", 0],
  ["Maslow_spoilboardThickness", "tree", "R", 12.7],
  ["Maslow_workThickness", "tree", "R", 6.35],
  ["Maslow_Park_Z", "tree", "R", 2],
  ["Maslow_Park_X", "tree", "R", 0],
  ["Maslow_Park_Y", "tree", "R", 0],
  ["Maslow_Dust_Fan_Speed", "tree", "I", "0", { M: 0, S: 100 }]
];

/** The default values, path → text as [ESP400] gives it (config paths without the leading /) */
export const sampleValues = () =>
  Object.fromEntries(SAMPLE.map(([p, , T, v]) => [p, T === "R" ? real(v) : String(v)]));

/** [ESP400]'s answer, with the current values (path → text); passwords show as ******** like the firmware's */
export function sampleAnswer(values) {
  const EEPROM = SAMPLE.map(([p, store, T, v, x]) => {
    const val = /Password$/.test(p) ? "********" : T === "R" ? real(values[p] ?? v) : values[p] ?? v;
    return store === "nvs" ? flash(p, T, val, x) : tree(p, T, val, x);
  });
  return JSON.stringify({ EEPROM });
}
