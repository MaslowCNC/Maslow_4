// Every device action the UI can send, and what it will send to the firmware once a real adapter replaces the
// simulator. "exists" commands are in Maslow-Mini 3448d81f; "planned" ones need firmware changes listed in
// ESP3D-WEBUI/REWRITE.md (Later firmware changes) and tests in "Where the firmware and the UI meet".
export const COMMANDS = {
  retract:        { cmd: "$ALL", status: "exists" },
  extend:         { cmd: "$EXT", status: "exists" },
  doneExtending:  { cmd: null, status: "planned", note: "Extend should keep paying out until the user says the belts are attached" },
  tension:        { cmd: "$TKSLK", status: "exists" },
  release:        { cmd: "$CMP", status: "exists" },
  findAnchors:    { cmd: "$CAL", status: "exists" },
  toolCheck:      { cmd: "$Spindle/Home", status: "exists", note: "The result is spindle board log text today" },
  removeTool:     { cmd: "$Spindle/RemoveTool", status: "exists" },
  watchForTool:   { cmd: null, status: "exists", note: "The spindle board loads a bit by itself when it breaks the beam; nothing to send" },
  setZFromStickout: { cmd: null, status: "planned", note: "Set Z from the measured stick-out" },
  setWorkArea:    { cmd: "$/Maslow_Work_Area_X=…, $/Maslow_Work_Area_Y=…", status: "exists", note: "Clearing the anchors on a new work area is planned" },
  setSpoilboard:  { cmd: "$/Maslow_spoilboardThickness=…", status: "exists" },
  setStock:       { cmd: "$/Maslow_workThickness=…", status: "exists" },
  run:            { cmd: "$SD/Run=<file>", status: "exists" },
  continueJob:    { cmd: null, status: "planned", note: "Stop should save progress so a job can continue" },
  trace:          { cmd: null, status: "exists", note: "The current UI sends the boundary moves itself" },
  park:           { cmd: "Park (G-code move)", status: "exists" },
  stop:           { cmd: "$STOP", status: "exists", note: "Must halt queued motion at once (issue 18)" },
  clearError:     { cmd: "$X", status: "exists" },
  rehomeZ:        { cmd: "$Spindle/Home", status: "exists" },
  reboot:         { cmd: "[ESP444]RESTART", status: "exists" },
  jog:            { cmd: "$J=G91 …", status: "exists" },
  setHomeXY:      { cmd: "G10 L20 P0 X0 Y0", status: "exists" },
  setZ:           { cmd: "G10 L20 P0 Z0", status: "exists" },
  joinNetwork:    { cmd: "WiFi settings ($ESP…)", status: "exists", note: "Placeholder until the current WiFi dialog is documented" }
};
