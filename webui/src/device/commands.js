// Every device action the UI can send, and what it becomes on the machine API from PR #1132 (docs/protocol.md,
// ESP3D-WEBUI/www/js/machine.js). `api` is the machine.js action when one exists. "exists" works on Maslow-Main
// today; "planned" needs firmware or machine.js work listed in ESP3D-WEBUI/REWRITE.md ("Where the firmware and
// the UI meet").
export const COMMANDS = {
  retract:        { api: "retractBelts()", cmd: "$ALL", status: "exists" },
  extend:         { api: "extendBelts()", cmd: "$EXT", status: "exists" },
  doneExtending:  { api: null, cmd: null, status: "planned", note: "Extend still ends by itself at extendDist" },
  tension:        { api: "applyTension()", cmd: "$TKSLK", status: "exists", note: "Ends in READY_TO_CUT, or TENSIONED (10) when the belts don't fit the anchors" },
  release:        { api: "releaseTension()", cmd: "$CMP", status: "exists" },
  findAnchors:    { api: "calibrate()", cmd: "$CAL", status: "exists", note: "No progress field yet (issue 16)" },
  toolCheck:      { api: null, cmd: "$Spindle/Home", status: "exists", note: "The result is spindle board log text; needs an action and a report field" },
  removeTool:     { api: null, cmd: "$Spindle/RemoveTool", status: "exists", note: "Same" },
  watchForTool:   { api: null, cmd: null, status: "exists", note: "The spindle board loads a bit by itself when it breaks the beam; nothing to send" },
  setZFromStickout: { api: null, cmd: null, status: "planned", note: "Set Z from the measured stick-out" },
  setWorkArea:    { api: "setSetting() + saveConfig()", cmd: "$/Maslow_Work_Area_X=…, $/Maslow_Work_Area_Y=…", status: "exists", note: "Clearing the anchors on a new work area is planned" },
  setSpoilboard:  { api: "setSetting()", cmd: "$/Maslow_spoilboardThickness=…", status: "exists" },
  setStock:       { api: "setSetting()", cmd: "$/Maslow_workThickness=…", status: "exists" },
  run:            { api: "runFile(path)", cmd: "$SD/Run=<file>", status: "exists" },
  continueJob:    { api: "resumeFile(path, line)", cmd: "$SD/RunFrom=<line>,<file>", status: "exists", note: "From the report's STOPPED:line,file; discardStopped() forgets it" },
  trace:          { api: "tracePath(points)", cmd: "G0 moves, one at a time", status: "exists" },
  park:           { api: "moveTo()", cmd: "G0 to the park position", status: "exists", note: "No park action of its own yet" },
  stop:           { api: "stop()", cmd: "0x18, then $STOP", status: "exists" },
  clearError:     { api: null, cmd: "$X", status: "exists", note: "Needs a machine.js action" },
  rehomeZ:        { api: null, cmd: "$Spindle/Home", status: "exists", note: "Needs a machine.js action" },
  reboot:         { api: null, cmd: "[ESP444]RESTART", status: "exists", note: "HTTP; not in machine.js" },
  jog:            { api: "jog(delta, {feed})", cmd: "$J=G91 G21 …", status: "exists" },
  setHomeXY:      { api: null, cmd: "G10 L20 P0 X0 Y0", status: "exists", note: "Needs a machine.js action" },
  setZ:           { api: null, cmd: "G10 L20 P0 Z0", status: "exists", note: "Needs a machine.js action" },
  joinNetwork:    { api: null, cmd: "WiFi settings ([ESP401])", status: "exists", note: "HTTP; placeholder until the WiFi dialog is designed" }
};
